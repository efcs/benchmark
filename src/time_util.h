#ifndef BENCHMARK_TIME_UTIL_H_
#define BENCHMARK_TIME_UTIL_H_

#include <chrono>
#include <ctime>
#include <string>
#include <utility>

namespace benchmark {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;

using std::chrono::duration;
using std::chrono::duration_cast;

using TimeSpec = struct timespec;

namespace internal {
template <class Tp>
struct IsDurationType : std::false_type {};
template <class Rep, class Ratio>
struct IsDurationType<duration<Rep, Ratio>> : std::true_type {};

#define REQUIRE_DURATION_TYPE(Type)                                 \
  static_assert(::benchmark::internal::IsDurationType<Type>::value, \
                "only std::chrono::duration types are allowed")

template <class DurT, class SubDurT, class FromDur>
std::pair<DurT, SubDurT> SplitTime(FromDur Dur) {
  REQUIRE_DURATION_TYPE(FromDur);
  auto big_dur = duration_cast<DurT>(Dur);
  auto subsec_dur = duration_cast<SubDurT>(Dur - big_dur);
  if (subsec_dur.count() < 0) {
    big_dur -= DurT(1);
    subsec_dur += DurT(1);
  }
  return {big_dur, subsec_dur};
}

}  // namespace internal

template <class FromDur>
TimeSpec ToTimeSpec(FromDur Dur) {
  REQUIRE_DURATION_TYPE(FromDur);
  auto split_time = internal::SplitTime<seconds, nanoseconds>(Dur);
  TimeSpec spec;
  spec.tv_sec = split_time.first.count();
  spec.tv_nsec = split_time.second.count();
  return spec;
}

inline nanoseconds FromTimeSpec(TimeSpec const& TS) {
  nanoseconds time;
  time += seconds(TS.tv_sec);
  time += nanoseconds(TS.tv_nsec);
  return time;
}

// Return the CPU usage of the current process
nanoseconds ProcessCPUUsage();

// Return the CPU usage of the current thread
nanoseconds ThreadCPUUsage();

using FPSeconds = std::chrono::duration<double, std::ratio<1>>;
using FPNanoSeconds = std::chrono::duration<double, std::nano>;

class ThreadCPUClock {
 public:
  typedef nanoseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef std::chrono::time_point<ThreadCPUClock, duration> time_point;
  static constexpr const bool is_steady = true;

  static time_point now() noexcept { return time_point(ThreadCPUUsage()); }
};

#if defined(HAVE_STEADY_CLOCK)
template <bool HighResIsSteady = std::chrono::high_resolution_clock::is_steady>
struct ChooseSteadyClock {
  typedef std::chrono::high_resolution_clock type;
};

template <>
struct ChooseSteadyClock<false> {
  typedef std::chrono::steady_clock type;
};
#endif

struct ChooseClockType {
#if defined(HAVE_STEADY_CLOCK)
  typedef ChooseSteadyClock<>::type type;
#else
  typedef std::chrono::high_resolution_clock type;
#endif
};

using ChronoClock = ChooseClockType::type;

std::string LocalDateTimeString();

}  // end namespace benchmark

#endif  // BENCHMARK_TIME_UTIL_H_
