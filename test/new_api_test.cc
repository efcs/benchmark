#include <chrono>
#include <thread>
#include "benchmark/benchmark.h"

using namespace benchmark;

int main(int argc, char **argv) {
  Initialize(&argc, argv);
  auto BMF = [](State &st) {
    for (auto _ : st) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  auto *B1 = RegisterBenchmark("bench1", BMF);
  auto *B2 = RegisterBenchmark("bench2", BMF);
  JSON res1 = RunBenchmark(B1);
  JSON res2 = RunBenchmark(B2);
  JSON cmps = CompareResults(res1, res2);
  ReportResults(cmps);
}
