#ifndef BENCHMARK_MINIMAL_BENCHMARK_H_
#define BENCHMARK_MINIMAL_BENCHMARK_H_

#include <stdint.h>
#include "macros.h"

namespace benchmark {
class BenchmarkReporter;

void Initialize(int* argc, const char** argv);

// Otherwise, run all benchmarks specified by the --benchmark_filter flag,
// and exit after running the benchmarks.
void RunSpecifiedBenchmarks(const BenchmarkReporter* reporter = nullptr);

// ------------------------------------------------------
// Routines that can be called from within a benchmark

// If this routine is called, peak memory allocation past this point in the
// benchmark is reported at the end of the benchmark report line. (It is
// computed by running the benchmark once with a single iteration and a memory
// tracer.)
// TODO(dominic)
// void MemoryUsage();

// If a particular benchmark is I/O bound, or if for some reason CPU
// timings are not representative, call this method from within the
// benchmark routine.  If called, the elapsed time will be used to
// control how many iterations are run, and in the printing of
// items/second or MB/seconds values.  If not called, the cpu time
// used by the benchmark will be used.
void UseRealTime();

namespace internal {
class Benchmark;
class BenchmarkFamilies;
}

// State is passed to a running Benchmark and contains state for the
// benchmark to use.
class State {
 public:
  ~State();

  // Returns true iff the benchmark should continue through another iteration.
  bool KeepRunning();

  void PauseTiming();
  void ResumeTiming();

  // Set the number of bytes processed by the current benchmark
  // execution.  This routine is typically called once at the end of a
  // throughput oriented benchmark.  If this routine is called with a
  // value > 0, the report is printed in MB/sec instead of nanoseconds
  // per iteration.
  //
  // REQUIRES: a benchmark has exited its KeepRunning loop.
  void SetBytesProcessed(int64_t bytes);

  // If this routine is called with items > 0, then an items/s
  // label is printed on the benchmark report line for the currently
  // executing benchmark. It is typically called at the end of a processing
  // benchmark where a processing items/second output is desired.
  //
  // REQUIRES: a benchmark has exited its KeepRunning loop.
  void SetItemsProcessed(int64_t items);

  // If this routine is called, the specified label is printed at the
  // end of the benchmark report line for the currently executing
  // benchmark.  Example:
  //  static void BM_Compress(int iters) {
  //    ...
  //    double compress = input_size / output_size;
  //    benchmark::SetLabel(StringPrintf("compress:%.1f%%", 100.0*compression));
  //  }
  // Produces output that looks like:
  //  BM_Compress   50         50   14115038  compress:27.3%
  //
  // REQUIRES: a benchmark has exited its KeepRunning loop.
  void SetLabel(const char* label);

  // Range arguments for this run. CHECKs if the argument has been set.
  int range_x() const;
  int range_y() const;

  int iterations() const { return total_iterations_; }

  const int thread_index;

 private:
  class FastClock;
  struct SharedState;
  struct PIMPLThread;
  struct ThreadStats;

  State(FastClock* clock, SharedState* s, int t);
  bool StartRunning();
  bool FinishInterval();
  bool MaybeStop();
  void NewInterval();
  bool AllStarting();

  static void* RunWrapper(void* arg);
  void Run();
  void RunAsThread();
  void Wait();

  enum EState {
    STATE_INITIAL,   // KeepRunning hasn't been called
    STATE_STARTING,  // KeepRunning called, waiting for other threads
    STATE_RUNNING,   // Running and being timed
    STATE_STOPPING,  // Not being timed but waiting for other threads
    STATE_STOPPED    // Stopped
  };

  EState state_;

  FastClock* clock_;

  // State shared by all BenchmarkRun objects that belong to the same
  // BenchmarkInstance
  SharedState* shared_;

  // Hide the thread type behind a pointer.
  PIMPLThread* thread_ptr_;

  // Each State object goes through a sequence of measurement intervals. By
  // default each interval is approx. 100ms in length. The following stats are
  // kept for each interval.
  int64_t iterations_;
  double start_cpu_;
  double start_time_;
  int64_t stop_time_micros_;

  double start_pause_cpu_;
  double pause_cpu_time_;
  double start_pause_real_;
  double pause_real_time_;

  // Total number of iterations for all finished runs.
  int64_t total_iterations_;

  // Approximate time in microseconds for one interval of execution.
  // Dynamically adjusted as needed.
  int64_t interval_micros_;

  // True if the current interval is the continuation of a previous one.
  bool is_continuation_;

  ThreadStats* stats_;

  friend class internal::Benchmark;
  DISALLOW_COPY_AND_ASSIGN(State)
};


namespace internal {

class MinimalBenchmark {
 public:
  // The MinimalBenchmark takes ownership of the Callback pointed to by f.
  MinimalBenchmark(const char* name, void(*f)(State&));

  ~MinimalBenchmark();
  MinimalBenchmark* Arg(int x);
  MinimalBenchmark* Range(int start, int limit);
  MinimalBenchmark* DenseRange(int start, int limit);
  MinimalBenchmark* ArgPair(int x, int y);
  MinimalBenchmark* RangePair(int lo1, int hi1, int lo2, int hi2);
  MinimalBenchmark* Apply(void (*func)(Benchmark* benchmark));
  MinimalBenchmark* Threads(int t);
  MinimalBenchmark* ThreadRange(int min_threads, int max_threads);
  MinimalBenchmark* ThreadPerCpu();

private:
    Benchmark* imp_;
};

}  // end namespace internal
}  // end namespace benchmark

// ------------------------------------------------------
// Macro to register benchmarks

// Helpers for generating unique variable names
#define BENCHMARK_CONCAT(a, b, c) BENCHMARK_CONCAT2(a, b, c)
#define BENCHMARK_CONCAT2(a, b, c) a##b##c

#define BENCHMARK(n)                                         \
  static ::benchmark::internal::MinimalBenchmark* BENCHMARK_CONCAT( \
      __benchmark_, n, __LINE__) ATTRIBUTE_UNUSED =          \
      (new ::benchmark::internal::MinimalBenchmark(#n, n))

// Old-style macros
#define BENCHMARK_WITH_ARG(n, a) BENCHMARK(n)->Arg((a))
#define BENCHMARK_WITH_ARG2(n, a1, a2) BENCHMARK(n)->ArgPair((a1), (a2))
#define BENCHMARK_RANGE(n, lo, hi) BENCHMARK(n)->Range((lo), (hi))
#define BENCHMARK_RANGE2(n, l1, h1, l2, h2) \
  BENCHMARK(n)->RangePair((l1), (h1), (l2), (h2))

// This will register a benchmark for a templatized function.  For example:
//
// template<int arg>
// void BM_Foo(int iters);
//
// BENCHMARK_TEMPLATE(BM_Foo, 1);
//
// will register BM_Foo<1> as a benchmark.
#define BENCHMARK_TEMPLATE(n, a)                             \
  static ::benchmark::internal::MinimalBenchmark* BENCHMARK_CONCAT( \
      __benchmark_, n, __LINE__) ATTRIBUTE_UNUSED =          \
      (new ::benchmark::internal::MinimalBenchmark(#n "<" #a ">", n<a>))

#define BENCHMARK_TEMPLATE2(n, a, b)                         \
  static ::benchmark::internal::MinimalBenchmark* BENCHMARK_CONCAT( \
      __benchmark_, n, __LINE__) ATTRIBUTE_UNUSED =          \
      (new ::benchmark::internal::MinimalBenchmark(#n "<" #a "," #b ">", n<a, b>))

#endif // BENCHMARK_MINIMAL_BENCHMARK_H_