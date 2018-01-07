
#undef NDEBUG
#include <utility>

#include "benchmark/benchmark.h"
#include "output_test.h"

// ========================================================================= //
// ---------------------- Testing Prologue Output -------------------------- //
// ========================================================================= //

ADD_CASES(TC_ConsoleOut,
          {{"^[-]+$", MR_Next},
           {"^Benchmark %s Time %s CPU %s Iterations$", MR_Next},
           {"^[-]+$", MR_Next}});

static int AddContextCases() {
  using namespace benchmark;
  AddCases(TC_ConsoleErr,
           {
               {"%int[-/]%int[-/]%int %int:%int:%int$", MR_Default},
               {"Run on \\(%int X %float MHz CPU s\\)", MR_Next},
           });
  JSON Context = GetContext();
  JSON CPUInfo = Context.at("cpu_info");
  JSON Caches = CPUInfo.at("caches");
  size_t CachesSize = Caches.size();
  if (CachesSize != 0) {
    AddCases(TC_ConsoleErr, {{"CPU Caches:$", MR_Next}});
  }
  for (size_t I = 0; I < CachesSize; ++I) {
    std::string num_caches_str =
        Caches[I].at("num_sharing") != 0 ? " \\(x%int\\)$" : "$";
    AddCases(
        TC_ConsoleErr,
        {{"L%int (Data|Instruction|Unified) %intK" + num_caches_str, MR_Next}});
  }
  return 0;
}
int dummy_register = AddContextCases();


// ========================================================================= //
// ------------------------ Testing Basic Output --------------------------- //
// ========================================================================= //

void BM_basic(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_basic);

ADD_CASES(TC_ConsoleOut, {{"^BM_basic %console_report$"}});

// ========================================================================= //
// ------------------------ Testing Bytes per Second Output ---------------- //
// ========================================================================= //

void BM_bytes_per_second(benchmark::State& state) {
  for (auto _ : state) {
  }
  state.SetBytesProcessed(1);
}
BENCHMARK(BM_bytes_per_second);

ADD_CASES(TC_ConsoleOut,
          {{"^BM_bytes_per_second %console_report +%float[kM]{0,1}B/s$"}});

// ========================================================================= //
// ------------------------ Testing Items per Second Output ---------------- //
// ========================================================================= //

void BM_items_per_second(benchmark::State& state) {
  for (auto _ : state) {
  }
  state.SetItemsProcessed(1);
}
BENCHMARK(BM_items_per_second);

ADD_CASES(TC_ConsoleOut,
          {{"^BM_items_per_second %console_report +%float[kM]{0,1} items/s$"}});

// ========================================================================= //
// ------------------------ Testing Label Output --------------------------- //
// ========================================================================= //

void BM_label(benchmark::State& state) {
  for (auto _ : state) {
  }
  state.SetLabel("some label");
}
BENCHMARK(BM_label);

ADD_CASES(TC_ConsoleOut, {{"^BM_label %console_report some label$"}});

// ========================================================================= //
// ------------------------ Testing Error Output --------------------------- //
// ========================================================================= //

void BM_error(benchmark::State& state) {
  state.SkipWithError("message");
  for (auto _ : state) {
  }
}
BENCHMARK(BM_error);
ADD_CASES(TC_ConsoleOut, {{"^BM_error[ ]+ERROR OCCURRED: 'message'$"}});

// ========================================================================= //
// ------------------------ Testing No Arg Name Output -----------------------
// //
// ========================================================================= //

void BM_no_arg_name(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_no_arg_name)->Arg(3);
ADD_CASES(TC_ConsoleOut, {{"^BM_no_arg_name/3 %console_report$"}});

// ========================================================================= //
// ------------------------ Testing Arg Name Output ----------------------- //
// ========================================================================= //

void BM_arg_name(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_arg_name)->ArgName("first")->Arg(3);
ADD_CASES(TC_ConsoleOut, {{"^BM_arg_name/first:3 %console_report$"}});

// ========================================================================= //
// ------------------------ Testing Arg Names Output ----------------------- //
// ========================================================================= //

void BM_arg_names(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_arg_names)->Args({2, 5, 4})->ArgNames({"first", "", "third"});
ADD_CASES(TC_ConsoleOut,
          {{"^BM_arg_names/first:2/5/third:4 %console_report$"}});

// ========================================================================= //
// ----------------------- Testing Complexity Output ----------------------- //
// ========================================================================= //

void BM_Complexity_O1(benchmark::State& state) {
  for (auto _ : state) {
  }
  state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_Complexity_O1)->Range(1, 1 << 18)->Complexity(benchmark::o1);
SET_SUBSTITUTIONS({{"%bigOStr", "[ ]* %float \\([0-9]+\\)"},
                   {"%RMS", "[ ]*[0-9]+ %"}});
