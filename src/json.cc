#include "benchmark/benchmark.h"

namespace benchmark {
namespace {
template <class EnumT>
using EnumList = std::vector<std::pair<std::string, EnumT>>;

template <class EnumT>
bool EnumToJson(json *Out, EnumT Value, EnumList<EnumT> const& List) {
  for (auto& KV : List) {
    if (KV.second == Value) {
      *Out = KV.first;
      return true;
    }
  }
  return false;
}

template <class EnumT>
bool JsonToEnum(EnumT *Out, const json& enum_obj, EnumList<EnumT> const& List) {
  assert(enum_obj.is_string());
  std::string enum_str = enum_obj;
  for (auto& KV : List) {
    if (KV.first == enum_str) {
      *Out = KV.second;
      return true;
    }
  }
  return false;
}


static const EnumList<JsonObjectType> JsonObjectTypeValues = {
    {"counter", JT_Counter}
};

static const EnumList<Counter::Flags> CounterFlagValues =
    {
        {"default", Counter::Flags::kDefaults},
        {"rate", Counter::Flags ::kIsRate},
        {"average_threads", Counter::Flags::kAvgThreads},
        {"average_threads_rate", Counter::Flags::kAvgThreadsRate}
    };
static const EnumList<TimeUnit> TimeUnitValues = {
    {"nanosecond", kNanosecond},
    {"microsecond", kMicrosecond},
    {"millisecond", kMillisecond}
};
static const EnumList<BigO> BigOValues = {
    {"none", oNone},
    {"O1", o1},
    {"oN", oN},
    {"oNSquared", oNSquared},
    {"oNCubed", oNCubed},
    {"oLogN", oLogN},
    {"oNLogN", oNLogN},
    {"oAuto", oAuto},
    {"oLambda", oLambda}
};
} // end namespace

#define DEFINE_JSON_ENUM_CONVERSION(EnumList) \
void to_json(json& output, decltype(EnumList)::value_type::second_type const& input) { \
  bool res = EnumToJson(&output, input, EnumList); \
  ((void)res); \
  assert(res && "failed to convert"); \
} \
void from_json(const json& input, decltype(EnumList)::value_type::second_type& output) { \
  bool res = JsonToEnum(&output, input, EnumList); \
  ((void)res); \
  assert(res && "failed to convert"); \
}
#
DEFINE_JSON_ENUM_CONVERSION(JsonObjectTypeValues)
DEFINE_JSON_ENUM_CONVERSION(CounterFlagValues)
DEFINE_JSON_ENUM_CONVERSION(TimeUnitValues)
DEFINE_JSON_ENUM_CONVERSION(BigOValues)



void to_json(json& output, Counter const& input) {
  json result{
      {"type", JT_Counter},
      {"value", input.value},
      {"flags", input.flags}
  };
  output = result;
}

void from_json(const json& input, Counter& output) {
  Counter result;
  assert(input.count("type") == 1 && input.at("type") == "counter");
  result.value = input.at("value");
  result.flags = input.at("flags");
  output = result;
}


namespace internal {
bool CheckJsonType(json const& input, JsonObjectType Expect) {
  JsonObjectType Type;
  bool Found = input.count("type") == 1 &&
      JsonToEnum(&Type, input.at("type"), JsonObjectTypeValues);
  return Found && Type == Expect;
}
} // end namespace internal

} // namespace benchmark
