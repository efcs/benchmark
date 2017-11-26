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
  ((void)B1);
  ((void)B2);
  //ReportResults(
  //    CompareResults(RunBenchmark(B1), RunBenchmark(B2))
  //);
}
