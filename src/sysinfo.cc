// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "internal_macros.h"

#ifdef BENCHMARK_OS_WINDOWS
#include <Shlwapi.h>
#include <VersionHelpers.h>
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>  // this header must be included before 'sys/sysctl.h' to avoid compilation error on FreeBSD
#include <unistd.h>
#if defined BENCHMARK_OS_FREEBSD || defined BENCHMARK_OS_MACOSX || \
    defined BENCHMARK_OS_NETBSD
#define BENCHMARK_HAS_SYSCTL
#include <sys/sysctl.h>
#elif defined BENCHMARK_OS_LINUX
#define BENCHMARK_HAS_PROC_FS
#endif
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>

#include "arraysize.h"
#include "check.h"
#include "cycleclock.h"
#include "internal_macros.h"
#include "log.h"
#include "sleep.h"
#include "string_util.h"

namespace benchmark {
namespace {

static void PrintImp(std::ostream& out) { out << std::endl; }

template <class First, class... Rest>
static void PrintImp(std::ostream& out, First&& f, Rest&&... rest) {
  out << std::forward<First>(f);
  PrintImp(out, std::forward<Rest>(rest)...);
}

template <class... Args>
BENCHMARK_NORETURN static void PrintErrorAndDie(Args&&... args) {
  PrintImp(std::cerr, std::forward<Args>(args)...);
  std::exit(1);
}

#ifdef BENCHMARK_HAS_SYSCTL

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
struct ValueUnion;

using ValueUnionPtr = std::unique_ptr<ValueUnion, decltype(&std::free)>;

struct ValueUnion {
  size_t Size;
  union {
    int32_t int32_value;
    int64_t int64_value;
    uint32_t uint32_value;
    uint64_t uint64_value;
    char string_value[];
  };

  char* data() { return string_value; }

  std::string getAsString() const { return std::string(string_value); }

  long long getAsInteger() {
    if (Size == sizeof(int32_value))
      return int32_value;
    else if (Size == sizeof(int64_value))
      return int64_value;
    CHECK(false) << "invalid size";
    return 0;
  }

  unsigned long long getAsUnsigned() {
    if (Size == sizeof(uint32_value))
      return uint32_value;
    else if (Size == sizeof(uint64_value))
      return uint64_value;
    CHECK(false) << "invalid size";
    return 0;
  }

  static ValueUnionPtr Create(size_t Size) {
    const size_t NewSize = sizeof(ValueUnion) + Size;
    const size_t UnionSize = std::max(sizeof(char*), sizeof(uint64_t)) + Size;
    void* mem = std::malloc(NewSize);
    ValueUnion* V = new (mem) ValueUnion(UnionSize);
    ValueUnionPtr ptr(V, &std::free);
    return ptr;
  }

