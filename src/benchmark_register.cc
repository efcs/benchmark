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
#include <sstream>
#include <thread>

#include "benchmark_commandline.h"
#include "check.h"
#include "complexity.h"
#include "log.h"
#include "mutex.h"
#include "re.h"
#include "statistics.h"
#include "string_util.h"
#include "sysinfo.h"
#include "timers.h"

namespace benchmark {

namespace {
// For non-dense Range, intermediate values are powers of kRangeMultiplier.
static const int kRangeMultiplier = 8;
// The size of a benchmark family determines is the number of inputs to repeat
// the benchmark on. If this is "large" then warn the user during configuration.
static const size_t kMaxFamilySize = 100;
}  // end namespace

namespace internal {

static bool IsZero(double n) {
  return std::abs(n) < std::numeric_limits<double>::epsilon();
}

//=============================================================================//
//                         BenchmarkFamilies
//=============================================================================//

// Class for managing registered benchmarks.  Note that each registered
// benchmark identifies a family of related benchmarks to run.
class BenchmarkFamilies {
 public:
  static BenchmarkFamilies* GetInstance();

  // Registers a benchmark family and returns the index assigned to it.
  void AddBenchmark(std::unique_ptr<Benchmark> family);

  // Clear all registered benchmark families.
  void ClearBenchmarks();

  // Extract the list of benchmark instances that match the specified
  // regular expression.
  ErrorCode FindBenchmarks(const std::string& re,
                           std::vector<BenchmarkInstance>* benchmarks);

  static const BenchmarkInfoBase* GetInfo(const Benchmark* B) {
    return static_cast<const BenchmarkInfoBase*>(B);
  }

  static const BenchmarkInfoBase* GetInfoForIndex(size_t Idx) {
    return GetInfo(GetInstance()->families_[Idx].get());
  }

 private:
  BenchmarkFamilies() {}

