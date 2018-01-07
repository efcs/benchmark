#ifndef BENCHMARK_UTILITY_H_
#define BENCHMARK_UTILITY_H_

#include "benchmark/benchmark.h"
#include "internal_macros.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

#include "check.h"

namespace benchmark {

static inline bool IsZero(double n) {
  return std::abs(n) < std::numeric_limits<double>::epsilon();
}

inline void PrintImp(std::ostream& out) { out << std::endl; }

template <class First, class... Rest>
void PrintImp(std::ostream& out, First&& f, Rest&&... rest) {
  out << std::forward<First>(f);
  PrintImp(out, std::forward<Rest>(rest)...);
}

template <class... Args>
BENCHMARK_NORETURN void PrintErrorAndDie(Args&&... args) {
  PrintImp(std::cerr, std::forward<Args>(args)...);
  std::exit(EXIT_FAILURE);
}

inline json GetRunOrMeanStat(json const& R) {
  if (R.at("runs").size() == 1) return R.at("runs")[0];
  std::string MeanName = R.at("name");
  MeanName += "_mean";
  CHECK(R.at("stats").size() != 0);
  for (auto It = R.at("stats").begin(); It != R.at("stats").end(); ++It) {
    json Val = It.value();
    std::string Name = Val.at("name");
    if (Name == MeanName) return Val;
  }
  BENCHMARK_UNREACHABLE();
}

}  // namespace benchmark
#endif                  // BENCHMARK_UTILITY_H_