 private:
  ValueUnion(size_t S) : Size(S), int32_value(0) {}
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

static ValueUnionPtr GetSysctlImp(std::string const& Name) {
  size_t CurBuffSize = static_cast<size_t>(-1);
  int res = sysctlbyname(Name.c_str(), nullptr, &CurBuffSize, nullptr, 0);
  ((void)res);
  CHECK_EQ(res, -1);

  ValueUnionPtr buff = ValueUnion::Create(CurBuffSize);
  if (sysctlbyname(Name.c_str(), buff->data(), &buff->Size, nullptr, 0) == 0)
    return buff;
  buff.reset();
  return buff;
}

BENCHMARK_MAYBE_UNUSED
static bool GetSysctl(std::string const& Name, std::string* Out) {
  Out->clear();
  auto Buff = GetSysctlImp(Name);
  if (!Buff) return false;
  Out->assign(Buff->data());
  return true;
}

template <class Tp,
          class = typename std::enable_if<std::is_integral<Tp>::value>::type>
BENCHMARK_MAYBE_UNUSED static bool GetSysctl(std::string const& Name, Tp* Out) {
  *Out = 0;
  auto Buff = GetSysctlImp(Name);
  if (!Buff) return false;
  *Out = static_cast<Tp>(Buff->getAsUnsigned());
  return true;
}
#elif defined BENCHMARK_HAS_PROC_FS
BENCHMARK_MAYBE_UNUSED
static bool GetSysctl(std::string const& Name, std::string* Out) {
  Out->clear();
  std::string Path = "/proc/sys/" + Name;
  std::for_each(Path.begin(), Path.end(), [](char& ch) {
    if (ch == '.') ch = '/';
  });
  std::ifstream f(Path.c_str());
  if (!f.is_open()) return false;
  using It = std::istreambuf_iterator<char>;
  std::string Res((It(f)), It{});
  if (f.bad()) return false;
  (*Out) = std::move(Res);
  return true;
}

template <class Tp,
          class = typename std::enable_if<std::is_integral<Tp>::value>::type>
BENCHMARK_MAYBE_UNUSED static bool GetSysctl(std::string const& Name, Tp* Val) {
  *Val = 0;
  std::string tmp;
  if (!GetSysctl(Name, &tmp)) return false;
  if (std::is_signed<Tp>::value) {
    long long tmp_val = std::stoll(tmp);
    *Val = static_cast<Tp>(tmp_val);
  } else {
    unsigned long long tmp_val = std::stoull(tmp);
    *Val = static_cast<Tp>(tmp_val);
  }
  return true;
}
#endif

#if defined(BENCHMARK_HAS_SYSCTL) || defined(BENCHMARK_HAS_PROC_FS)
BENCHMARK_MAYBE_UNUSED
static bool GetSysctl(std::string const& Name, std::vector<std::string>* Vals) {
  std::string tmp;
  if (!GetSysctl(Name, &tmp)) return false;
  std::istringstream iss(tmp);
  using InIt = std::istream_iterator<std::string>;
  std::copy(InIt(iss), InIt(), std::back_inserter(*Vals));
  return true;
}

template <class Tp>
BENCHMARK_MAYBE_UNUSED static bool GetSysctlWithError(std::string const& Name,
                                                      Tp* val) {
  if (!GetSysctlWithError(Name, val)) {
    std::cerr << "Failed to read sysctl field '" << Name << "'";
    return false;
  }
  return true;
}
#endif

#if !defined BENCHMARK_OS_MACOSX
const int64_t estimate_time_ms = 1000;

// Helper function estimates cycles/sec by observing cycles elapsed during
// sleep(). Using small sleep time decreases accuracy significantly.
int64_t EstimateCyclesPerSecond() {
  const int64_t start_ticks = cycleclock::Now();
  SleepForMilliseconds(estimate_time_ms);
  return cycleclock::Now() - start_ticks;
}
#endif

static bool ReadFromFileImp(std::ifstream& in) { return in.good(); }

// Helper function for reading an int from a file. Returns true if successful
// and the memory location pointed to by value is set to the value read.
template <class First, class... Args>
bool ReadFromFileImp(std::ifstream& in, First* value, Args... args) {
  *value = First();
  in >> *value;
  if (!in.good()) return false;
  return ReadFromFileImp(in, args...);
}

template <class... Args>
bool ReadFromFile(std::string const& fname, Args*... args) {
  std::ifstream f(fname.c_str());
  if (!f.is_open()) return false;
  return ReadFromFileImp(f, args...);
}

#if defined BENCHMARK_OS_LINUX || defined BENCHMARK_OS_CYGWIN

static bool startsWithKey(std::string const& Value, std::string const& Key,
                          bool IgnoreCase = true) {
  if (Key.size() > Value.size()) return false;
  auto Cmp = [&](char X, char Y) {
    return IgnoreCase ? std::tolower(X) == std::tolower(Y) : X == Y;
  };
  return std::equal(Key.begin(), Key.end(), Value.begin(), Cmp);
}
#endif

bool CpuScalingEnabled(int num_cpus) {
#ifndef BENCHMARK_OS_WINDOWS
  // On Linux, the CPUfreq subsystem exposes CPU information as files on the
  // local file system. If reading the exported files fails, then we may not be
  // running on Linux, so we silently ignore all the read errors.
  std::string res;
  for (int cpu = 0; cpu < num_cpus; ++cpu) {
    std::string governor_file =
        StrCat("/sys/devices/system/cpu/cpu", cpu, "/cpufreq/scaling_governor");
    if (ReadFromFile(governor_file, &res)) {
      if (res == "performance") return true;
    }
  }
#endif
  return false;
}

BENCHMARK_MAYBE_UNUSED
static std::vector<CPUInfo::CacheInfo> GetCacheSizesFromKVFS() {
  std::vector<CPUInfo::CacheInfo> res;
  std::string dir = "/sys/devices/system/cpu/cpu0/cache/";
  int Idx = 0;
  while (true) {
    CPUInfo::CacheInfo info;
    std::string FPath = StrCat(dir, "index", Idx++, "/");
    std::ifstream f(StrCat(FPath, "size").c_str());
    if (!f.is_open()) {
      break;
    }
    std::string suffix;
    f >> info.size;
    if (f.fail())
      PrintErrorAndDie("Failed while reading file '", FPath, "size'");
    if (f.good()) {
      f >> suffix;
      if (f.bad())
        PrintErrorAndDie(
            "Invalid cache size format: failed to read size suffix");
      else if (f && suffix != "K")
        PrintErrorAndDie("Invalid cache size format: Expected bytes ", suffix);
      else if (suffix == "K")
        info.size *= 1000;
    }
    if (!ReadFromFile(StrCat(FPath, "type"), &info.type))
      PrintErrorAndDie("Failed to read from file ", FPath, "type");
    if (!ReadFromFile(StrCat(FPath, "level"), &info.level))
      PrintErrorAndDie("Failed to read from file ", FPath, "level");
    res.push_back(info);
  }

  return res;
}
#ifdef BENCHMARK_OS_MACOSX
static std::vector<CPUInfo::CacheInfo> GetCacheSizesMacOSX() {
  std::vector<CPUInfo::CacheInfo> res;
  struct {
    std::string name;
    std::string type;
    int level;
  } Cases[] = {{"hw.l1dcachesize", "Data", 1},
               {"hw.l1icachesize", "Instruction", 1},
               {"hw.l2cachesize", "Unified", 2},
               {"hw.l3cachesize", "Unified", 3}};
  for (auto& C : Cases) {
    int val;
    if (!GetSysctl(C.name, &val)) continue;
    CPUInfo::CacheInfo info;
    info.type = C.type;
    info.level = C.level;
    info.size = val;
    res.push_back(std::move(info));
  }
  return res;
}
#endif

static std::vector<CPUInfo::CacheInfo> GetCacheSizes() {
#ifdef BENCHMARK_OS_MACOSX
  return GetCacheSizesMacOSX();
#else
  return GetCacheSizesFromKVFS();
#endif
}

void InitializeSystemInfo(CPUInfo& info) {
#if defined BENCHMARK_OS_LINUX || defined BENCHMARK_OS_CYGWIN

  long freq;

  bool saw_mhz = false;

  // If the kernel is exporting the tsc frequency use that. There are issues
  // where cpuinfo_max_freq cannot be relied on because the BIOS may be
  // exporintg an invalid p-state (on x86) or p-states may be used to put the
  // processor in a new mode (turbo mode). Essentially, those frequencies
  // cannot always be relied upon. The same reasons apply to /proc/cpuinfo as
  // well.
  if (!saw_mhz &&
      ReadFromFile("/sys/devices/system/cpu/cpu0/tsc_freq_khz", &freq)) {
    // The value is in kHz (as the file name suggests).  For example, on a
    // 2GHz warpstation, the file contains the value "2000000".
    info.cycles_per_second = freq * 1000.0;
    saw_mhz = true;
  }

  // If CPU scaling is in effect, we want to use the *maximum* frequency,
  // not whatever CPU speed some random processor happens to be using now.
  if (!saw_mhz &&
      ReadFromFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
                   &freq)) {
    // The value is in kHz.  For example, on a 2GHz warpstation, the file
    // contains the value "2000000".
    info.cycles_per_second = freq * 1000.0;
    saw_mhz = true;
  }