  std::vector<std::unique_ptr<Benchmark>> families_;
  Mutex mutex_;
};

BenchmarkFamilies* BenchmarkFamilies::GetInstance() {
  static BenchmarkFamilies instance;
  return &instance;
}

void BenchmarkFamilies::AddBenchmark(std::unique_ptr<Benchmark> family) {
  MutexLock l(mutex_);
  family->index = families_.size();
  families_.push_back(std::move(family));
}

void BenchmarkFamilies::ClearBenchmarks() {
  MutexLock l(mutex_);
  families_.clear();
  families_.shrink_to_fit();
}

std::vector<BenchmarkInstance> Benchmark::GenerateInstances() const {
  // Family was deleted or benchmark doesn't match

  const std::vector<int> one_thread = {1};

  const std::vector<int>* instance_thread_counts =
      (thread_counts.empty()
           ? &one_thread
           : &static_cast<const std::vector<int>&>(thread_counts));
  const size_t family_size =
      std::max<size_t>(args.size(), 0) * instance_thread_counts->size();
  // The benchmark will be run at least 'family_size' different inputs.
  // If 'family_size' is very large warn the user.
  if (family_size > kMaxFamilySize) {
    GetErrorStream() << "The number of inputs is very large. " << family_name
                     << " will be repeated at least " << family_size
                     << " times.\n";
  }
  Benchmark* this_nc = const_cast<Benchmark*>(this);
  BenchmarkInfoBase* this_base = this_nc;
  auto make_instance = [&, this](std::vector<int> const& instance_args,
                                 int num_threads, JSON const& data) {
    BenchmarkInstance instance;
    instance.name = family_name;
    instance.benchmark = this_nc;
    instance.info = this_base;
    instance.arg = instance_args;
    instance.threads = num_threads;
    instance.input_data = data;

    // Add arguments to instance name
    size_t arg_i = 0;
    for (auto const& arg : instance_args) {
      instance.name += "/";

      if (arg_i < arg_names.size()) {
        const auto& arg_name = arg_names[arg_i];
        if (!arg_name.empty()) {
          instance.name += StringPrintF("%s:", arg_names[arg_i].c_str());
        }
      }

      instance.name += StringPrintF("%d", arg);
      ++arg_i;
    }

    if (!data.is_null() && data.count("name")) {
      instance.name += "/input:";
      instance.name += data.at("name");
    } else if (!data.is_null()) {
      instance.name += "/with_inputs";
      CHECK(data.is_object());
      for (auto It = data.begin(); It != data.end(); ++It) {
        std::string Key = It.key();
        JSON Value = It.value();
        CHECK(Value.is_primitive());
        instance.name += "/" + Key + ":" + It.value().dump();
      }
    }

    if (!IsZero(min_time))
      instance.name += StringPrintF("/min_time:%0.3f", min_time);
    if (iterations != 0)
      instance.name += StringPrintF("/iterations:%d", iterations);
    if (repetitions != 0)
      instance.name += StringPrintF("/repeats:%d", repetitions);

    if (use_manual_time) {
      instance.name += "/manual_time";
    } else if (use_real_time) {
      instance.name += "/real_time";
    }

    // Add the number of threads used to the name
    if (!thread_counts.empty()) {
      instance.name += StringPrintF("/threads:%d", instance.threads);
    }
    instance.last_benchmark_instance = false;
    return instance;
  };
  std::vector<BenchmarkInstance> instance_list(family_size);
  const std::vector<std::vector<int>> default_args(1);
  std::vector<JSONPointer> default_inputs;
  default_inputs.emplace_back();

  for (auto& A : args.empty() ? default_args : args)
    for (auto& J : user_data.empty() ? default_inputs : user_data)
      for (auto NT : *instance_thread_counts)
        instance_list.push_back(make_instance(A, NT, J.get()));

  return instance_list;
}

ErrorCode BenchmarkFamilies::FindBenchmarks(
    const std::string& spec, std::vector<BenchmarkInstance>* benchmarks) {
  Regex re;
  if (ErrorCode EC = re.Init(spec)) return EC;

  // Special list of thread counts to use when none are specified
  const std::vector<int> one_thread = {1};

  MutexLock l(mutex_);
  for (std::unique_ptr<Benchmark>& family : families_) {
    // Family was deleted or benchmark doesn't match
    if (!family) continue;
    bool AddedInstance = false;
    for (auto const& Instance : family->GenerateInstances()) {
      if (re.Match(Instance.name)) {
        AddedInstance = true;
        benchmarks->push_back(Instance);
      }
    }
    if (AddedInstance) benchmarks->back().last_benchmark_instance = true;
  }
  return ErrorCode::Success();
}

Benchmark* RegisterBenchmarkInternal(Benchmark* bench) {
  std::unique_ptr<Benchmark> bench_ptr(bench);
  BenchmarkFamilies* families = BenchmarkFamilies::GetInstance();
  families->AddBenchmark(std::move(bench_ptr));
  return bench;
}


//=============================================================================//
//                               Benchmark
//=============================================================================//

BenchmarkInfoBase::BenchmarkInfoBase(const char* name)
    : family_name(name),
      report_mode(RM_Unspecified),
      time_unit(kNanosecond),
      range_multiplier(kRangeMultiplier),
      min_time(0),
      iterations(0),
      repetitions(0),
      use_real_time(false),
      use_manual_time(false),
      complexity(oNone),
      complexity_lambda(nullptr) {}

Benchmark::Benchmark(const char* name) : BenchmarkInfoBase(name) {
  ComputeStatistics("mean", StatisticsMean);
  ComputeStatistics("median", StatisticsMedian);
  ComputeStatistics("stddev", StatisticsStdDev);
}

Benchmark::~Benchmark() {}

void Benchmark::AddRange(std::vector<int>* dst, int lo, int hi, int mult) {
  CHECK_GE(lo, 0);
  CHECK_GE(hi, lo);
  CHECK_GE(mult, 2);

  // Add "lo"
  dst->push_back(lo);

  static const int kint32max = std::numeric_limits<int32_t>::max();

  // Now space out the benchmarks in multiples of "mult"
  for (int32_t i = 1; i < kint32max / mult; i *= mult) {
    if (i >= hi) break;
    if (i > lo) {
      dst->push_back(i);
    }
  }
  // Add "hi" (if different from "lo")
  if (hi != lo) {
    dst->push_back(hi);
  }
}

Benchmark* Benchmark::WithData(JSON Data) {
  user_data.emplace_back(std::move(Data));
  return this;
}

Benchmark* Benchmark::Arg(int x) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == 1);
  args.push_back({x});
  return this;
}

Benchmark* Benchmark::Unit(TimeUnit unit) {
  time_unit = unit;
  return this;
}

Benchmark* Benchmark::Range(int start, int limit) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == 1);
  std::vector<int> arglist;
  AddRange(&arglist, start, limit, range_multiplier);

  for (int i : arglist) {
    args.push_back({i});
  }
  return this;
}

Benchmark* Benchmark::Ranges(const std::vector<std::pair<int, int>>& ranges) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == static_cast<int>(ranges.size()));
  std::vector<std::vector<int>> arglists(ranges.size());
  std::size_t total = 1;
  for (std::size_t i = 0; i < ranges.size(); i++) {
    AddRange(&arglists[i], ranges[i].first, ranges[i].second, range_multiplier);
    total *= arglists[i].size();
  }

  std::vector<std::size_t> ctr(arglists.size(), 0);

  for (std::size_t i = 0; i < total; i++) {
    std::vector<int> tmp;
    tmp.reserve(arglists.size());

    for (std::size_t j = 0; j < arglists.size(); j++) {
      tmp.push_back(arglists[j].at(ctr[j]));
    }

    args.push_back(std::move(tmp));

    for (std::size_t j = 0; j < arglists.size(); j++) {
      if (ctr[j] + 1 < arglists[j].size()) {
        ++ctr[j];
        break;
      }
      ctr[j] = 0;
    }
  }
  return this;
}

