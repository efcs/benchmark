#include "utility.h"

#include "benchmark/benchmark.h"
#include "internal_macros.h"

namespace benchmark {
namespace internal {

void UseCharPointer(char const volatile*) {}

#ifdef BENCHMARK_HAS_NO_BUILTIN_UNREACHABLE
BENCHMARK_NORETURN void UnreachableImp(const char* FName, int Line) {
  std::cerr << FName << ":" << Line << " executing unreachable code!"
            << std::endl;
  std::abort();
}
#endif

}  // namespace internal

}  // namespace benchmark