  double bogo_clock = 1.0;
  bool saw_bogo = false;
  long max_cpu_id = 0;
  int num_cpus = 0;

  std::ifstream f("/proc/cpuinfo");
  if (!f.is_open()) {
    std::cerr << "failed to open /proc/cpuinfo\n";
    if (!saw_mhz) {
      info.cycles_per_second = static_cast<double>(EstimateCyclesPerSecond());
    }
    return;
  }

  std::string ln;
  while (std::getline(f, ln)) {
    if (ln.empty()) continue;
    size_t SplitIdx = ln.find(':');
    std::string value;
    if (SplitIdx != std::string::npos) value = ln.substr(SplitIdx + 1);
    // When parsing the "cpu MHz" and "bogomips" (fallback) entries, we only
    // accept postive values. Some environments (virtual machines) report zero,
    // which would cause infinite looping in WallTime_Init.
    if (!saw_mhz && startsWithKey(ln, "cpu MHz")) {
      if (!value.empty()) {
        info.cycles_per_second = std::stod(value) * 1000000.0;
        if (info.cycles_per_second > 0) saw_mhz = true;
      }
    } else if (startsWithKey(ln, "bogomips")) {
      ;
      if (!value.empty()) {
        bogo_clock = std::stod(value) * 1000000.0;
        if (bogo_clock > 0) saw_bogo = true;
      }
    } else if (startsWithKey(ln, "processor", /*IgnoreCase*/ false)) {
      // The above comparison is case-sensitive because ARM kernels often
      // include a "Processor" ln that tells you about the CPU, distinct
      // from the usual "processor" lns that give you CPU ids. No current
      // Linux architecture is using "Processor" for CPU ids.
      num_cpus++;  // count up every time we see an "processor :" entry
      if (!value.empty()) {
        const long cpu_id = std::stol(value);
        if (max_cpu_id < cpu_id) max_cpu_id = cpu_id;
      }
    }
  }
  if (f.bad()) {
    std::cerr << "Failure reading /proc/cpuinfo\n";
    return;
  }
  if (!f.eof()) {
    std::cerr << "Failed to read to end of /proc/cpuinfo\n";
    return;
  }
  f.close();

