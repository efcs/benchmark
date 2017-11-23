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
#include "sysinfo.h"
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
                                          JSON const &context) {
  CHECK(out) << "cannot be null";
  auto &Out = *out;

  Out << LocalDateTimeString() << "\n";

  JSON info = context.at("cpu_info");
  Out << "Run on (" << info.get_at<int>("num_cpus") << " X "
      << (info.get_at<double>("cycles_per_second") / 1000000.0) << " MHz CPU "
      << ((info.get_at<int>("num_cpus") > 1) ? "s" : "") << ")\n";
  std::vector<CPUInfo::CacheInfo> caches = info.at("caches");
  if (caches.size() != 0) {
    Out << "CPU Caches:\n";
    for (auto &CInfo : caches) {
      Out << "  L" << CInfo.level << " " << CInfo.type << " "
          << (CInfo.size / 1000) << "K\n";
    }
  }

  if (info.get_at<bool>("scaling_enabled")) {
    Out << "***WARNING*** CPU scaling is enabled, the benchmark "
           "real time measurements may be noisy and will incur extra "
           "overhead.\n";
  }

#ifndef NDEBUG
  Out << "***WARNING*** Library was built as DEBUG. Timings may be "
         "affected.\n";
#endif
}



}  // end namespace benchmark