ADD_CASES(TC_ConsoleOut, {{"^BM_Complexity_O1_BigO %bigOStr %bigOStr[ ]*$"},
                          {"^BM_Complexity_O1_RMS %RMS %RMS[ ]*$"}});

// ========================================================================= //
// ----------------------- Testing Aggregate Output ------------------------ //
// ========================================================================= //

// Test that non-aggregate data is printed by default
void BM_Repeat(benchmark::State& state) {
  for (auto _ : state) {
  }
}
// need two repetitions min to be able to output any aggregate output
BENCHMARK(BM_Repeat)->Repetitions(2);
ADD_CASES(TC_ConsoleOut, {{"^BM_Repeat/repeats:2 %console_report$"},
                          {"^BM_Repeat/repeats:2 %console_report$"},
                          {"^BM_Repeat/repeats:2_mean %console_report$"},
                          {"^BM_Repeat/repeats:2_median %console_report$"},
                          {"^BM_Repeat/repeats:2_stddev %console_report$"}});

// but for two repetitions, mean and median is the same, so let's repeat..
BENCHMARK(BM_Repeat)->Repetitions(3);
ADD_CASES(TC_ConsoleOut, {{"^BM_Repeat/repeats:3 %console_report$"},
                          {"^BM_Repeat/repeats:3 %console_report$"},
                          {"^BM_Repeat/repeats:3 %console_report$"},
                          {"^BM_Repeat/repeats:3_mean %console_report$"},
                          {"^BM_Repeat/repeats:3_median %console_report$"},
                          {"^BM_Repeat/repeats:3_stddev %console_report$"}});

// median differs between even/odd number of repetitions, so just to be sure
BENCHMARK(BM_Repeat)->Repetitions(4);
ADD_CASES(TC_ConsoleOut, {{"^BM_Repeat/repeats:4 %console_report$"},
                          {"^BM_Repeat/repeats:4 %console_report$"},
                          {"^BM_Repeat/repeats:4 %console_report$"},
                          {"^BM_Repeat/repeats:4 %console_report$"},
                          {"^BM_Repeat/repeats:4_mean %console_report$"},
                          {"^BM_Repeat/repeats:4_median %console_report$"},
                          {"^BM_Repeat/repeats:4_stddev %console_report$"}});

// Test that a non-repeated test still prints non-aggregate results even when
// only-aggregate reports have been requested
void BM_RepeatOnce(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_RepeatOnce)->Repetitions(1)->ReportAggregatesOnly();
ADD_CASES(TC_ConsoleOut, {{"^BM_RepeatOnce/repeats:1 %console_report$"}});

// Test that non-aggregate data is not reported
void BM_SummaryRepeat(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_SummaryRepeat)->Repetitions(3)->ReportAggregatesOnly();
ADD_CASES(TC_ConsoleOut,
          {{".*BM_SummaryRepeat/repeats:3 ", MR_Not},
           {"^BM_SummaryRepeat/repeats:3_mean %console_report$"},
           {"^BM_SummaryRepeat/repeats:3_median %console_report$"},
           {"^BM_SummaryRepeat/repeats:3_stddev %console_report$"}});

void BM_RepeatTimeUnit(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_RepeatTimeUnit)
    ->Repetitions(3)
    ->ReportAggregatesOnly()
    ->Unit(benchmark::kMicrosecond);
ADD_CASES(TC_ConsoleOut,
          {{".*BM_RepeatTimeUnit/repeats:3 ", MR_Not},
           {"^BM_RepeatTimeUnit/repeats:3_mean %console_us_report$"},
           {"^BM_RepeatTimeUnit/repeats:3_median %console_us_report$"},
           {"^BM_RepeatTimeUnit/repeats:3_stddev %console_us_report$"}});

// ========================================================================= //
// -------------------- Testing user-provided statistics ------------------- //
// ========================================================================= //

const auto UserStatistics = [](const std::vector<double>& v) {
  return v.back();
};
void BM_UserStats(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_UserStats)
    ->Repetitions(3)
    ->ComputeStatistics("", UserStatistics);
// check that user-provided stats is calculated, and is after the default-ones
// empty string as name is intentional, it would sort before anything else
ADD_CASES(TC_ConsoleOut, {{"^BM_UserStats/repeats:3 %console_report$"},
                          {"^BM_UserStats/repeats:3 %console_report$"},
                          {"^BM_UserStats/repeats:3 %console_report$"},
                          {"^BM_UserStats/repeats:3_mean %console_report$"},
                          {"^BM_UserStats/repeats:3_median %console_report$"},
                          {"^BM_UserStats/repeats:3_stddev %console_report$"},
                          {"^BM_UserStats/repeats:3_ %console_report$"}});

// ========================================================================= //
// --------------------------- TEST CASES END ------------------------------ //
// ========================================================================= //

int main(int argc, char* argv[]) { RunOutputTests(argc, argv); }
