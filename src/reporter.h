#ifndef BENCHMARK_REPORTER_H_
#define BENCHMARK_REPORTER_H_

#include "benchmark/benchmark.h"

namespace benchmark {

void PrintBasicContext(std::ostream* out, json const& context);

}  // namespace benchmark

#endif  // BENCHMARK_REPORTER_H_
