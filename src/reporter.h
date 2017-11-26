#ifndef BENCHMARK_REPORTER_H_
#define BENCHMARK_REPORTER_H_

#include "benchmark/benchmark.h"

namespace benchmark {

void PrintBasicContext(std::ostream* out, JSON const& context);

explicit ConsoleReporter::ConsoleReporter()
    : output_options_(OO_Defaults),
      name_field_width_(0),
      prev_counters_(),
      printed_header_(false) {}

void ConsoleReporter::operator()(CallbackKind K, JSON const& J) {
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

  ConsoleReporter& ConsoleReporter::Get() {
    static ConsoleReporter* reporter = new ConsoleReporter();
    return *reporter;
  }

  ConsoleReporter& GetGlobalReporter() { return ConsoleReporter::Get(); }

}  // namespace benchmark

#endif  // BENCHMARK_REPORTER_H_
