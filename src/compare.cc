#include "benchmark/benchmark.h"
#include "check.h"
#include "utility.h"

namespace benchmark {

enum JSONNodeKind { JK_Report, JK_Run, JK_ReportList };

JSONNodeKind ClassifyNode(json const& R1) {
  if (R1.is_array()) {
    if (R1.size() == 0) return JK_ReportList;
    CHECK_EQ(ClassifyNode(R1[0]), JK_Report);
    return JK_ReportList;
  }
  if (R1.is_object() && R1.count("runs") == 1 && R1.count("family") == 1)
    return JK_Report;
  if (R1.is_object() && R1.count("kind") == 1) {
    std::string Kind = R1.at("kind");
    if (Kind == "normal" || Kind == "error" || Kind == "statistic")
      return JK_Run;
  }
  std::cerr << "Unknown json Kind" << std::endl;
  BENCHMARK_UNREACHABLE();
}

double CalculateChange(double Old, double New) {
  if (IsZero(Old) && IsZero(New)) return 0.0;
  if (IsZero(Old)) return (New - Old) / ((Old + New) / 2);
  return (New - Old) / std::abs(Old);
}

json CompareReport(json const& R1, json const& R2) {
  json S1 = GetRunOrMeanStat(R1);
  json S2 = GetRunOrMeanStat(R2);
  std::vector<std::string> ToCompare = {"cpu_iteration_time",
                                        "real_iteration_time"};
  json Res{{"name", R1.at("name").get<std::string>() + "/compare_to/" +
                        R2.at("name").get<std::string>()},
           {"kind", "comparison"},
           {"old_result", R1},
           {"new_result", R2},
           {"comparison", json::object()}};
  for (auto& Key : ToCompare) {
    Res["comparison"][Key] = CalculateChange(S1.at(Key), S2.at(Key));
  }
  return Res;
}

json FindMatchingInstance(json const& Instance, json const& Root) {
  for (auto const& Item : Root) {
    if (Item.at("instance") == Instance) return Item;
  }
  return json{};
}

json CompareResults(json const& R1, json const& R2) {
  JSONNodeKind Kind = ClassifyNode(R1);
  CHECK_EQ(Kind, ClassifyNode(R2));
  if (Kind == JK_Report) return CompareReport(R1, R2);
  if (Kind == JK_ReportList) {
    json Res = json::array();
    for (auto It = R1.begin(); It != R1.end(); ++It) {
      json Val = *It;
      json Match = FindMatchingInstance(Val.at("instance"), R2);
      CHECK(!Match.is_null());
      json Report = CompareReport(Val, Match);
      Res.push_back(Report);
    }
    return Res;
  }
  ((void)R1);
  ((void)R2);
  json res;
  // FIXME
  return res;
}

}  // namespace benchmark
