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
#include "timers.h"

#include <cstdlib>

#include <iostream>
#include <tuple>
#include <vector>

#include "check.h"

namespace benchmark {

BenchmarkReporter::BenchmarkReporter()
    : output_stream_(&std::cout), error_stream_(&std::cerr) {}

BenchmarkReporter::~BenchmarkReporter() {}

void BenchmarkReporter::PrintBasicContext(std::ostream *out,
                                          const json& context) {
  CHECK(out) << "cannot be null";
  auto &Out = *out;

  Out << context.at("date").get<std::string>() << "\n";

  const json &info = context["cpu_info"];
  int num_cpus = info.at("num_cpus");
  Out << "Run on (" << num_cpus << " X "
      << info.at("frequency_mhz") << " MHz CPU "
      << ((num_cpus > 1) ? "s" : "") << ")\n";
  const json &caches = info.at("caches");
  if (caches.size() != 0) {
    Out << "CPU Caches:\n";
    for (auto &CInfo : caches) {
      Out << "  L" << CInfo.at("level") << " "
          << CInfo.at("type").get<std::string>() << " "
          << (CInfo.at("size").get<int>() / 1000) << "K";
      int num_sharing = CInfo.at("num_sharing");
      if (num_sharing != 0)
        Out << " (x" << (num_cpus / num_sharing) << ")";
      Out << "\n";
    }
  }

  if (info.at("scaling_enabled").get<bool>())
    Out << "***WARNING*** CPU scaling is enabled, the benchmark "
           "real time measurements may be noisy and will incur extra "
           "overhead.\n";
  if (context.at("library_build_type") == "debug")
  Out << "***WARNING*** Library was built as DEBUG. Timings may be "
         "affected.\n";
}

double BenchmarkReporter::Run::GetAdjustedRealTime() const {
  double new_time = real_accumulated_time * GetTimeUnitMultiplier(time_unit);
  if (iterations != 0) new_time /= static_cast<double>(iterations);
  return new_time;
}

double BenchmarkReporter::Run::GetAdjustedCPUTime() const {
  double new_time = cpu_accumulated_time * GetTimeUnitMultiplier(time_unit);
  if (iterations != 0) new_time /= static_cast<double>(iterations);
  return new_time;
}

}  // end namespace benchmark