Benchmark* Benchmark::ArgName(const std::string& name) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == 1);
  arg_names = {name};
  return this;
}

Benchmark* Benchmark::ArgNames(const std::vector<std::string>& names) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == static_cast<int>(names.size()));
  arg_names = names;
  return this;
}

Benchmark* Benchmark::DenseRange(int start, int limit, int step) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == 1);
  CHECK_GE(start, 0);
  CHECK_LE(start, limit);
  for (int arg = start; arg <= limit; arg += step) {
    args.push_back({arg});
  }
  return this;
}

Benchmark* Benchmark::Args(const std::vector<int>& new_args) {
  CHECK(ArgsCnt() == -1 || ArgsCnt() == static_cast<int>(new_args.size()));
  args.push_back(new_args);
  return this;
}

Benchmark* Benchmark::Apply(void (*custom_arguments)(Benchmark* benchmark)) {
  custom_arguments(this);
  return this;
}

Benchmark* Benchmark::RangeMultiplier(int multiplier) {
  CHECK(multiplier > 1);
  range_multiplier = multiplier;
  return this;
}


Benchmark* Benchmark::MinTime(double t) {
  CHECK(t > 0.0);
  CHECK(iterations == 0);
  min_time = t;
  return this;
}


Benchmark* Benchmark::Iterations(size_t n) {
  CHECK(n > 0);
  CHECK(IsZero(min_time));
  iterations = n;
  return this;
}

Benchmark* Benchmark::Repetitions(int n) {
  CHECK(n > 0);
  repetitions = n;
  return this;
}

Benchmark* Benchmark::ReportAggregatesOnly(bool value) {
  report_mode = value ? RM_ReportAggregatesOnly : RM_Default;
  return this;
}

Benchmark* Benchmark::UseRealTime() {
  CHECK(!use_manual_time)
      << "Cannot set UseRealTime and UseManualTime simultaneously.";
  use_real_time = true;
  return this;
}

Benchmark* Benchmark::UseManualTime() {
  CHECK(!use_real_time)
      << "Cannot set UseRealTime and UseManualTime simultaneously.";
  use_manual_time = true;
  return this;
}

Benchmark* Benchmark::Complexity(BigO complexity_val) {
  complexity = complexity_val;
  return this;
}

Benchmark* Benchmark::Complexity(BigOFunc* complexity_func) {
  complexity_lambda = complexity_func;
  complexity = oLambda;
  return this;
}

Benchmark* Benchmark::ComputeStatistics(std::string name,
                                        StatisticsFunc* SFunc) {
  statistics.emplace_back(name, SFunc);
  return this;
}

Benchmark* Benchmark::Threads(int t) {
  CHECK_GT(t, 0);
  thread_counts.push_back(t);
  return this;
}

Benchmark* Benchmark::ThreadRange(int min_threads, int max_threads) {
  CHECK_GT(min_threads, 0);
  CHECK_GE(max_threads, min_threads);

  AddRange(&thread_counts, min_threads, max_threads, 2);
  return this;
}

Benchmark* Benchmark::DenseThreadRange(int min_threads, int max_threads,
                                       int stride) {
  CHECK_GT(min_threads, 0);
  CHECK_GE(max_threads, min_threads);
  CHECK_GE(stride, 1);

  for (auto i = min_threads; i < max_threads; i += stride) {
    thread_counts.push_back(i);
  }
  thread_counts.push_back(max_threads);
  return this;
}

Benchmark* Benchmark::ThreadPerCpu() {
  thread_counts.push_back(CPUInfo::Get().num_cpus);
  return this;
}

void Benchmark::SetName(const char* xname) { family_name = xname; }

int Benchmark::ArgsCnt() const {
  if (args.empty()) {
    if (arg_names.empty()) return -1;
    return static_cast<int>(arg_names.size());
  }
  return static_cast<int>(args.front().size());
}

//=============================================================================//
//                            FunctionBenchmark
//=============================================================================//

void FunctionBenchmark::Run(State& st) { func_(st); }

}  // end namespace internal

BenchmarkInstanceList FindBenchmarks(std::string const& Regex, ErrorCode* EC) {
  if (EC) EC->clear();
  const char* const AdjRegex =
      (Regex.empty() || Regex == "all") ? "." : Regex.c_str();
  BenchmarkInstanceList res;
  ErrorCode MyEC = internal::BenchmarkFamilies::GetInstance()->FindBenchmarks(
      AdjRegex, &res);
  if (MyEC && EC) {
    *EC = MyEC;
  } else if (MyEC) {
    GetErrorStream() << "Failed to initialize regex \"" << Regex
                     << "\". Error: " << MyEC.message() << std::endl;
  }
  return res;
}

BenchmarkInstanceList FindSpecifiedBenchmarks() {
  return FindBenchmarks(FLAGS_benchmark_filter);
}

void ClearRegisteredBenchmarks() {
  internal::BenchmarkFamilies::GetInstance()->ClearBenchmarks();
}

}  // end namespace benchmark
