#ifndef BENCHMARK_UTILITY_H_
#define BENCHMARK_UTILITY_H_

#include "benchmark/benchmark.h"
#include "internal_macros.h"

#include <cmath>
#include <limits>

namespace benchmark {

static inline bool IsZero(double n) {
  return std::abs(n) < std::numeric_limits<double>::epsilon();
}

}  // namespace benchmark
#endif                  // BENCHMARK_UTILITY_H_
