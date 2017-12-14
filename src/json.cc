#include "benchmark/benchmark.h"

namespace benchmark {

// Externally instantiate the JSON type so that users aren't forced to
// repeatedly recompile it.
template class basic_json<>;

}  // namespace benchmark
