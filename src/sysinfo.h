#ifndef BENCHMARK_SYSINFO_H_
#define BENCHMARK_SYSINFO_H_

#include <iostream>
#include <ostream>

#include "benchmark/benchmark.h"

namespace benchmark {

namespace sysctl_util {
enum class ValueKind { String, Integer, Struct };

template <ValueKind VK>
struct ValueHelper;

template <>
struct ValueHelper<ValueKind::String> {
  using type = std::string;
  std::string Parse(std::string in) { return in; }
};

template <>
struct ValueHelper<ValueKind::Integer> {
  using type = std::string;
  int Parse(std::string in) { return std::stoi(in); }
};

template <>
struct ValueHelper<ValueKind::Struct> {
  using type = std::string;
  std::vector<std::string> Parse(std::string in) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    for (std::string s; iss >> s;) result.push_back(s);
    return result;
  }
};

template <class Tp>
class SysCtlResult {
  Tp value;
  bool has_error;

 public:
  SysCtlResult() : has_error(true) {}
  SysCtlResult(Tp v) : value(std::move(v)), has_error(false) {}

  bool isInvalid() { return has_error; }
  Tp const& get() const {
    CHECK(!has_error);
    return value;
  }
  Tp& get() {
    CHECK(!has_error);
    return value;
  }
};

template <ValueKind VK>
SysCtlResult<typename ValueHelper<VK>::type> SysCtlRead(
    std::string const& Name) {
  std::string in;
  if (!SysCtlReadRaw(Name, &in)) return {};
  return {ValueHelper<VK>::Parse(in)};
}
}  // namespace sysctl_util

}  // namespace benchmark

#endif
