#ifndef BENCHMARK_INTERNAL_H
#define BENCHMARK_INTERNAL_H

#include "benchmark/benchmark.h"
#include <string>
#include <functional>


namespace benchmark {

// Interface for custom benchmark result printers.
// By default, benchmark reports are printed to stdout. However an application
// can control the destination of the reports by calling
// RunSpecifiedBenchmarks and passing it a custom reporter object.
// The reporter object must implement the following interface.
class BenchmarkReporter {
 public:
  struct Context {
    int num_cpus;
    double mhz_per_cpu;
    // std::string cpu_info;
    bool cpu_scaling_enabled;

    // The number of chars in the longest benchmark name.
    int name_field_width;
  };

  struct Run {
    Run()
        : thread_index(-1),
          iterations(1),
          real_accumulated_time(0),
          cpu_accumulated_time(0),
          bytes_per_second(0),
          items_per_second(0),
          max_heapbytes_used(0) {}

    std::string benchmark_name;
    std::string report_label;
    int thread_index;
    int64_t iterations;
    double real_accumulated_time;
    double cpu_accumulated_time;

    // Zero if not set by benchmark.
    double bytes_per_second;
    double items_per_second;

    // This is set to 0.0 if memory tracing is not enabled.
    double max_heapbytes_used;
  };

  // Called once for every suite of benchmarks run.
  // The parameter "context" contains information that the
  // reporter may wish to use when generating its report, for example the
  // platform under which the benchmarks are running. The benchmark run is
  // never started if this function returns false, allowing the reporter
  // to skip runs based on the context information.
  virtual bool ReportContext(const Context& context) const = 0;

  // Called once for each group of benchmark runs, gives information about
  // cpu-time and heap memory usage during the benchmark run.
  // Note that all the grouped benchmark runs should refer to the same
  // benchmark, thus have the same name.
  virtual void ReportRuns(const std::vector<Run>& report) const = 0;

  virtual ~BenchmarkReporter() {}
};

namespace internal {


typedef std::function<void(State&)> BenchmarkFunction;

// Run all benchmarks whose name is a partial match for the regular
// expression in "spec". The results of benchmark runs are fed to "reporter".
void RunMatchingBenchmarks(const std::string& spec,
                           const BenchmarkReporter* reporter);

// Extract the list of benchmark names that match the specified regular
// expression.
void FindMatchingBenchmarkNames(const std::string& re,
                                std::vector<std::string>* benchmark_names);

                                
class BenchmarkImp
{
public:
    BenchmarkImp(const char* name, BenchmarkFunction f)
        : name_(name), function_(f)
    {}
    
    std::string name_;
    BenchmarkFunction function_;
    int registration_index_;
    std::vector<int> rangeX_;
    std::vector<int> rangeY_;
    std::vector<int> thread_counts_;
    std::mutex mutex_;
};


// ------------------------------------------------------
// Internal implementation details follow; please ignore

// Simple reporter that outputs benchmark data to the console. This is the
// default reporter used by RunSpecifiedBenchmarks().
class ConsoleReporter : public BenchmarkReporter {
 public:
  virtual bool ReportContext(const Context& context) const;
  virtual void ReportRuns(const std::vector<Run>& reports) const;

 private:
  std::string PrintMemoryUsage(double bytes) const;
  virtual void PrintRunData(const Run& report) const;
  mutable int name_field_width_;
};


} // namespace internal
} // namespace benchmark
#endif // BENCHMARK_INTERNAL_H
