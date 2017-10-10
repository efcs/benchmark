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
#include <cfloat>
#include <cfenv>

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

void show_fe_exceptions(void)
{
    auto printf = [](const char* S) {
      std::cout << S;
    };
    printf("exceptions raised:");
    if(fetestexcept(FE_DIVBYZERO)) printf(" FE_DIVBYZERO");
    if(fetestexcept(FE_INEXACT))   printf(" FE_INEXACT");
    if(fetestexcept(FE_INVALID))   printf(" FE_INVALID");
    if(fetestexcept(FE_OVERFLOW))  printf(" FE_OVERFLOW");
    if(fetestexcept(FE_UNDERFLOW)) printf(" FE_UNDERFLOW");
    feclearexcept(FE_ALL_EXCEPT);
    std::cout << std::endl;
}

template<typename T>
void show_binrep(const T& a)
{
    const unsigned char* beg = reinterpret_cast<const unsigned char*>(&a);
    const unsigned char* end = beg + sizeof(a);
    while (end != beg)
      std::cout << std::bitset<CHAR_BIT>(*(--end)) << ' ';

    //while(beg != end)
    //    std::cout << std::bitset<CHAR_BIT>(*beg++) << ' ';
    std::cout << std::endl;
}

static double RemoveNegZero(double D) {
  using Lim = std::numeric_limits<double>;
  static_assert(Lim::has_denorm, "");
  static_assert(Lim::is_iec559, "");
  if (std::signbit(D) == 1) {

    auto volatile VD = D;
    VD = std::fabs(VD);
    assert(std::signbit(VD) == 0);
    auto Name = [&](const char* N) {
      std::cout << N << std::endl;
    };
    Name("D");
    show_binrep(D);
    Name("round_error");
    show_binrep(Lim::round_error());
    Name("L");
    show_binrep(Lim::denorm_min());
    Name("VD");
    show_binrep((double)VD);
    double MIN = __DBL_MIN__;
    Name("Min");
    show_binrep(MIN);
    assert(std::isnormal(D));
    assert(!std::isunordered(0.0, D));

    show_fe_exceptions();
    assert(D <= 0.0);
    show_fe_exceptions();

#ifdef __DBL_TRUE_MIN__
    double TM = __DBL_TRUE_MIN__;
    show_binrep(TM);
#endif
    assert(VD <= Lim::round_error());
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