  if (!saw_mhz) {
    if (saw_bogo) {
      // If we didn't find anything better, we'll use bogomips, but
      // we're not happy about it.
      info.cycles_per_second = bogo_clock;
    } else {
      // If we don't even have bogomips, we'll use the slow estimation.
      info.cycles_per_second = static_cast<double>(EstimateCyclesPerSecond());
    }
  }
  if (num_cpus == 0) {
    fprintf(stderr, "Failed to read num. CPUs correctly from /proc/cpuinfo\n");
  } else {
    if ((max_cpu_id + 1) != num_cpus) {
      fprintf(stderr,
              "CPU ID assignments in /proc/cpuinfo seem messed up."
              " This is usually caused by a bad BIOS.\n");
    }
    info.num_cpus = num_cpus;
  }

#elif defined BENCHMARK_OS_FREEBSD || defined BENCHMARK_OS_NETBSD
  // FreeBSD notes
  // =============
  // For this sysctl to work, the machine must be configured without
  // SMP, APIC, or APM support.  hz should be 64-bit in freebsd 7.0
  // and later.  Before that, it's a 32-bit quantity (and gives the
  // wrong answer on machines faster than 2^32 Hz).  See
  //  http://lists.freebsd.org/pipermail/freebsd-i386/2004-November/001846.html
  // But also compare FreeBSD 7.0:
  //  http://fxr.watson.org/fxr/source/i386/i386/tsc.c?v=RELENG70#L223
  //  231         error = sysctl_handle_quad(oidp, &freq, 0, req);
  // To FreeBSD 6.3 (it's the same in 6-STABLE):
  //  http://fxr.watson.org/fxr/source/i386/i386/tsc.c?v=RELENG6#L131
  //  139         error = sysctl_handle_int(oidp, &freq, sizeof(freq), req);
  unsigned long long hz = 0;
  if (!GetSysctl("machdep.tsc_freq", &hz)) {
    fprintf(stderr, "Unable to determine clock rate from sysctl: %s: %s\n",
            sysctl_path, strerror(errno));
    info.cycles_per_second = static_cast<double>(EstimateCyclesPerSecond());
  } else {
    info.cycles_per_second = hz;
  }

  if (!GetSysctl("hw.ncpu", &info.num_cpus)) {
    fprintf(stderr, "%s\n", strerror(errno));
    std::exit(EXIT_FAILURE);
  }

#elif defined BENCHMARK_OS_WINDOWS
  // In NT, read MHz from the registry. If we fail to do so or we're in win9x
  // then make a crude estimate.
  DWORD data, data_size = sizeof(data);
  if (IsWindowsXPOrGreater() &&
      SUCCEEDED(
          SHGetValueA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      "~MHz", nullptr, &data, &data_size)))
    info.cycles_per_second =
        static_cast<double>((int64_t)data * (int64_t)(1000 * 1000));  // was mhz
  else
    info.cycles_per_second = static_cast<double>(EstimateCyclesPerSecond());

  SYSTEM_INFO sysinfo;
  // Use memset as opposed to = {} to avoid GCC missing initializer false
  // positives.
  std::memset(&sysinfo, 0, sizeof(SYSTEM_INFO));
  GetSystemInfo(&sysinfo);
  info.num_cpus = sysinfo.dwNumberOfProcessors;  // number of logical
                                                 // processors in the current
                                                 // group

#elif defined BENCHMARK_OS_MACOSX
  if (!GetSysctl("hw.ncpu", &info.num_cpus)) {
    fprintf(stderr, "%s\n", strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  info.cycles_per_second = 0;
  int64_t cpu_freq = 0;
  if (!GetSysctl("hw.cpufrequency", &cpu_freq)) {
#if defined BENCHMARK_OS_IOS
    fprintf(stderr, "CPU frequency cannot be detected. \n");
#else
    fprintf(stderr, "%s\n", strerror(errno));
    std::exit(EXIT_FAILURE);
#endif
  }
  info.cycles_per_second = cpu_freq;
#else
  // Generic cycles per second counter
  info.cycles_per_second = static_cast<double>(EstimateCyclesPerSecond());
#endif

  info.scaling_enabled = CpuScalingEnabled(info.num_cpus);
  info.caches = GetCacheSizes();
}

}  // end namespace

CPUInfo::CPUInfo()
    : cycles_per_second(1.0), num_cpus(1), scaling_enabled(false) {}

const CPUInfo& CPUInfo::Get() {
  static CPUInfo info;
  static const CPUInfo& inited_info = (InitializeSystemInfo(info), info);
  return inited_info;
}

}  // end namespace benchmark
