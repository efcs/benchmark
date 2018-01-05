//===---------------------------------------------------------------------===//
// json_test - Unit tests for benchmark/json.h
//===---------------------------------------------------------------------===//

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"


namespace {
enum JSONType {
  JT_Object,
  JT_Array,
  JT_Int,
  JT_Double,
  JT_String,
  JT_Bool,
  JT_None
};
bool CheckJSONType(JSONType Expect, benchmark::json const& Obj) {
  switch (Expect) {
  case JT_Object:
    return Obj.is_object();
  case JT_Array:
    return Obj.is_array();
  case JT_Int:
    return Obj.is_number_integer();
  case JT_Double:
    return Obj.is_number_float();
  case JT_String:
    return Obj.is_string();
  case JT_Bool:
    return Obj.is_boolean();
  case JT_None:
    return true;
  default:
    assert("unreachable" && false);
  }
}
#define EXPECT_JSON_FIELD(Name, Type, Input) \
  ASSERT_EQ(Input.count(Name), 1); \
  EXPECT_TRUE(CheckJSONType(Type, Input.at(Name)))
#
#define EXPECT_JSON_FIELD_VALUE(Name, Type, Value, Input) \
  EXPECT_JSON_FIELD(Name, Type, Input); \
  EXPECT_EQ(Input.at(Name), Value)
#

TEST(JSONTest, BreathingTest) {
  using benchmark::json;
  using namespace benchmark;
  json obj{{"name", "foo"}, {"value", 42}, {"list", {1, 2, 3}}};
  auto expect_json = R"(
  {
    "name": "foo",
    "value": 42,
    "list": [1, 2, 3]
  })"_json;
  EXPECT_EQ(obj, expect_json);
  EXPECT_EQ(obj.at("name"), "foo");
  EXPECT_EQ(obj.at("value"), 42);
  EXPECT_EQ(obj.at("list"), (json::array_t{1, 2, 3}));
  auto list = obj.at("list");
  auto expect_list = expect_json.at("list");
  for (auto It = list.begin(), EIt = expect_list.begin(); It != list.end();
       ++It, ++EIt) {
    EXPECT_EQ(*It, *EIt);
  }
}

TEST(JSONTest, JSONInputTest) {
  using namespace benchmark;
  RegisterBenchmark("test1",
                    [](State& st) {
                      json obj = st.GetInput();
                      assert(!obj.is_null());
                      switch (obj.at("case").get<int>()) {
                        case 1:
                          assert(obj.at("name") == "foo");
                          assert(obj.at("a") == 42);
                          break;
                        case 2:
                          assert(obj.count("name") == 0);
                          assert(obj.at("b") == 101);
                          break;
                        default:
                          assert(false && "in default case");
                      }
                      for (auto _ : st) {
                      }
                    })
      ->WithInput({{"case", 1}, {"name", "foo"}, {"a", 42}})
      ->WithInput({{"case", 2}, {"b", 101}});
  RunSpecifiedBenchmarks();
}


TEST(ContextTest, BasicContext) {
  const benchmark::json& C = benchmark::GetBasicContext();
  EXPECT_EQ(C.count("date"), 1);
  EXPECT_TRUE(C.at("date").is_string());
  EXPECT_EQ(C.count("library_build_type"), 1);
  std::string BT = C.at("library_build_type");
  EXPECT_TRUE(BT == "debug" || BT == "release");
  EXPECT_EQ(C.count("cpu_info"), 1);
  EXPECT_TRUE(C.at("cpu_info").is_object());
  EXPECT_EQ(C.at("cpu_info"), benchmark::GetCPUInfo());
}

TEST(ContextTest, CPUInfo) {
  const benchmark::json& C = benchmark::GetCPUInfo();

  EXPECT_JSON_FIELD("num_cpus", JT_Int, C);
  EXPECT_GE(C.at("num_cpus"), 1);

  EXPECT_JSON_FIELD("scaling_enabled", JT_Bool, C);
  EXPECT_JSON_FIELD("frequency_mhz", JT_Double, C);
  EXPECT_GE(C.at("frequency_mhz").get<double>(), 0.01);
  EXPECT_JSON_FIELD("caches", JT_Array, C);
}

TEST(ContextTest, CacheInfo) {
  const benchmark::json& Info = benchmark::GetCPUInfo();
  EXPECT_JSON_FIELD("caches", JT_Array, Info);
  const auto& Caches = Info.at("caches");
  for (auto& C : Caches) {
    EXPECT_JSON_FIELD("type", JT_String, C);
    EXPECT_JSON_FIELD("level", JT_Int, C);
    EXPECT_JSON_FIELD("size", JT_Int, C);
    EXPECT_JSON_FIELD("num_sharing", JT_Int, C);
  }
}

}  // end namespace
