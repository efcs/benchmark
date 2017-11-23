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
#include "benchmark_api_internal.h"
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

#include "check.h"
#include "colorprint.h"
#include "commandlineflags.h"
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

DEFINE_bool(benchmark_list_tests, false,
            "Print a list of benchmarks. This option overrides all other "
            "options.");

DEFINE_string(benchmark_filter, ".",
              "A regular expression that specifies the set of benchmarks "
              "to execute.  If this flag is empty, no benchmarks are run.  "
              "If this flag is the string \"all\", all benchmarks linked "
              "into the process are run.");

DEFINE_double(benchmark_min_time, 0.5,
              "Minimum number of seconds we should run benchmark before "
              "results are considered significant.  For cpu-time based "
              "tests, this is the lower bound on the total cpu time "
              "used by all threads that make up the test.  For real-time "
              "based tests, this is the lower bound on the elapsed time "
              "of the benchmark execution, regardless of number of "
              "threads.");

DEFINE_int32(benchmark_repetitions, 1,
             "The number of runs of each benchmark. If greater than 1, the "
             "mean and standard deviation of the runs will be reported.");

DEFINE_bool(benchmark_report_aggregates_only, false,
            "Report the result of each benchmark repetitions. When 'true' is "
            "specified only the mean, standard deviation, and other statistics "
            "are reported for repeated benchmarks.");

DEFINE_string(benchmark_format, "console",
              "The format to use for console output. Valid values are "
              "'console', or 'json'.");

DEFINE_string(benchmark_out, "", "The file to write additonal output to");

DEFINE_string(benchmark_color, "auto",
              "Whether to use colors in the output.  Valid values: "
              "'true'/'yes'/1, 'false'/'no'/0, and 'auto'. 'auto' means to use "
              "colors if the output is being sent to a terminal and the TERM "
              "environment variable is set to a terminal type that supports "
              "colors.");

DEFINE_bool(benchmark_counters_tabular, false,
            "Whether to use tabular format when printing user counters to "
            "the console.  Valid values: 'true'/'yes'/1, 'false'/'no'/0."
            "Defaults to false.");

DEFINE_int32(v, 0, "The level of verbose logging to output");

namespace benchmark {

namespace {
static const size_t kMaxIterations = 1000000000;
}  // end namespace

namespace internal {

static bool IsZero(double n) {
  return std::abs(n) < std::numeric_limits<double>::epsilon();
}

void UseCharPointer(char const volatile*) {}

#ifdef BENCHMARK_HAS_NO_BUILTIN_UNREACHABLE
BENCHMARK_NORETURN void UnreachableImp(const char* FName, int Line) {
  std::cerr << FName << ":" << Line << " executing unreachable code!"
            << std::endl;
  std::abort();
}
#endif

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

JSON RunBenchmark(const benchmark::internal::BenchmarkInstance& b,
                  std::vector<JSON>* complexity_reports) {
  std::vector<JSON> run_reports;

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
    stat_reports.insert(stat_reports.end(), additional_run_stats.begin(),
                        additional_run_stats.end());
    complexity_reports->clear();
  }

  const bool report_aggregates_only =
      repeats != 1 &&
      (b.info->report_mode == internal::RM_Unspecified
           ? FLAGS_benchmark_report_aggregates_only
           : b.info->report_mode == internal::RM_ReportAggregatesOnly);

