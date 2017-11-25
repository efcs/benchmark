#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <cstring>

#include "../src/check.h"  // NOTE: check.h is for internal use only!
#include "../src/re.h"     // NOTE: re.h is for internal use only
#include "output_test.h"

// ========================================================================= //
// ------------------------------ Internals -------------------------------- //
// ========================================================================= //
namespace internal {
namespace {

using TestCaseList = std::vector<TestCase>;

// Use a vector because the order elements are added matters during iteration.
// std::map/unordered_map don't guarantee that.
// For example:
//  SetSubstitutions({{"%HelloWorld", "Hello"}, {"%Hello", "Hi"}});
//     Substitute("%HelloWorld") // Always expands to Hello.
using SubMap = std::vector<std::pair<std::string, std::string>>;

TestCaseList& GetTestCaseList(TestCaseID ID) {
  // Uses function-local statics to ensure initialization occurs
  // before first use.
  static TestCaseList lists[TC_NumID];
  return lists[ID];
}

SubMap& GetSubstitutions() {
  // Don't use 'dec_re' from header because it may not yet be initialized.
  static std::string safe_dec_re = "[0-9]*[.]?[0-9]+([eE][-+][0-9]+)?";
  static SubMap map = {
      {"%float", "[0-9]*[.]?[0-9]+([eE][-+][0-9]+)?"},
      // human-readable float
      {"%hrfloat", "[0-9]*[.]?[0-9]+([eE][-+][0-9]+)?[kMGTPEZYmunpfazy]?"},
      {"%int", "[ ]*[0-9]+"},
      {" %s ", "[ ]+"},
      {"%time", "[ ]*[0-9]{1,5} ns"},
      {"%console_report", "[ ]*[0-9]{1,5} ns [ ]*[0-9]{1,5} ns [ ]*[0-9]+"},
      {"%console_us_report", "[ ]*[0-9] us [ ]*[0-9] us [ ]*[0-9]+"}};
  return map;
}

std::string PerformSubstitutions(std::string source) {
  SubMap const& subs = GetSubstitutions();
  using SizeT = std::string::size_type;
  for (auto const& KV : subs) {
    SizeT pos;
    SizeT next_start = 0;
    while ((pos = source.find(KV.first, next_start)) != std::string::npos) {
      next_start = pos + KV.second.size();
      source.replace(pos, KV.first.size(), KV.second);
    }
  }
  return source;
}

void CheckCase(std::stringstream& remaining_output, TestCase const& TC,
               TestCaseList const& not_checks) {
  std::string first_line;
  bool on_first = true;
  std::string line;
  while (remaining_output.eof() == false) {
    CHECK(remaining_output.good());
    std::getline(remaining_output, line);
    if (on_first) {
      first_line = line;
      on_first = false;
    }
    for (const auto& NC : not_checks) {
      CHECK(!NC.regex->Match(line))
          << "Unexpected match for line \"" << line << "\" for MR_Not regex \""
          << NC.regex_str << "\""
          << "\n    actual regex string \"" << TC.substituted_regex << "\""
          << "\n    started matching near: " << first_line;
    }
    if (TC.regex->Match(line)) return;
    CHECK(TC.match_rule != MR_Next)
        << "Expected line \"" << line << "\" to match regex \"" << TC.regex_str
        << "\""
        << "\n    actual regex string \"" << TC.substituted_regex << "\""
        << "\n    started matching near: " << first_line;
  }
  CHECK(remaining_output.eof() == false)
      << "End of output reached before match for regex \"" << TC.regex_str
      << "\" was found"
      << "\n    actual regex string \"" << TC.substituted_regex << "\""
      << "\n    started matching near: " << first_line;
}

void CheckCases(TestCaseList const& checks, std::stringstream& output) {
  std::vector<TestCase> not_checks;
  for (size_t i = 0; i < checks.size(); ++i) {
    const auto& TC = checks[i];
    if (TC.match_rule == MR_Not) {
      not_checks.push_back(TC);
      continue;
    }
    CheckCase(output, TC, not_checks);
    not_checks.clear();
  }
}

}  // end namespace
}  // end namespace internal

// ========================================================================= //
// -------------------------- Results checking ----------------------------- //
// ========================================================================= //

namespace internal {

// Utility class to manage subscribers for checking benchmark results.
// It works by parsing the CSV output to read the results.
class ResultsChecker {
 public:

  struct PatternAndFn : public TestCase { // reusing TestCase for its regexes
    PatternAndFn(const std::string& rx, ResultsCheckFn fn_)
    : TestCase(rx), fn(fn_) {}
    ResultsCheckFn fn;
  };

  std::vector< PatternAndFn > check_patterns;
  std::vector< Results > results;
  std::vector< std::string > field_names;

  void Add(const std::string& entry_pattern, ResultsCheckFn fn);

  void CheckResults(std::stringstream& output);

