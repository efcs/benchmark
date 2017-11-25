#ifndef BENCHMARK_COMMANDLINE_H_
#define BENCHMARK_COMMANDLINE_H_

#include <cstdint>
#include <string>
#include "benchmark/benchmark.h"

// Macro for referencing flags.
#define FLAG_NAME(name) FLAGS_##name

// Macros for declaring flags.
#define DECLARE_bool(name) extern bool FLAG_NAME(name)
#define DECLARE_int32(name) extern int32_t FLAG_NAME(name)
#define DECLARE_int64(name) extern int64_t FLAG_NAME(name)
#define DECLARE_double(name) extern double FLAG_NAME(name)
#define DECLARE_string(name) extern std::string FLAG_NAME(name)

// Macros for defining flags.
#define DEFINE_bool(name, default_val, doc) bool FLAG_NAME(name) = (default_val)
#define DEFINE_int32(name, default_val, doc) \
  int32_t FLAG_NAME(name) = (default_val)
#define DEFINE_int64(name, default_val, doc) \
  int64_t FLAG_NAME(name) = (default_val)
#define DEFINE_double(name, default_val, doc) \
  double FLAG_NAME(name) = (default_val)
#define DEFINE_string(name, default_val, doc) \
  std::string FLAG_NAME(name) = (default_val)

namespace benchmark {

#ifdef BUILDING_BENCHMARK_COMMANDLINE_CC
#define FLAG(Type, Name, Default, String) \
  BENCHMARK_PRIVATE_CONCAT(DEFINE, _, Type)(Name, Default, String)
#else
#define FLAG(Type, Name, Default, String) \
  BENCHMARK_PRIVATE_CONCAT(DECLARE, _, Type)(Name)
#endif

FLAG(bool, benchmark_list_tests, false,
     "Print a list of benchmarks. This option overrides all other "
     "options.");

FLAG(string, benchmark_filter, ".",
     "A regular expression that specifies the set of benchmarks "
     "to execute.  If this flag is empty, no benchmarks are run.  "
     "If this flag is the string \"all\", all benchmarks linked "
     "into the process are run.");

FLAG(double, benchmark_min_time, 0.5,
     "Minimum number of seconds we should run benchmark before "
     "results are considered significant.  For cpu-time based "
     "tests, this is the lower bound on the total cpu time "
     "used by all threads that make up the test.  For real-time "
     "based tests, this is the lower bound on the elapsed time "
     "of the benchmark execution, regardless of number of "
     "threads.");

FLAG(int32, benchmark_repetitions, 1,
     "The number of runs of each benchmark. If greater than 1, the "
     "mean and standard deviation of the runs will be reported.");

FLAG(bool, benchmark_report_aggregates_only, false,
     "Report the result of each benchmark repetitions. When 'true' is "
     "specified only the mean, standard deviation, and other statistics "
     "are reported for repeated benchmarks.");

FLAG(string, benchmark_out, "", "The file to write additonal output to");

FLAG(string, benchmark_color, "auto",
     "Whether to use colors in the output.  Valid values: "
     "'true'/'yes'/1, 'false'/'no'/0, and 'auto'. 'auto' means to use "
     "colors if the output is being sent to a terminal and the TERM "
     "environment variable is set to a terminal type that supports "
     "colors.");

FLAG(bool, benchmark_counters_tabular, false,
     "Whether to use tabular format when printing user counters to "
     "the console.  Valid values: 'true'/'yes'/1, 'false'/'no'/0."
     "Defaults to false.");

FLAG(int32, v, 0, "The level of verbose logging to output");

#undef FLAG

namespace internal {

// Parses 'str' for a 32-bit signed integer.  If successful, writes the result
// to *value and returns true; otherwise leaves *value unchanged and returns
// false.
bool ParseInt32(const std::string& src_text, const char* str, int32_t* value);

// Parses a bool/Int32/string from the environment variable
// corresponding to the given Google Test flag.
bool BoolFromEnv(const char* flag, bool default_val);
int32_t Int32FromEnv(const char* flag, int32_t default_val);
double DoubleFromEnv(const char* flag, double default_val);
const char* StringFromEnv(const char* flag, const char* default_val);

// Parses a string for a bool flag, in the form of either
// "--flag=value" or "--flag".
//
// In the former case, the value is taken as true if it passes IsTruthyValue().
//
// In the latter case, the value is taken as true.
//
// On success, stores the value of the flag in *value, and returns
// true.  On failure, returns false without changing *value.
bool ParseBoolFlag(const char* str, const char* flag, bool* value);

// Parses a string for an Int32 flag, in the form of
// "--flag=value".
//
// On success, stores the value of the flag in *value, and returns
// true.  On failure, returns false without changing *value.
bool ParseInt32Flag(const char* str, const char* flag, int32_t* value);

// Parses a string for a Double flag, in the form of
// "--flag=value".
//
// On success, stores the value of the flag in *value, and returns
// true.  On failure, returns false without changing *value.
bool ParseDoubleFlag(const char* str, const char* flag, double* value);

// Parses a string for a string flag, in the form of
// "--flag=value".
//
// On success, stores the value of the flag in *value, and returns
// true.  On failure, returns false without changing *value.
bool ParseStringFlag(const char* str, const char* flag, std::string* value);

// Returns true if the string matches the flag.
bool IsFlag(const char* str, const char* flag);

// Returns true unless value starts with one of: '0', 'f', 'F', 'n' or 'N', or
// some non-alphanumeric character. As a special case, also returns true if
// value is the empty string.
bool IsTruthyFlagValue(const std::string& value);

}  // end namespace internal
}  // end namespace benchmark

#endif  // BENCHMARK_COMMANDLINE_H_
