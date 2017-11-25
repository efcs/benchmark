
#undef NDEBUG
#include <cassert>
#include <vector>

#include "../src/check.h"  // NOTE: check.h is for internal use only!
#include "benchmark/benchmark.h"

namespace {

using namespace benchmark;

struct ReporterCallback {
  void operator()(CallbackKind K, JSON& J) {
    if (K == CK_Report) all_runs_.push_back(J);
  }
  std::vector<benchmark::JSON> all_runs_;
};

struct TestCase {
  std::string name;
  const char* label;
  // Note: not explicit as we rely on it being converted through ADD_CASES.
  TestCase(const char* xname, const char* xlabel)
      : name(xname), label(xlabel) {}
  TestCase(const char* xname) : TestCase(xname, nullptr) {}

  void CheckRun(internal::BenchmarkInstance const& I) const {
    CHECK(name == I.name) << "expected " << name << " got " << I.name;
    if (label) {
      // CHECK(json.count("label") == 1);
      // std::string L = json.at("label");
      // CHECK(L == label) << "expected " << label << " got " << L;
    }
  }
};

std::vector<TestCase> ExpectedResults;

int AddCases(std::initializer_list<TestCase> const& v) {
  for (auto N : v) {
    ExpectedResults.push_back(N);
  }
  return 0;
}

#define CONCAT(x, y) CONCAT2(x, y)
#define CONCAT2(x, y) x##y
#define ADD_CASES(...) int CONCAT(dummy, __LINE__) = AddCases({__VA_ARGS__})

}  // end namespace

typedef benchmark::internal::Benchmark* ReturnVal;

//----------------------------------------------------------------------------//
// Test RegisterBenchmark with no additional arguments
//----------------------------------------------------------------------------//
void BM_function(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_function);
ReturnVal dummy = benchmark::RegisterBenchmark(
    "BM_function_manual_registration", BM_function);
ADD_CASES({"BM_function"}, {"BM_function_manual_registration"});

//----------------------------------------------------------------------------//
// Test RegisterBenchmark with additional arguments
// Note: GCC <= 4.8 do not support this form of RegisterBenchmark because they
//       reject the variadic pack expansion of lambda captures.
//----------------------------------------------------------------------------//
#ifndef BENCHMARK_HAS_NO_VARIADIC_REGISTER_BENCHMARK

void BM_extra_args(benchmark::State& st, const char* label) {
  for (auto _ : st) {
  }
  st.SetLabel(label);
}
int RegisterFromFunction() {
  std::pair<const char*, const char*> cases[] = {
      {"test1", "One"}, {"test2", "Two"}, {"test3", "Three"}};
  for (auto const& c : cases)
    benchmark::RegisterBenchmark(c.first, &BM_extra_args, c.second);
  return 0;
}
int dummy2 = RegisterFromFunction();
ADD_CASES({"test1", "One"}, {"test2", "Two"}, {"test3", "Three"});

#endif  // BENCHMARK_HAS_NO_VARIADIC_REGISTER_BENCHMARK

//----------------------------------------------------------------------------//
// Test RegisterBenchmark with different callable types
//----------------------------------------------------------------------------//

struct CustomFixture {
  void operator()(benchmark::State& st) {
    for (auto _ : st) {
    }
  }
};

void TestRegistrationAtRuntime() {
#ifdef BENCHMARK_HAS_CXX11
  {
    CustomFixture fx;
    benchmark::RegisterBenchmark("custom_fixture", fx);
    AddCases({"custom_fixture"});
  }
#endif
#ifndef BENCHMARK_HAS_NO_VARIADIC_REGISTER_BENCHMARK
  {
    const char* x = "42";
    auto capturing_lam = [=](benchmark::State& st) {
      for (auto _ : st) {
      }
      st.SetLabel(x);
    };
    benchmark::RegisterBenchmark("lambda_benchmark", capturing_lam);
    AddCases({{"lambda_benchmark", "42"}});
  }
#endif
}

// Test that all benchmarks, registered at either during static init or runtime,
// are run and the results are passed to the reported.
void RunTestOne() {
  TestRegistrationAtRuntime();

  BenchmarkInstanceList All = FindSpecifiedBenchmarks();

  auto EB = ExpectedResults.begin();

  for (auto I : All) {
    assert(EB != ExpectedResults.end());
    EB->CheckRun(I);
    ++EB;
  }
  assert(EB == ExpectedResults.end());
}

// Test that ClearRegisteredBenchmarks() clears all previously registered
// benchmarks.
// Also test that new benchmarks can be registered and ran afterwards.
void RunTestTwo() {
  assert(ExpectedResults.size() != 0 &&
         "must have at least one registered benchmark");
  ExpectedResults.clear();
  benchmark::ClearRegisteredBenchmarks();
  BenchmarkInstanceList AllRuns = FindSpecifiedBenchmarks();
  assert(AllRuns.size() == 0);
  TestRegistrationAtRuntime();
  BenchmarkInstanceList found = FindSpecifiedBenchmarks();
  assert(found.size() == ExpectedResults.size());

  auto EB = ExpectedResults.begin();

  for (internal::BenchmarkInstance I : found) {
    assert(EB != ExpectedResults.end());
    EB->CheckRun(I);
    ++EB;
  }
  assert(EB == ExpectedResults.end());
}

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);

  RunTestOne();
  RunTestTwo();
}
