#ifndef BENCHMARK_API_INTERNAL_H
#define BENCHMARK_API_INTERNAL_H

#include <cmath>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>
#include "benchmark/benchmark.h"
#include "reporter.h"

namespace benchmark {
namespace internal {

// Information kept per benchmark we may want to run

bool FindBenchmarksInternal(const std::string& re,
                            std::vector<BenchmarkInstance>* benchmarks,
                            std::ostream* Err);

ConsoleReporter::OutputOptions GetOutputOptions(bool force_no_color = false);

}  // end namespace internal
}  // end namespace benchmark

#endif  // BENCHMARK_API_INTERNAL_H
