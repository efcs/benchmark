// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "benchmark/benchmark.h"
#include "internal_macros.h"

#ifndef BENCHMARK_OS_WINDOWS
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>

#include "benchmark_commandline.h"
#include "check.h"
#include "colorprint.h"
#include "complexity.h"
#include "counter.h"
#include "internal_macros.h"
#include "log.h"
#include "mutex.h"
#include "re.h"
#include "reporter.h"
#include "statistics.h"
#include "string_util.h"
#include "sysinfo.h"
#include "timers.h"
#include "utility.h"

namespace benchmark {

namespace {
static const size_t kMaxIterations = 1000000000;
}  // end namespace

namespace internal {

class ThreadManager {
 public:
  ThreadManager(int num_threads)
      : alive_threads_(num_threads), start_stop_barrier_(num_threads) {}

  Mutex& GetBenchmarkMutex() const RETURN_CAPABILITY(benchmark_mutex_) {
    return benchmark_mutex_;
  }

  bool StartStopBarrier() EXCLUDES(end_cond_mutex_) {
    return start_stop_barrier_.wait();
  }

  void NotifyThreadComplete() EXCLUDES(end_cond_mutex_) {
    start_stop_barrier_.removeThread();
    if (--alive_threads_ == 0) {
      MutexLock lock(end_cond_mutex_);
      end_condition_.notify_all();
    }
  }

  void WaitForAllThreads() EXCLUDES(end_cond_mutex_) {
    MutexLock lock(end_cond_mutex_);
    end_condition_.wait(lock.native_handle(),
                        [this]() { return alive_threads_ == 0; });
  }

 public:
  struct Result {
    double real_time_used = 0;
    double cpu_time_used = 0;
    double manual_time_used = 0;
    int64_t bytes_processed = 0;
    int64_t items_processed = 0;
    int complexity_n = 0;
    std::string report_label_;
    std::string error_message_;
    bool has_error_ = false;
    UserCounters counters;
  };
  GUARDED_BY(GetBenchmarkMutex()) Result results;

 private:
  mutable Mutex benchmark_mutex_;
  std::atomic<int> alive_threads_;
  Barrier start_stop_barrier_;
  Mutex end_cond_mutex_;
  Condition end_condition_;
};

// Timer management class
class ThreadTimer {
 public:
  ThreadTimer() = default;

  // Called by each thread
  void StartTimer() {
    running_ = true;
    start_real_time_ = ChronoClockNow();
    start_cpu_time_ = ThreadCPUUsage();
  }

  // Called by each thread
  void StopTimer() {
    CHECK(running_);
    running_ = false;
    real_time_used_ += ChronoClockNow() - start_real_time_;
    // Floating point error can result in the subtraction producing a negative
    // time. Guard against that.
    cpu_time_used_ += std::max<double>(ThreadCPUUsage() - start_cpu_time_, 0);
  }

  // Called by each thread
  void SetIterationTime(double seconds) { manual_time_used_ += seconds; }

  bool running() const { return running_; }

  // REQUIRES: timer is not running
  double real_time_used() {
    CHECK(!running_);
    return real_time_used_;
  }

  // REQUIRES: timer is not running
  double cpu_time_used() {
    CHECK(!running_);
    return cpu_time_used_;
  }

  // REQUIRES: timer is not running
  double manual_time_used() {
    CHECK(!running_);
    return manual_time_used_;
  }

 private:
  bool running_ = false;        // Is the timer running
  double start_real_time_ = 0;  // If running_
  double start_cpu_time_ = 0;   // If running_

