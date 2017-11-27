#ifdef NDEBUG
#undef NDEBUG
#endif
#include <chrono>
#include <thread>
#include "../src/check.h"
#include "benchmark/benchmark.h"

using namespace benchmark;

static void BM_ExampleWithJSON(State &st) {
  JSON input = st.GetInputData();
  Counter continue_counter = input.value("previous_counter", Counter(0.0));
  for (auto _ : st) {
    // Do something
  }
  st["my_output"] = {{"foo", input["a"]}, {"bar", input["b"]}};
  st["my_counter"] = continue_counter;
  st["my_rate"] = Counter(st.real_time_used(), Counter::kIsRate);
}
BENCHMARK(BM_ExampleWithJSON)
    ->WithData({{"name", "example1"},
                {"a", 42},
                {"b", 101},
                {"previous_counter", Counter(5.24)}});

void test_json_data() {
  auto BenchList = FindBenchmarks("BM_ExampleWithJSON/input:example1");
  CHECK(BenchList.size() == 1);
  JSON Res = RunBenchmark(BenchList[0], /*Report*/ true);

  {
    CHECK(!Res.is_array());
    JSON Run = Res.at("runs")[0];
    CHECK_EQ(Run.count("user_data"), 1);
    JSON Data = Run.at("user_data");
    CHECK(Data.is_object());
    CHECK_EQ(Data.count("my_output"), 1);
    JSON MyOutput = Data.at("my_output");
    CHECK_EQ(MyOutput.get_at<int>("foo"), 42);
    CHECK_EQ(MyOutput.get_at<int>("bar"), 101);
    Counter C = Data.at("my_counter");
    CHECK_FLOAT_EQ(C.value, 5.24, 0.01);
    Counter R = Data.at("my_rate");
    CHECK_FLOAT_EQ(R.value, 1.0, 0.01);
  }
}

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
  test_json_data();
}
