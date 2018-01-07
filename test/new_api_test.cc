#ifdef NDEBUG
#undef NDEBUG
#endif
#include <chrono>
#include <iostream>
#include <thread>
#include "../src/check.h"
#include "benchmark/benchmark.h"

using namespace benchmark;

static void BM_ExampleWithJSON(State &st) {
  json input = st.GetInput();
  Counter continue_counter = input.value("previous_counter", Counter(0.0));
  for (auto _ : st) {
    // Do something
  }
  st["my_output"] = {{"foo", input["a"]}, {"bar", input["b"]}};
  st["my_counter"] = continue_counter;
  st["my_rate"] = Counter(st.real_time_used(), Counter::kIsRate);
}
BENCHMARK(BM_ExampleWithJSON)
    ->WithInput({{"name", "example1"},
                {"a", 42},
                {"b", 101},
                {"previous_counter", Counter(5.24)}});

void test_json_data() {
  auto BenchList = FindBenchmarks("BM_ExampleWithJSON/input:example1");
  CHECK(BenchList.size() == 1);
  json Res = RunBenchmark(BenchList[0], /*Report*/ true);

  {
    CHECK(!Res.is_array());
    json Run = Res.at("runs")[0];
    CHECK_EQ(Run.count("user_data"), 1);
    json Data = Run.at("user_data");
    CHECK(Data.is_object());
    CHECK_EQ(Data.count("my_output"), 1);
    json MyOutput = Data.at("my_output");
    CHECK_EQ(MyOutput.at("foo").get<int>(), 42);
    CHECK_EQ(MyOutput.at("bar").get<int>(), 101);
    Counter C = Data.at("my_counter");
    CHECK_FLOAT_EQ(C.value, 5.24, 0.01);
    Counter R = Data.at("my_rate");
    CHECK_FLOAT_EQ(R.value, 1.0, 0.01);
  }
}

void test_compare() {
  auto BM_Slow = [](State &st) {
    for (auto _ : st) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  auto BM_Fast = [](State &st) {
    for (auto _ : st) {
    }
  };
  auto *B1 = RegisterBenchmark("bench_slow", BM_Slow);
  auto *B2 = RegisterBenchmark("bench_fast", BM_Fast);
  auto R1 = RunBenchmark(B1);
  auto R2 = RunBenchmark(B2);
  GetGlobalReporter().Report(CompareResults(R1, R2));
  GetGlobalReporter().Report(CompareResults(R2, R1));
}

int main(int argc, char **argv) {
  Initialize(&argc, argv);
  test_json_data();
  test_compare();
}
