#ifndef BENCHMARK_SYSINFO_H_
#define BENCHMARK_SYSINFO_H_

#include <vector>
#include "benchmark/benchmark.h"

namespace benchmark {

struct CPUInfo {
  struct CacheInfo {
    std::string type;
    int level;
    int size;
  };

  int num_cpus;
  double cycles_per_second;
  std::vector<CacheInfo> caches;
  bool scaling_enabled;

  static const CPUInfo& Get();

 private:
  CPUInfo();

  friend void to_json(JSON& J, CPUInfo const& D);
  friend void to_json(JSON& J, const CacheInfo& CI);
  friend void from_json(JSON const& J, CacheInfo& CI);

  BENCHMARK_DISALLOW_COPY_AND_ASSIGN(CPUInfo);
};
}  // namespace benchmark

#endif  // BENCHMARK_SYSINFO_H_
