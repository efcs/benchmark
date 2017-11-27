#ifdef NDEBUG
#undef NDEBUG
#endif
#include <chrono>
#include <thread>
#include "../src/check.h"
#include "benchmark/benchmark.h"

using namespace benchmark;

void test_json_data() {
  auto BM = [&](State &st) {
    JSON input = st.GetInputData();
    CHECK(!input.is_null());
    for (auto _ : st) {
    }
    st["my_output"] = {{"foo", input["a"]}, {"bar", input["b"]}};
    st["my_counter"] = Counter(5.24);
    st["my_rate"] = Counter(100, Counter::kIsRate);
  };
  auto B1 = RegisterBenchmark("json_bench1", BM);
  B1->WithData({
      {"a", 42},
      {"b", 101},
  });
  JSON Res = RunBenchmark(B1);
  ReportResults(Res);

  {
    CHECK(Res.is_array());
    CHECK(Res.size() == 1);
    JSON Run = Res[0].at("runs")[0];
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
    // CHECK_FLOAT_LT(R.value, 1.0, 0.001);
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
