#ifndef BENCHMARK_REPORTER_H_
#define BENCHMARK_REPORTER_H_

#include "benchmark/benchmark.h"

namespace benchmark {
// Interface for custom benchmark result printers.
// By default, benchmark reports are printed to stdout. However an application
// can control the destination of the reports by calling
// RunSpecifiedBenchmarks and passing it a custom reporter object.
// The reporter object must implement the following interface.
class BenchmarkReporter {
 public:
  // Construct a BenchmarkReporter with the output stream set to 'std::cout'
  // and the error stream set to 'std::cerr'
  BenchmarkReporter();

  // Called once for every suite of benchmarks run.
  // The parameter "context" contains information that the
  // reporter may wish to use when generating its report, for example the
  // platform under which the benchmarks are running. The benchmark run is
  // never started if this function returns false, allowing the reporter
  // to skip runs based on the context information.
  virtual bool ReportContext(const JSON& context) = 0;

  // Called once for each group of benchmark runs, gives information about
  // cpu-time and heap memory usage during the benchmark run. If the group
  // of runs contained more than two entries then 'report' contains additional
  // elements representing the mean and standard deviation of those runs.
  // Additionally if this group of runs was the last in a family of benchmarks
  // 'reports' contains additional entries representing the asymptotic
  // complexity and RMS of that benchmark family.
  virtual void ReportResults(const JSON& result) = 0;

  // Called once and only once after ever group of benchmarks is run and
  // reported.
  virtual void Finalize() {}

  // REQUIRES: The object referenced by 'out' is valid for the lifetime
  // of the reporter.
  void SetOutputStream(std::ostream* out) {
    assert(out);
    output_stream_ = out;
  }

  // REQUIRES: The object referenced by 'err' is valid for the lifetime
  // of the reporter.
  void SetErrorStream(std::ostream* err) {
    assert(err);
    error_stream_ = err;
  }

  std::ostream& GetOutputStream() const { return *output_stream_; }

  std::ostream& GetErrorStream() const { return *error_stream_; }

  virtual ~BenchmarkReporter();

  // Write a human readable string to 'out' representing the specified
  // 'context'.
  // REQUIRES: 'out' is non-null.
  static void PrintBasicContext(std::ostream* out, JSON const& context);

 private:
  std::ostream* output_stream_;
  std::ostream* error_stream_;
};

// Simple reporter that outputs benchmark data to the console. This is the
// default reporter used by RunSpecifiedBenchmarks().
class ConsoleReporter : public BenchmarkReporter {
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

  virtual bool ReportContext(const JSON& context);
  virtual void ReportResults(const JSON& result);

 protected:
  virtual void PrintRunData(JSON const& report);
  virtual void PrintHeader(const JSON& report);

  OutputOptions output_options_;
  size_t name_field_width_;
  UserCounters prev_counters_;
  bool printed_header_;
};

class JSONReporter : public BenchmarkReporter {
 public:
  JSONReporter() : first_report_(true) {}
  virtual bool ReportContext(const JSON& context);
  virtual void ReportResults(const JSON& result);
  virtual void Finalize();

 private:
  bool first_report_;
};

}  // namespace benchmark

#endif  // BENCHMARK_REPORTER_H_
