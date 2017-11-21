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
#include <sstream>

#include "check.h"
#include "cycleclock.h"
#include "internal_macros.h"
#include "log.h"
#include "sleep.h"
#include "string_util.h"

namespace benchmark {
namespace {

void PrintImp(std::ostream& out) { out << std::endl; }

template <class First, class... Rest>
void PrintImp(std::ostream& out, First&& f, Rest&&... rest) {
  out << std::forward<First>(f);
  PrintImp(out, std::forward<Rest>(rest)...);
}

template <class... Args>
BENCHMARK_NORETURN void PrintErrorAndDie(Args&&... args) {
  PrintImp(std::cerr, std::forward<Args>(args)...);
  std::exit(1);
}

#ifdef BENCHMARK_HAS_SYSCTL

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

/// ValueUnion - A type used to correctly alias the byte-for-byte output of
/// `sysctl` with the result type it's to be interpreted as.
struct ValueUnion {
  // The size of the union member + its trailing array size.
  size_t Size;
  union DataT {
    char dummy;
    uint32_t uint32_value;
    uint64_t uint64_value;
    // FIXME (Maybe?): This is a C11 flexible array member, and not technically
    // C++. However, all compilers support it and it allows for correct aliasing
    // of union members from bytes.
    char bytes[];
  };
  DataT *data;

  char* data() const { return data->bytes; }

  std::string getAsString() const { return std::string(data()); }

  long long getAsInteger() const {
    if (Size == sizeof(data->uint32_value))
      return static_cast<int32_t>(data->uint32_value);
    else if (Size == sizeof(data->uint64_value))
      return static_cast<int64_t>(data->uint64_value);
    CHECK(false) << "invalid size";
    return 0;
  }

  unsigned long long getAsUnsigned() const {
    if (Size == sizeof(data->uint32_value))
      return data->uint32_value;
    else if (Size == sizeof(data->uint64_value))
      return data->uint64_value;
    CHECK(false) << "invalid size";
    return 0;
  }

  static ValueUnion Create(size_t BuffSize) {.
    const size_t UnionSize = sizeof(DataT) + BuffSize;
    return ValueUnion(::new (std::malloc(UnionSize)) DataT(),UnionSize);
  }

  ValueUnion(ValueUnion&& other) : Size(other.Size), data(other.data)  {
    other.data = nullptr;
    other.Size = 0;
  }

  ~ValueUnion() {
    clear();
  }

  explicit operator bool() const {
    return !empty();
  }

  bool empty() const {
    return data == nullptr;
  }

  void clear() {
    if (data) {
      std::free(data);
      data = nullptr;
    }
    Size = 0;
  }

  ValueUnion() : Size(0), data(nullptr) {}

