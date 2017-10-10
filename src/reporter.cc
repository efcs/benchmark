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
#include <bitset>

#include <cstdlib>

#include <iostream>
#include <tuple>
#include <vector>

#include "check.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

namespace benchmark {

BenchmarkReporter::BenchmarkReporter()
    : output_stream_(&std::cout), error_stream_(&std::cerr) {}

BenchmarkReporter::~BenchmarkReporter() {}

void BenchmarkReporter::PrintBasicContext(std::ostream *out,
                                          Context const &context) {
  CHECK(out) << "cannot be null";
  auto &Out = *out;

  Out << "Run on (" << context.num_cpus << " X " << context.mhz_per_cpu
      << " MHz CPU " << ((context.num_cpus > 1) ? "s" : "") << ")\n";

  Out << LocalDateTimeString() << "\n";

  if (context.cpu_scaling_enabled) {
    Out << "***WARNING*** CPU scaling is enabled, the benchmark "
           "real time measurements may be noisy and will incur extra "
           "overhead.\n";
  }

#ifndef NDEBUG
  Out << "***WARNING*** Library was built as DEBUG. Timings may be "
         "affected.\n";
#endif
}

template<typename T>
void show_binrep(const T& a)
{
    const char* beg = reinterpret_cast<const char*>(&a);
    const char* end = beg + sizeof(a);
    while(beg != end)
        std::cout << std::bitset<CHAR_BIT>(*beg++) << ' ';
    std::cout << '\n';
}

static double RemoveNegZero(double D) {
  using Lim = std::numeric_limits<double>;
  static_assert(Lim::has_denorm, "");
  if (std::signbit(D) == 1) {
    assert(std::fpclassify(D) != FP_ZERO);
    assert(std::fpclassify(D) != FP_NAN);
    assert(std::fpclassify(D) != FP_SUBNORMAL);
#if 0
    std::cout.precision(Lim::max_digits10);
    std::cout << std::hex;
    std::cout.binary
    std::cout.write(reinterpret_cast<const char*>(&D), sizeof(D));
    std::cout << std::endl;
#endif
    show_binrep(D);
    assert(std::isnormal(D));
    assert(!std::isunordered(0.0, D));
    assert(D > -0.5);
    assert(D > -0.0001);
    assert(D >= (-Lim::denorm_min()));
    assert(D > (-Lim::denorm_min()));
    assert(false);
    return 0.0;
  }
  return D;
}

double BenchmarkReporter::Run::GetAdjustedRealTime() const {
  double new_time = real_accumulated_time * GetTimeUnitMultiplier(time_unit);
  if (iterations != 0) new_time /= static_cast<double>(iterations);
  return RemoveNegZero(new_time);
}

double BenchmarkReporter::Run::GetAdjustedCPUTime() const {
  double new_time = cpu_accumulated_time * GetTimeUnitMultiplier(time_unit);
  if (iterations != 0) new_time /= static_cast<double>(iterations);
  return RemoveNegZero(new_time);
}

}  // end namespace benchmark
