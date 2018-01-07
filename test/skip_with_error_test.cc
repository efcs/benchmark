
#undef NDEBUG
#include <cassert>
#include <vector>

#include "../src/check.h"  // NOTE: check.h is for internal use only!
#include "benchmark/benchmark.h"

namespace {

struct TestCase {
  std::string name;
  bool error_occurred;
  std::string error_message;

  void CheckRun(benchmark::json const& json_root) const {
    CHECK(json_root.at("stats").size() == 0);
    CHECK(json_root.at("runs").size() == 1);
    benchmark::json json = json_root.at("runs")[0];
    std::string BName = json.at("name");

    CHECK(name == BName) << "expected " << name << " got " << BName;
    std::string Kind = json.at("kind");
    CHECK((Kind == "error") == error_occurred);
    if (Kind == "error") {
      std::string Msg = json.at("error_message");
      CHECK(error_message == Msg);
    } else {
      CHECK(json.count("error_message") == 0);
    }
  }
};

std::vector<TestCase> ExpectedResults;

int AddCases(const char* base_name, std::initializer_list<TestCase> const& v) {
  for (auto TC : v) {
    TC.name = base_name + TC.name;
    ExpectedResults.push_back(std::move(TC));
  }
  return 0;
}

#define CONCAT(x, y) CONCAT2(x, y)
#define CONCAT2(x, y) x##y
#define ADD_CASES(...) int CONCAT(dummy, __LINE__) = AddCases(__VA_ARGS__)

}  // end namespace

void BM_error_before_running(benchmark::State& state) {
  state.SkipWithError("error message");
  while (state.KeepRunning()) {
    assert(false);
  }
}
BENCHMARK(BM_error_before_running);
ADD_CASES("BM_error_before_running", {{"", true, "error message"}});

void BM_error_before_running_range_for(benchmark::State& state) {
  state.SkipWithError("error message");
  for (auto _ : state) {
    assert(false);
  }
}
BENCHMARK(BM_error_before_running_range_for);
ADD_CASES("BM_error_before_running_range_for", {{"", true, "error message"}});

void BM_error_during_running(benchmark::State& state) {
  int first_iter = true;
  while (state.KeepRunning()) {
    if (state.range(0) == 1 && state.thread_index <= (state.threads / 2)) {
      assert(first_iter);
      first_iter = false;
      state.SkipWithError("error message");
    } else {
      state.PauseTiming();
      state.ResumeTiming();
    }
  }
}
BENCHMARK(BM_error_during_running)->Arg(1)->Arg(2)->ThreadRange(1, 8);
ADD_CASES("BM_error_during_running", {{"/1/threads:1", true, "error message"},
                                      {"/1/threads:2", true, "error message"},
                                      {"/1/threads:4", true, "error message"},
                                      {"/1/threads:8", true, "error message"},
                                      {"/2/threads:1", false, ""},
                                      {"/2/threads:2", false, ""},
                                      {"/2/threads:4", false, ""},
                                      {"/2/threads:8", false, ""}});

void BM_error_during_running_ranged_for(benchmark::State& state) {
  assert(state.max_iterations > 3 && "test requires at least a few iterations");
  int first_iter = true;
  // NOTE: Users should not write the for loop explicitly.
  for (auto It = state.begin(), End = state.end(); It != End; ++It) {
    if (state.range(0) == 1) {
      assert(first_iter);
      first_iter = false;
      state.SkipWithError("error message");
      // Test the unfortunate but documented behavior that the ranged-for loop
      // doesn't automatically terminate when SkipWithError is set.
      assert(++It != End);
      break; // Required behavior
    }
  }
}
BENCHMARK(BM_error_during_running_ranged_for)->Arg(1)->Arg(2)->Iterations(5);
ADD_CASES("BM_error_during_running_ranged_for",
          {{"/1/iterations:5", true, "error message"},
           {"/2/iterations:5", false, ""}});



void BM_error_after_running(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(state.iterations());
  }
  if (state.thread_index <= (state.threads / 2))
    state.SkipWithError("error message");
}
BENCHMARK(BM_error_after_running)->ThreadRange(1, 8);
ADD_CASES("BM_error_after_running", {{"/threads:1", true, "error message"},
                                     {"/threads:2", true, "error message"},
                                     {"/threads:4", true, "error message"},
                                     {"/threads:8", true, "error message"}});

void BM_error_while_paused(benchmark::State& state) {
  bool first_iter = true;
  while (state.KeepRunning()) {
    if (state.range(0) == 1 && state.thread_index <= (state.threads / 2)) {
      assert(first_iter);
      first_iter = false;
      state.PauseTiming();
      state.SkipWithError("error message");
    } else {
      state.PauseTiming();
      state.ResumeTiming();
    }
  }
}
BENCHMARK(BM_error_while_paused)->Arg(1)->Arg(2)->ThreadRange(1, 8);
ADD_CASES("BM_error_while_paused", {{"/1/threads:1", true, "error message"},
                                    {"/1/threads:2", true, "error message"},
                                    {"/1/threads:4", true, "error message"},
                                    {"/1/threads:8", true, "error message"},
                                    {"/2/threads:1", false, ""},
                                    {"/2/threads:2", false, ""},
                                    {"/2/threads:4", false, ""},
                                    {"/2/threads:8", false, ""}});

int main(int argc, char* argv[]) {
  using namespace benchmark;
  benchmark::Initialize(&argc, argv);
  benchmark::GetGlobalReporter().EnableColor(false);
  json BMList = RunBenchmarks(FindSpecifiedBenchmarks());

  auto EB = ExpectedResults.begin();

  for (json::iterator It = BMList.begin(); It != BMList.end(); ++It) {
    assert(EB != ExpectedResults.end());
    EB->CheckRun(*It);
    ++EB;
  }
  assert(EB == ExpectedResults.end());

  return 0;
}