  // Accumulated time so far (does not contain current slice if running_)
  double real_time_used_ = 0;
  double cpu_time_used_ = 0;
  // Manually set iteration time. User sets this with SetIterationTime(seconds).
  double manual_time_used_ = 0;
};

namespace {
using CallbackEntry = std::pair<int, CallbackType>;
using CallbackList = std::vector<CallbackEntry>;

int GetNextCallbackID() {
  static int ID = 0;
  return ID++;
}

CallbackList* GetCallbackList() {
  static CallbackList callbacks;
  return &callbacks;
}

void InvokeCallbacks(CallbackKind K, JSON& J) {
  for (auto& CB : *GetCallbackList()) CB.second(K, J);
}

}  // end namespace

int RegisterCallback(CallbackType CB) {
  int ID = GetNextCallbackID();
  GetCallbackList()->emplace_back(ID, CB);
  return ID;
}

void EraseCallback(int ID) {
  auto& CBList = *GetCallbackList();
  CBList.erase(std::remove_if(
      CBList.begin(), CBList.end(),
      [=](CallbackEntry const& Ent) { return Ent.first == ID; }));
}

void ClearCallbacks() { GetCallbackList()->clear(); }

namespace {

JSON CreateRunReport(const benchmark::internal::BenchmarkInstance& b,
                     const internal::ThreadManager::Result& results,
                     size_t iters, double seconds) {
  // Create report about this benchmark run.

  // Report the total iterations across all threads.
  int64_t iterations = static_cast<int64_t>(iters) * b.threads;

  JSON json_report = {
      {"name", b.name},
      {"kind", results.has_error_ ? "error" : "normal"},
      {"iterations", static_cast<int64_t>(iters) * b.threads},
  };
  if (!results.report_label_.empty())
    json_report["label"] = results.report_label_;
  if (results.has_error_) json_report["error_message"] = results.error_message_;

  if (!results.has_error_) {
    json_report["time_unit"] = GetTimeUnitString(b.info->time_unit);
    auto UnitMul = GetTimeUnitMultiplier(b.info->time_unit);

    if (results.bytes_processed > 0 && seconds > 0.0) {
      json_report["bytes_per_second"] = (results.bytes_processed / seconds);
    }
    if (results.items_processed > 0 && seconds > 0.0) {
      json_report["items_per_second"] = (results.items_processed / seconds);
    }

    double real_time = b.info->use_manual_time ? results.manual_time_used
                                               : results.real_time_used;
    real_time *= UnitMul;
    json_report["real_accumulated_time"] = real_time;
    json_report["real_iteration_time"] =
        real_time / static_cast<double>(iterations);
    double cpu_time = results.cpu_time_used * UnitMul;
    json_report["cpu_accumulated_time"] = cpu_time;
    json_report["cpu_iteration_time"] =
        cpu_time / static_cast<double>(iterations);

    if (results.complexity_n != 0)
      json_report["complexity_n"] = results.complexity_n;
    if (b.info->complexity != 0) json_report["complexity"] = b.info->complexity;

    // report.statistics = b.statistics;
    auto counters_cp = results.counters;
    internal::Finish(&counters_cp, seconds, b.threads);

    JSON counters = counters_cp;
    json_report["counters"] = counters;
  }
  return json_report;
}

// Execute one thread of benchmark b for the specified number of iterations.
// Adds the stats collected for the thread into *total.
void RunInThread(const benchmark::internal::BenchmarkInstance* b, size_t iters,
                 int thread_id, internal::ThreadManager* manager) {
  internal::ThreadTimer timer;
  State st(iters, b->arg, thread_id, b->threads, &timer, manager);
  b->benchmark->Run(st);
  CHECK(st.iterations() == st.max_iterations)
      << "Benchmark returned before State::KeepRunning() returned false!";
  {
    MutexLock l(manager->GetBenchmarkMutex());
    internal::ThreadManager::Result& results = manager->results;
    results.cpu_time_used += timer.cpu_time_used();
    results.real_time_used += timer.real_time_used();
    results.manual_time_used += timer.manual_time_used();
    results.bytes_processed += st.bytes_processed();
    results.items_processed += st.items_processed();
    results.complexity_n += st.complexity_length_n();
    internal::Increment(&results.counters, st.counters);
  }
  manager->NotifyThreadComplete();
}

JSON RunSingleBenchmarkImp(const benchmark::internal::BenchmarkInstance& b,
                           std::vector<JSON>* complexity_reports) {
  std::vector<JSON> run_reports;
  std::vector<JSON> internal_complexity_reports;
  if (complexity_reports == nullptr)
    complexity_reports = &internal_complexity_reports;
  const bool has_explicit_iteration_count = b.info->iterations != 0;
  size_t iters = has_explicit_iteration_count ? b.info->iterations : 1;
  std::unique_ptr<internal::ThreadManager> manager;
  std::vector<std::thread> pool(b.threads - 1);
  const int repeats = b.info->repetitions != 0 ? b.info->repetitions
                                               : FLAGS_benchmark_repetitions;
  for (int repetition_num = 0; repetition_num < repeats; repetition_num++) {
    for (;;) {
      // Try benchmark
      VLOG(2) << "Running " << b.name << " for " << iters << "\n";

      manager.reset(new internal::ThreadManager(b.threads));
      for (std::size_t ti = 0; ti < pool.size(); ++ti) {
        pool[ti] = std::thread(&RunInThread, &b, iters,
                               static_cast<int>(ti + 1), manager.get());
      }
      RunInThread(&b, iters, 0, manager.get());
      manager->WaitForAllThreads();
      for (std::thread& thread : pool) thread.join();
      internal::ThreadManager::Result results;
      {
        MutexLock l(manager->GetBenchmarkMutex());
        results = manager->results;
      }
      manager.reset();
      // Adjust real/manual time stats since they were reported per thread.
      results.real_time_used /= b.threads;
      results.manual_time_used /= b.threads;

      VLOG(2) << "Ran in " << results.cpu_time_used << "/"
              << results.real_time_used << "\n";

      // Base decisions off of real time if requested by this benchmark.
      double seconds = results.cpu_time_used;
      if (b.info->use_manual_time) {
        seconds = results.manual_time_used;
      } else if (b.info->use_real_time) {
        seconds = results.real_time_used;
      }

      const double min_time = !IsZero(b.info->min_time)
                                  ? b.info->min_time
                                  : FLAGS_benchmark_min_time;

      // Determine if this run should be reported; Either it has
      // run for a sufficient amount of time or because an error was reported.
      const bool should_report =
          repetition_num > 0 ||
          has_explicit_iteration_count  // An exact iteration count was
                                        // requested
          || results.has_error_ || iters >= kMaxIterations ||
          seconds >= min_time  // the elapsed time is large enough
          // CPU time is specified but the elapsed real time greatly exceeds the
          // minimum time. Note that user provided timers are except from this
          // sanity check.
          || ((results.real_time_used >= 5 * min_time) &&
              !b.info->use_manual_time);

      if (should_report) {
        JSON report = CreateRunReport(b, results, iters, seconds);
        bool IsError = report.at("kind").get<std::string>() == "error";
        if (!IsError && b.info->complexity != oNone)
          complexity_reports->push_back(report);
        run_reports.push_back(report);
        break;
      }

      // See how much iterations should be increased by
      // Note: Avoid division by zero with max(seconds, 1ns).
      double multiplier = min_time * 1.4 / std::max(seconds, 1e-9);
      // If our last run was at least 10% of FLAGS_benchmark_min_time then we
      // use the multiplier directly. Otherwise we use at most 10 times
      // expansion.
      // NOTE: When the last run was at least 10% of the min time the max
      // expansion should be 14x.
      bool is_significant = (seconds / min_time) > 0.1;
      multiplier = is_significant ? multiplier : std::min(10.0, multiplier);
      if (multiplier <= 1.0) multiplier = 2.0;
      double next_iters = std::max(multiplier * iters, iters + 1.0);
      if (next_iters > kMaxIterations) {
        next_iters = kMaxIterations;
      }
      VLOG(3) << "Next iters: " << next_iters << ", " << multiplier << "\n";
      iters = static_cast<int>(next_iters + 0.5);
    }
  }

  // Calculate additional statistics
  auto stat_reports = ComputeStats(run_reports, b.info->statistics);
  if ((b.info->complexity != oNone) && b.last_benchmark_instance) {
    auto additional_run_stats = ComputeBigO(b, *complexity_reports);
    if (!additional_run_stats.is_null())
      stat_reports.push_back(additional_run_stats);
    complexity_reports->clear();
  }

  const bool report_aggregates_only =
      repeats != 1 &&
      (b.info->report_mode == internal::RM_Unspecified
           ? FLAGS_benchmark_report_aggregates_only
           : b.info->report_mode == internal::RM_ReportAggregatesOnly);

  JSON instance_info{{"args", b.arg}, {"threads", b.threads}};

  JSON reports_json = {{"name", b.name},
                       {"family", b.info->family_name},
                       {"instance", instance_info},
                       {"runs", run_reports},
                       {"stats", stat_reports},
                       {"report_aggregates_only", report_aggregates_only}};
  return reports_json;
}

}  // namespace

}  // namespace internal

State::State(size_t max_iters, const std::vector<int>& ranges, int thread_i,
             int n_threads, internal::ThreadTimer* timer,
             internal::ThreadManager* manager)
    : started_(false),
      finished_(false),
      total_iterations_(max_iters + 1),
      range_(ranges),
      bytes_processed_(0),
      items_processed_(0),
      complexity_n_(0),
      error_occurred_(false),
      counters(),
      thread_index(thread_i),
      threads(n_threads),
      max_iterations(max_iters),
      timer_(timer),
      manager_(manager) {
  CHECK(max_iterations != 0) << "At least one iteration must be run";
  CHECK(total_iterations_ != 0) << "max iterations wrapped around";
  CHECK_LT(thread_index, threads) << "thread_index must be less than threads";
}

void State::PauseTiming() {
  // Add in time accumulated so far
  CHECK(started_ && !finished_ && !error_occurred_);
  timer_->StopTimer();
}

void State::ResumeTiming() {
  CHECK(started_ && !finished_ && !error_occurred_);
  timer_->StartTimer();
}

void State::SkipWithError(const char* msg) {
  CHECK(msg);
  error_occurred_ = true;
  {
    MutexLock l(manager_->GetBenchmarkMutex());
    if (manager_->results.has_error_ == false) {
      manager_->results.error_message_ = msg;
      manager_->results.has_error_ = true;
    }
  }
  total_iterations_ = 1;
  if (timer_->running()) timer_->StopTimer();
}

void State::SetIterationTime(double seconds) {
  timer_->SetIterationTime(seconds);
}

void State::SetLabel(const char* label) {
  MutexLock l(manager_->GetBenchmarkMutex());
  manager_->results.report_label_ = label;
}

void State::StartKeepRunning() {
  CHECK(!started_ && !finished_);
  started_ = true;
  manager_->StartStopBarrier();
  if (!error_occurred_) ResumeTiming();
}

void State::FinishKeepRunning() {
  CHECK(started_ && (!finished_ || error_occurred_));
  if (!error_occurred_) {
    PauseTiming();
  }
  // Total iterations has now wrapped around zero. Fix this.
  total_iterations_ = 1;
  finished_ = true;
  manager_->StartStopBarrier();
}

namespace internal {
namespace {

JSON GetNameAndStatFieldWidths(
    const std::vector<BenchmarkInstance>& benchmarks) {
  // Determine the width of the name field using a minimum width of 10.
  bool has_repetitions = FLAGS_benchmark_repetitions > 1;
  size_t name_field_width = 10;
  size_t stat_field_width = 0;
  for (const BenchmarkInstance& benchmark : benchmarks) {
    name_field_width =
        std::max<size_t>(name_field_width, benchmark.name.size());
    has_repetitions |= benchmark.info->repetitions > 1;

    for (const auto& Stat : benchmark.info->statistics)
      stat_field_width = std::max<size_t>(stat_field_width, Stat.name_.size());
  }
  if (has_repetitions) name_field_width += 1 + stat_field_width;
  JSON res{{"name_field_width", name_field_width},
           {"stat_field_width", stat_field_width}};
  return res;
}

void DisplayContextOnce() {
  static bool guard =
      (PrintBasicContext(&GetErrorStream(), GetContext()), true);
  ((void)guard);
}
}  // end namespace
}  // end namespace internal

JSON GetContext() {
#if defined(NDEBUG)
  const char build_type[] = "release";
#else
  const char build_type[] = "debug";
#endif
  // Print header here
  JSON context{
      {"date", LocalDateTimeString()},
      {"library_build_type", build_type},
      {"cpu_info", CPUInfo::Get()}};
  return context;
}

JSON RunBenchmarks(const std::vector<internal::BenchmarkInstance>& benchmarks,
                   bool ReportConsole) {
  internal::DisplayContextOnce();
  auto invokeAllCallbacks = [&](CallbackKind K, JSON& J) {
    internal::InvokeCallbacks(K, J);
    if (ReportConsole) GetGlobalConsoleReporter()(K, J);
  };
  JSON initial_info = internal::GetNameAndStatFieldWidths(benchmarks);
  invokeAllCallbacks(CK_Initial, initial_info);

  std::vector<JSON> complexity_reports;
  JSON benchmark_res = JSON::array();
  for (const auto& benchmark : benchmarks) {
    JSON report =
        internal::RunSingleBenchmarkImp(benchmark, &complexity_reports);
    invokeAllCallbacks(CK_Report, report);
    benchmark_res.push_back(report);
  }

  invokeAllCallbacks(CK_Final, benchmark_res);
  return benchmark_res;
}

JSON RunBenchmark(internal::BenchmarkInstance const& I, bool ReportConsole) {
  internal::DisplayContextOnce();
  auto invokeAllCallbacks = [&](CallbackKind K, JSON& J) {
    internal::InvokeCallbacks(K, J);
    if (ReportConsole) GetGlobalConsoleReporter()(K, J);
  };

  std::vector<internal::BenchmarkInstance> V;
  V.push_back(I);
  JSON initial_info = internal::GetNameAndStatFieldWidths(V);
  invokeAllCallbacks(CK_Initial, initial_info);

  JSON Res = internal::RunSingleBenchmarkImp(I, nullptr);
  invokeAllCallbacks(CK_Report, Res);

  invokeAllCallbacks(CK_Final, Res);
  return Res;
}

size_t RunSpecifiedBenchmarks() {
  std::string spec = FLAGS_benchmark_filter;
  if (spec.empty() || spec == "all")
    spec = ".";  // Regexp that matches all benchmarks

  ErrorCode EC;
  std::vector<internal::BenchmarkInstance> benchmarks =
      FindBenchmarks(spec, &EC);
  if (EC) {
    GetErrorStream() << "Failed to initialize regex \"" << spec
                     << "\". Error: " << EC.message() << std::endl;
    return 0;
  }
  if (benchmarks.empty()) {
    GetErrorStream() << "Failed to match any benchmarks against regex: " << spec
                     << "\n";
    return 0;
  }

  if (FLAGS_benchmark_list_tests) {
    for (auto const& benchmark : benchmarks)
      GetOutputStream() << benchmark.name << "\n";
    return benchmarks.size();
  }

  JSON Res = RunBenchmarks(benchmarks, /*ReportConsole*/ true);
  // If --benchmark_out=<fname> is specified, write the final results to it.
  if (!FLAGS_benchmark_out.empty()) {
    std::ofstream output_file;
    output_file.open(FLAGS_benchmark_out);
    if (!output_file.is_open()) {
      GetErrorStream() << "invalid file name: '" << FLAGS_benchmark_out
                       << std::endl;
      std::exit(1);
    }
    JSON full_res{{"context", GetContext()}, {"benchmarks", Res}};
    output_file << std::setw(2) << full_res << std::endl;
  }

  return benchmarks.size();
}


}  // end namespace benchmark