  explicit ValueUnion(size_t BuffSize)
    : Size(sizeof(DataT) + BuffSize),
      data(::new (std::malloc(Size)) DataT())
  {}
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

ValueUnion GetSysctlImp(std::string const& Name) {
  size_t CurBuffSize = static_cast<size_t>(-1);
  errno = 0;
  sysctlbyname(Name.c_str(), nullptr, &CurBuffSize, nullptr, 0);
  if (errno != ENOMEM)
    return ValueUnion();

  ValueUnion buff(CurBuffSize);
  if (sysctlbyname(Name.c_str(), buff.data(), &buff.Size, nullptr, 0) == 0)
    return buff;
  return ValueUnion();
}

BENCHMARK_MAYBE_UNUSED
bool GetSysctl(std::string const& Name, std::string* Out) {
  Out->clear();
  auto Buff = GetSysctlImp(Name);
  if (!Buff) return false;
  Out->assign(Buff.data());
  return true;
}

template <class Tp,
          class = typename std::enable_if<std::is_integral<Tp>::value>::type>
BENCHMARK_MAYBE_UNUSED bool GetSysctl(std::string const& Name, Tp* Out) {
  *Out = 0;
  auto Buff = GetSysctlImp(Name);
  if (!Buff) return false;
  *Out = static_cast<Tp>(Buff.getAsUnsigned());
  return true;
}

BENCHMARK_MAYBE_UNUSED
bool GetSysctl(std::string const& Name, std::vector<std::string>* Vals) {
  std::string tmp;
  if (!GetSysctl(Name, &tmp)) return false;
  std::istringstream iss(tmp);
  using InIt = std::istream_iterator<std::string>;
  std::copy(InIt(iss), InIt(), std::back_inserter(*Vals));
  return true;
}

template <class Tp>
BENCHMARK_MAYBE_UNUSED bool GetSysctlWithError(std::string const& Name,
                                               Tp* val) {
  if (!GetSysctlWithError(Name, val)) {
    std::cerr << "Failed to read sysctl field '" << Name << "'";
    return false;
  }
  return true;
}
#endif

bool ReadFromFileImp(std::ifstream& in) { return in.good(); }

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

bool CpuScalingEnabled(int num_cpus) {
  // We don't have a valid CPU count, so don't even bother.
  if (num_cpus <= 0)
    return false;
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
std::vector<CPUInfo::CacheInfo> GetCacheSizesFromKVFS() {
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
std::vector<CPUInfo::CacheInfo> GetCacheSizesMacOSX() {
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

std::vector<CPUInfo::CacheInfo> GetCacheSizes() {
#ifdef BENCHMARK_OS_MACOSX
  return GetCacheSizesMacOSX();
#else
  return GetCacheSizesFromKVFS();
#endif
}

int GetNumCPUs() {
#ifdef BENCHMARK_HAS_SYSCTL
  int NumCPU = -1;
  if (!GetSysctl("hw.ncpu", &NumCPU)) {
    fprintf(stderr, "%s\n", strerror(errno));
    std::exit(EXIT_FAILURE);
  }
  return NumCPU;
#elif defined(BENCHMARK_OS_WINDOWS)
  SYSTEM_INFO sysinfo;
  // Use memset as opposed to = {} to avoid GCC missing initializer false
  // positives.
  std::memset(&sysinfo, 0, sizeof(SYSTEM_INFO));
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;  // number of logical
                                        // processors in the current
                                        // group
#else
  int NumCPUs = -1;
  int MaxID = -1;
  std::ifstream f("/proc/cpuinfo");
  if (!f.is_open()) {
    std::cerr << "failed to open /proc/cpuinfo\n";
    return -1;
  }
  const std::string Key = "processor";
  std::string ln;
  while (std::getline(f, ln)) {
    if (ln.empty()) continue;
    size_t SplitIdx = ln.find(':');
    std::string value;
    if (SplitIdx != std::string::npos) value = ln.substr(SplitIdx + 1);
    if (ln.size() >= Key.size() && ln.compare(0, Key.size(), Key) == 0) {
      NumCPUs++;
      if (!value.empty()) {
        int CurID = std::stoi(value);
        MaxID = std::max(CurID, MaxID);
      }
    }
  }
  if (f.bad()) {
    std::cerr << "Failure reading /proc/cpuinfo\n";
    return -1;
  }
  if (!f.eof()) {
    std::cerr << "Failed to read to end of /proc/cpuinfo\n";
    return -1;
  }
  f.close();

  if ((MaxID + 1) != NumCPUs) {
    fprintf(stderr,
            "CPU ID assignments in /proc/cpuinfo seem messed up."
            " This is usually caused by a bad BIOS.\n");
  }
  return NumCPUs;
#endif
}

double GetCPUCyclesPerSecond() {
#if defined BENCHMARK_OS_LINUX || defined BENCHMARK_OS_CYGWIN
  long freq;

  // If the kernel is exporting the tsc frequency use that. There are issues
  // where cpuinfo_max_freq cannot be relied on because the BIOS may be
  // exporintg an invalid p-state (on x86) or p-states may be used to put the
  // processor in a new mode (turbo mode). Essentially, those frequencies
  // cannot always be relied upon. The same reasons apply to /proc/cpuinfo as
  // well.
  if (ReadFromFile("/sys/devices/system/cpu/cpu0/tsc_freq_khz", &freq)
      // If CPU scaling is in effect, we want to use the *maximum* frequency,
      // not whatever CPU speed some random processor happens to be using now.
      || ReadFromFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
                      &freq)) {
    // The value is in kHz (as the file name suggests).  For example, on a
    // 2GHz warpstation, the file contains the value "2000000".
    return freq * 1000.0;
  }

  const double error_value = -1;
  double bogo_clock = error_value;

  std::ifstream f("/proc/cpuinfo");
  if (!f.is_open()) {
    std::cerr << "failed to open /proc/cpuinfo\n";
    return error_value;
  }

  auto startsWithKey = [](std::string const& Value, std::string const& Key) {
    if (Key.size() > Value.size()) return false;
    auto Cmp = [&](char X, char Y) {
      return std::tolower(X) == std::tolower(Y);
    };
    return std::equal(Key.begin(), Key.end(), Value.begin(), Cmp);
  };

  std::string ln;
  while (std::getline(f, ln)) {
    if (ln.empty()) continue;
    size_t SplitIdx = ln.find(':');
    std::string value;
    if (SplitIdx != std::string::npos) value = ln.substr(SplitIdx + 1);
    // When parsing the "cpu MHz" and "bogomips" (fallback) entries, we only
    // accept postive values. Some environments (virtual machines) report zero,
    // which would cause infinite looping in WallTime_Init.
    if (startsWithKey(ln, "cpu MHz")) {
      if (!value.empty()) {
        double cycles_per_second = std::stod(value) * 1000000.0;
        if (cycles_per_second > 0) return cycles_per_second;
      }
    } else if (startsWithKey(ln, "bogomips")) {
      if (!value.empty()) {
        bogo_clock = std::stod(value) * 1000000.0;
        if (bogo_clock < 0.0) bogo_clock = error_value;
      }
    }
  }
  if (f.bad()) {
    std::cerr << "Failure reading /proc/cpuinfo\n";
    return error_value;
  }
  if (!f.eof()) {
    std::cerr << "Failed to read to end of /proc/cpuinfo\n";
    return error_value;
  }
  f.close();

  // If we didn't find anything better, we'll use bogomips, but
  // we're not happy about it.
  return bogo_clock;

#elif defined BENCHMARK_HAS_SYSCTL
  constexpr bool IsBSD =
#if defined(BENCHMARK_OS_FREEBSD) || defined(BENCHMARK_OS_OPENBSD)
      true;
#else
      false;
#endif

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
  std::string FreqStr = IsBSD ? "machdep.tsc_freq" : "hw.cpufrequency";
  unsigned long long hz = 0;
  if (!GetSysctl(FreqStr, &hz)) {
    fprintf(stderr, "Unable to determine clock rate from sysctl: %s: %s\n",
            FreqStr.c_str(), strerror(errno));
  } else {
    info.cycles_per_second = hz;
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
#endif
  // If we've fallen through, attempt to roughly estimate the CPU clock rate.
  const int estimate_time_ms = 1000;
  const auto start_ticks = cycleclock::Now();
  SleepForMilliseconds(estimate_time_ms);
  return static_cast<double>(cycleclock::Now() - start_ticks);
}

CPUInfo& InitializeCPUInfo(CPUInfo& info) {
  info.num_cpus = GetNumCPUs();
  info.caches = GetCacheSizes();
  info.cycles_per_second = GetCPUCyclesPerSecond();
  info.scaling_enabled = CpuScalingEnabled(info.num_cpus);
  return info;
}

}  // end namespace

const CPUInfo& CPUInfo::Get() {
  static const CPUInfo& inited_info = InitializeCPUInfo(GetUninitialized());
  return inited_info;
}

CPUInfo& CPUInfo::GetUninitialized() {
  static CPUInfo info;
  return info;
}

}  // end namespace benchmark