  JSON reports_json = {{"name", b.name},
                       {"family", b.info->index},
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

JSON RunBenchmarks(const std::vector<BenchmarkInstance>& benchmarks,
                   BenchmarkReporter* console_reporter) {
  // Note the file_reporter can be null.
  CHECK(console_reporter != nullptr);

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

#if defined(NDEBUG)
  const char build_type[] = "release";
#else
  const char build_type[] = "debug";
#endif
  // Print header here
  JSON context{{"name_field_width", name_field_width},
               {"date", LocalDateTimeString()},
               {"library_build_type", build_type},
               {"cpu_info", CPUInfo::Get()}};
  JSON benchmark_res = JSON::array();

  // Keep track of runing times of all instances of current benchmark
  std::vector<JSON> complexity_reports;

  // We flush streams after invoking reporter methods that write to them. This
  // ensures users get timely updates even when streams are not line-buffered.
  auto flushStreams = [](BenchmarkReporter* reporter) {
    if (!reporter) return;
    std::flush(reporter->GetOutputStream());
    std::flush(reporter->GetErrorStream());
  };

  if (console_reporter->ReportContext(context)) {
    flushStreams(console_reporter);

    for (const auto& benchmark : benchmarks) {
      JSON report = RunBenchmark(benchmark, &complexity_reports);
      benchmark_res.push_back(report);
      console_reporter->ReportResults(report);
      flushStreams(console_reporter);
    }
  }
  console_reporter->Finalize();
  flushStreams(console_reporter);
  JSON res{{"context", context}, {"benchmarks", benchmark_res}};
  return res;
}

std::unique_ptr<BenchmarkReporter> CreateReporter(
    std::string const& name, ConsoleReporter::OutputOptions output_opts) {
  typedef std::unique_ptr<BenchmarkReporter> PtrType;
  if (name == "console") {
    return PtrType(new ConsoleReporter(output_opts));
  } else if (name == "json") {
    return PtrType(new JSONReporter);
  } else {
    std::cerr << "Unexpected format: '" << name << "'\n";
    std::exit(1);
  }
}

}  // end namespace

ConsoleReporter::OutputOptions GetOutputOptions(bool force_no_color) {
  int output_opts = ConsoleReporter::OO_Defaults;
  if ((FLAGS_benchmark_color == "auto" && IsColorTerminal()) ||
      IsTruthyFlagValue(FLAGS_benchmark_color)) {
    output_opts |= ConsoleReporter::OO_Color;
  } else {
    output_opts &= ~ConsoleReporter::OO_Color;
  }
  if(force_no_color) {
    output_opts &= ~ConsoleReporter::OO_Color;
  }
  if(FLAGS_benchmark_counters_tabular) {
    output_opts |= ConsoleReporter::OO_Tabular;
  } else {
    output_opts &= ~ConsoleReporter::OO_Tabular;
  }
  return static_cast< ConsoleReporter::OutputOptions >(output_opts);
}

}  // end namespace internal

size_t RunSpecifiedBenchmarks() {
  return RunSpecifiedBenchmarks(&std::cout, &std::cerr);
}

size_t RunSpecifiedBenchmarks(std::ostream* out, std::ostream* err) {
  std::string spec = FLAGS_benchmark_filter;
  if (spec.empty() || spec == "all")
    spec = ".";  // Regexp that matches all benchmarks

  // Setup the reporters
  std::unique_ptr<BenchmarkReporter> console_reporter =
      internal::CreateReporter(FLAGS_benchmark_format,
                               internal::GetOutputOptions());

  console_reporter->SetOutputStream(out);
  console_reporter->SetErrorStream(err);

  auto& Out = console_reporter->GetOutputStream();
  auto& Err = console_reporter->GetErrorStream();

  std::vector<internal::BenchmarkInstance> benchmarks;
  if (!FindBenchmarksInternal(spec, &benchmarks, &Err)) return 0;

  if (benchmarks.empty()) {
    Err << "Failed to match any benchmarks against regex: " << spec << "\n";
    return 0;
  }

  if (FLAGS_benchmark_list_tests) {
    for (auto const& benchmark : benchmarks) Out << benchmark.name << "\n";
  } else {
    JSON Res = internal::RunBenchmarks(benchmarks, console_reporter.get());
    std::string const& fname = FLAGS_benchmark_out;

    if (!fname.empty()) {
      std::ofstream output_file;
      output_file.open(fname);
      if (!output_file.is_open()) {
        Err << "invalid file name: '" << fname << std::endl;
        std::exit(1);
      }
      output_file << Res << std::endl;
    }
  }
  return benchmarks.size();
}

namespace internal {

void PrintUsageAndExit() {
  fprintf(stdout,
          "benchmark"
          " [--benchmark_list_tests={true|false}]\n"
          "          [--benchmark_filter=<regex>]\n"
          "          [--benchmark_min_time=<min_time>]\n"
          "          [--benchmark_repetitions=<num_repetitions>]\n"
          "          [--benchmark_report_aggregates_only={true|false}\n"
          "          [--benchmark_format=<console|json>]\n"
          "          [--benchmark_out=<filename>]\n"
          "          [--benchmark_out_format=<json|console>]\n"
          "          [--benchmark_color={auto|true|false}]\n"
          "          [--benchmark_counters_tabular={true|false}]\n"
          "          [--v=<verbosity>]\n");
  exit(0);
}

void ParseCommandLineFlags(int* argc, char** argv) {
  using namespace benchmark;
  for (int i = 1; i < *argc; ++i) {
    if (ParseBoolFlag(argv[i], "benchmark_list_tests",
                      &FLAGS_benchmark_list_tests) ||
        ParseStringFlag(argv[i], "benchmark_filter", &FLAGS_benchmark_filter) ||
        ParseDoubleFlag(argv[i], "benchmark_min_time",
                        &FLAGS_benchmark_min_time) ||
        ParseInt32Flag(argv[i], "benchmark_repetitions",
                       &FLAGS_benchmark_repetitions) ||
        ParseBoolFlag(argv[i], "benchmark_report_aggregates_only",
                      &FLAGS_benchmark_report_aggregates_only) ||
        ParseStringFlag(argv[i], "benchmark_format", &FLAGS_benchmark_format) ||
        ParseStringFlag(argv[i], "benchmark_out", &FLAGS_benchmark_out) ||
        ParseStringFlag(argv[i], "benchmark_color", &FLAGS_benchmark_color) ||
        // "color_print" is the deprecated name for "benchmark_color".
        // TODO: Remove this.
        ParseStringFlag(argv[i], "color_print", &FLAGS_benchmark_color) ||
        ParseBoolFlag(argv[i], "benchmark_counters_tabular",
                        &FLAGS_benchmark_counters_tabular) ||
        ParseInt32Flag(argv[i], "v", &FLAGS_v)) {
      for (int j = i; j != *argc - 1; ++j) argv[j] = argv[j + 1];

      --(*argc);
      --i;
    } else if (IsFlag(argv[i], "help")) {
      PrintUsageAndExit();
    }
  }
  if (FLAGS_benchmark_format != "console" && FLAGS_benchmark_format != "json") {
    PrintUsageAndExit();
  }
  if (FLAGS_benchmark_color.empty()) {
    PrintUsageAndExit();
  }
}

int InitializeStreams() {
  static std::ios_base::Init init;
  return 0;
}

}  // end namespace internal

void Initialize(int* argc, char** argv) {
  internal::ParseCommandLineFlags(argc, argv);
  internal::LogLevel() = FLAGS_v;
}

BenchmarkInstanceList FindBenchmarks() {
  return FindBenchmarks(FLAGS_benchmark_filter);
}
JSON RunBenchmarks(BenchmarkInstanceList const& Benches) {
  // Setup the reporters
  std::unique_ptr<BenchmarkReporter> console_reporter =
      internal::CreateReporter(FLAGS_benchmark_format,
                               internal::GetOutputOptions());
  return internal::RunBenchmarks(Benches, console_reporter.get());
}
bool ReportUnrecognizedArguments(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    fprintf(stderr, "%s: error: unrecognized command-line flag: %s\n", argv[0], argv[i]);
  }
  return argc > 1;
}

}  // end namespace benchmark
