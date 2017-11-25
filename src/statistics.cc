// Copyright 2016 Ismael Jimenez Martinez. All rights reserved.
// Copyright 2017 Roman Lebedev. All rights reserved.
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

#include "benchmark/benchmark.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <numeric>
#include "check.h"
#include "statistics.h"

namespace benchmark {

auto StatisticsSum = [](const std::vector<double>& v) {
  return std::accumulate(v.begin(), v.end(), 0.0);
};

double StatisticsMean(const std::vector<double>& v) {
  if (v.size() == 0) return 0.0;
  return StatisticsSum(v) * (1.0 / v.size());
}

double StatisticsMedian(const std::vector<double>& v) {
  if (v.size() < 3) return StatisticsMean(v);
  std::vector<double> partial;
  // we need roundDown(count/2)+1 slots
  partial.resize(1 + (v.size() / 2));
  std::partial_sort_copy(v.begin(), v.end(), partial.begin(), partial.end());
  // did we have odd number of samples?
  // if yes, then the last element of partially-sorted vector is the median
  // it no, then the average of the last two elements is the median
  if(v.size() % 2 == 1)
    return partial.back();
  return (partial[partial.size() - 2] + partial[partial.size() - 1]) / 2.0;
}

// Return the sum of the squares of this sample set
auto SumSquares = [](const std::vector<double>& v) {
  return std::inner_product(v.begin(), v.end(), v.begin(), 0.0);
};

auto Sqr = [](const double dat) { return dat * dat; };
auto Sqrt = [](const double dat) {
  // Avoid NaN due to imprecision in the calculations
  if (dat < 0.0) return 0.0;
  return std::sqrt(dat);
};

double StatisticsStdDev(const std::vector<double>& v) {
  const auto mean = StatisticsMean(v);
  if (v.size() == 0) return mean;

  // Sample standard deviation is undefined for n = 1
  if (v.size() == 1)
    return 0.0;

  const double avg_squares = SumSquares(v) * (1.0 / v.size());
  return Sqrt(v.size() / (v.size() - 1.0) * (avg_squares - Sqr(mean)));
}

std::vector<JSON> ComputeStats(std::vector<JSON> const& reports,
                               std::vector<Statistics> const& stats) {
  std::vector<JSON> results;
  auto error_count =
      std::count_if(reports.begin(), reports.end(), [](JSON const& run) {
        return run.get_at<std::string>("kind") == "error";
      });

  if (reports.size() - error_count < 2) {
    // We don't report aggregated data if there was a single run.
    return results;
  }

  // Accumulators.
  std::vector<double> real_accumulated_time_stat;
  std::vector<double> cpu_accumulated_time_stat;
  std::vector<double> bytes_per_second_stat;
  std::vector<double> items_per_second_stat;

  real_accumulated_time_stat.reserve(reports.size());
  cpu_accumulated_time_stat.reserve(reports.size());
  bytes_per_second_stat.reserve(reports.size());
  items_per_second_stat.reserve(reports.size());

  // All repetitions should be run wNith the same number of iterations so we
  // can take this information from the first benchmark.
  int64_t const run_iterations = reports.front().at("iterations");
  // create stats for user counters
  struct CounterStat {
    Counter c;
    std::vector<double> s;
  };
  std::map< std::string, CounterStat > counter_stats;
  for (JSON const& r : reports) {
    UserCounters UC = r.at("counters");
    for (auto const& cnt : UC) {
      auto it = counter_stats.find(cnt.first);
      if(it == counter_stats.end()) {
        counter_stats.insert({cnt.first, {cnt.second, std::vector<double>{}}});
        it = counter_stats.find(cnt.first);
        it->second.s.reserve(reports.size());
      } else {
        CHECK_EQ(counter_stats[cnt.first].c.flags, cnt.second.flags);
      }
    }
  }

  // Populate the accumulators.
  for (JSON const& run : reports) {
    std::string Name = run.at("name");

    //  CHECK_EQ(reports[0].json_report.get_at<std::string>("name"), Name);
    // CHECK_EQ(run_iterations, run.iterations);
    std::string Kind = run.at("kind");
    if (Kind == "error") continue;
    real_accumulated_time_stat.emplace_back(
        run.get_at<double>("real_accumulated_time"));
    cpu_accumulated_time_stat.emplace_back(
        run.get_at<double>("cpu_accumulated_time"));

    // FIXME: EricWF
    // items_per_second_stat.emplace_back(run.get_at<double>("items_per_second"));
    // bytes_per_second_stat.emplace_back(run.get_at<double>("bytes_per_second"));
    // user counters
    UserCounters UC = run["counters"];
    for (auto const& cnt : UC) {
      auto it = counter_stats.find(cnt.first);
      CHECK_NE(it, counter_stats.end());
      it->second.s.emplace_back(cnt.second);
    }
  }

  // Only add label if it is same for all runs
  auto GetLabel = [](JSON const& R) {
    std::string res;
    if (R.count("label") != 0) res = R.at("label");
    return res;
  };

  std::string report_label = GetLabel(reports[0]);
  for (std::size_t i = 1; i < reports.size(); i++) {
    if (GetLabel(reports[i]) != report_label) {
      report_label = "";
      break;
    }
  }

  for (const auto& Stat : stats) {
    // Get the data from the accumulator to BenchmarkReporter::Run's.
    JSON data = {
        {"name", reports[0].get_at<std::string>("name") + "_" + Stat.name_},
        {"kind", "statistic"},
        {"label", report_label},
        {"iterations", run_iterations},
        {"time_unit", reports[0].get_at<std::string>("time_unit")},
        {"real_accumulated_time", Stat.compute_(real_accumulated_time_stat)},
        {"cpu_accumulated_time", Stat.compute_(cpu_accumulated_time_stat)},
        //{"bytes_per_second", Stat.compute_(bytes_per_second_stat)},
        //{"items_per_second", Stat.compute_(items_per_second_stat)}
    };
    data["real_iteration_time"] =
        data.get_at<double>("real_accumulated_time") / run_iterations;
    data["cpu_iteration_time"] =
        data.get_at<double>("cpu_accumulated_time") / run_iterations;

    // user counters
    std::map<std::string, Counter> CounterStats;

    for(auto const& kv : counter_stats) {
      const auto uc_stat = Stat.compute_(kv.second.s);
      auto c = Counter(uc_stat, counter_stats[kv.first].c.flags);
      CounterStats.emplace(kv.first, c);
    }
    data["counters"] = CounterStats;

    results.push_back(data);
  }

  return results;
}

}  // end namespace benchmark
