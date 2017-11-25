#include "benchmark/benchmark.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <iostream>
#include <limits>
#include <sstream>
#include <string>


static void NoPrefix(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(NoPrefix);

static void BM_Foo(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_Foo);

static void BM_Bar(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_Bar);

static void BM_FooBar(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_FooBar);

static void BM_FooBa(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_FooBa);

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  struct {
    std::string Name;
    std::string Regex;
    size_t Expect;
  } TestCases[] = {{"simple", "Foo", 3},   {"suffix", "BM_.*", 4},
                   {"all", ".*", 5},       {"blank", "", 5},
                   {"none", "monkey", 0},  {"wildcard", ".*Foo.*", 3},
                   {"begin", "^BM_.*", 4}, {"begin2", "^N", 1},
                   {"end", ".*Ba$", 1}};
  for (auto& TC : TestCases) {
    benchmark::ErrorCode EC;
    auto Benches = benchmark::FindBenchmarks(TC.Regex, &EC);
    size_t returned_count = Benches.size();
    if (EC) {
      std::cerr << "ERROR: Failed to initialize regex (" << TC.Regex
                << ") with error: " << EC.message() << std::endl;
      return -1;
    }
    if (returned_count != TC.Expect) {
      std::cerr << "Test Case '" << TC.Name << "' FAILED!\n"
                << "  With Regex: '" << TC.Regex << "'\n"
                << "  Expected Count: " << TC.Expect << "\n"
                << "  Got Count: " << returned_count << std::endl;
      return -1;
    }
  }
}