 private:
};

// store the static ResultsChecker in a function to prevent initialization
// order problems
ResultsChecker& GetResultsChecker() {
  static ResultsChecker rc;
  return rc;
}

// add a results checker for a benchmark
void ResultsChecker::Add(const std::string& entry_pattern, ResultsCheckFn fn) {
  check_patterns.emplace_back(entry_pattern, fn);
}

// check the results of all subscribed benchmarks
void ResultsChecker::CheckResults(std::stringstream& output) {
  // first reset the stream to the start
  {
    auto start = std::ios::streampos(0);
    // clear before calling tellg()
    output.clear();
    // seek to zero only when needed
    if(output.tellg() > start) output.seekg(start);
    // and just in case
    output.clear();
  }
  // now go over every line and publish it to the ResultsChecker
  std::string line;
  bool on_first = true;
  while (output.eof() == false) {
    CHECK(output.good());
    std::getline(output, line);
    if (on_first) {
      on_first = false;
      continue;
    }
  }
  // finally we can call the subscribed check functions
  for(const auto& p : check_patterns) {
    VLOG(2) << "--------------------------------\n";
    VLOG(2) << "checking for benchmarks matching " << p.regex_str << "...\n";
    for(const auto& r : results) {
      if(!p.regex->Match(r.name)) {
        VLOG(2) << p.regex_str << " is not matched by " << r.name << "\n";
        continue;
      } else {
        VLOG(2) << p.regex_str << " is matched by " << r.name << "\n";
      }
      VLOG(1) << "Checking results of " << r.name << ": ... \n";
      p.fn(r);
      VLOG(1) << "Checking results of " << r.name << ": OK.\n";
    }
  }
}

}  // end namespace internal

size_t AddChecker(const char* bm_name, ResultsCheckFn fn)
{
  auto &rc = internal::GetResultsChecker();
  rc.Add(bm_name, fn);
  return rc.results.size();
}

int Results::NumThreads() const {
  auto pos = name.find("/threads:");
  if(pos == name.npos) return 1;
  auto end = name.find('/', pos + 9);
  std::stringstream ss;
  ss << name.substr(pos + 9, end);
  int num = 1;
  ss >> num;
  CHECK(!ss.fail());
  return num;
}

double Results::GetTime(BenchmarkTime which) const {
  CHECK(which == kCpuTime || which == kRealTime);
  const char *which_str = which == kCpuTime ? "cpu_time" : "real_time";
  double val = GetAs< double >(which_str);
  auto unit = Get("time_unit");
  CHECK(unit);
  if(*unit == "ns") {
    return val * 1.e-9;
  } else if(*unit == "us") {
    return val * 1.e-6;
  } else if(*unit == "ms") {
    return val * 1.e-3;
  } else if(*unit == "s") {
    return val;
  } else {
    CHECK(1 == 0) << "unknown time unit: " << *unit;
    return 0;
  }
}

// ========================================================================= //
// -------------------------- Public API Definitions------------------------ //
// ========================================================================= //

TestCase::TestCase(std::string re, int rule)
    : regex_str(std::move(re)),
      match_rule(rule),
      substituted_regex(internal::PerformSubstitutions(regex_str)),
      regex(std::make_shared<benchmark::Regex>()) {
  benchmark::ErrorCode EC = regex->Init(substituted_regex);
  CHECK(!EC) << "Could not construct regex \"" << substituted_regex << "\""
             << "\n    originally \"" << regex_str << "\""
             << "\n    got error: " << EC.message();
}

int AddCases(TestCaseID ID, std::initializer_list<TestCase> il) {
  auto& L = internal::GetTestCaseList(ID);
  L.insert(L.end(), il);
  return 0;
}

int SetSubstitutions(
    std::initializer_list<std::pair<std::string, std::string>> il) {
  auto& subs = internal::GetSubstitutions();
  for (auto KV : il) {
    bool exists = false;
    KV.second = internal::PerformSubstitutions(KV.second);
    for (auto& EKV : subs) {
      if (EKV.first == KV.first) {
        EKV.second = std::move(KV.second);
        exists = true;
        break;
      }
    }
    if (!exists) subs.push_back(std::move(KV));
  }
  return 0;
}

void RunOutputTests(int argc, char* argv[]) {
  using internal::GetTestCaseList;
  benchmark::Initialize(&argc, argv);

  std::stringstream* out_stream = new std::stringstream();
  std::stringstream* err_stream = new std::stringstream();
  benchmark::SetOutputStream(out_stream);
  benchmark::SetErrorStream(err_stream);

  // Create the test reporter and run the benchmarks.
  std::cout << "Running benchmarks...\n";
  benchmark::RunSpecifiedBenchmarks();

  std::string msg = "\nTesting Console  Output\n";
  std::string banner(msg.size() - 1, '-');
  std::cout << banner << msg << banner << "\n";

  std::cerr << err_stream->str();
  std::cout << out_stream->str();
  benchmark::SetOutputStream(nullptr);
  benchmark::SetErrorStream(nullptr);

  internal::CheckCases(GetTestCaseList(TC_ConsoleErr), *err_stream);
  internal::CheckCases(GetTestCaseList(TC_ConsoleOut), *out_stream);

  delete out_stream;
  delete err_stream;

  std::cout << "\n";
}
