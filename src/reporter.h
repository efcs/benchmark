#ifndef BENCHMARK_REPORTER_H_
#define BENCHMARK_REPORTER_H_

#include "benchmark/benchmark.h"

namespace benchmark {

void PrintBasicContext(std::ostream* out, JSON const& context);

// Simple reporter that outputs benchmark data to the console. This is the
// default reporter used by RunSpecifiedBenchmarks().
class ConsoleReporter {
 public:
  enum OutputOptions {
    OO_None = 0,
    OO_Color = 1,
    OO_Tabular = 2,
    OO_ColorTabular = OO_Color | OO_Tabular,
    OO_Defaults = OO_ColorTabular
  };
  explicit ConsoleReporter(OutputOptions opts_ = OO_Defaults)
      : output_options_(opts_),
        name_field_width_(0),
        prev_counters_(),
        printed_header_(false) {}

  void operator()(CallbackKind K, JSON const& J) {
    switch (K) {
      case CK_Initial:
        return Initialize(J);
      case CK_Context:
        PrintBasicContext(&GetErrorStream(), J);
        std::flush(GetErrorStream());
        break;
      case CK_Report:
        return ReportResults(J);
      case CK_Final:
        break;  // nothing to do
    }
  }

  static OutputOptions GetCommandLineOutputOptions(bool force_no_color = false);
  static ConsoleReporter GetCommandLineReporter() {
    return ConsoleReporter(GetCommandLineOutputOptions());
  }

 private:
  void Initialize(const JSON& init_info);
  void ReportResults(const JSON& result);
  void PrintRunData(const JSON& report);
  void PrintHeader(const JSON& report);

 private:
  OutputOptions output_options_;
  size_t name_field_width_;
  UserCounters prev_counters_;
  bool printed_header_;
};

ConsoleReporter& GetGlobalConsoleReporter();

}  // namespace benchmark

#endif  // BENCHMARK_REPORTER_H_
