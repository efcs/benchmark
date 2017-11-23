// Copyright 2016 Ismael Jimenez Martinez. All rights reserved.
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

// Source project : https://github.com/ismaelJimenez/cpp.leastsq
// Adapted to be used with google benchmark

#include "benchmark/benchmark.h"

#include <algorithm>
#include <cmath>
#include "check.h"
#include "complexity.h"

namespace benchmark {

// Internal function to calculate the different scalability forms
BigOFunc* FittingCurve(BigO complexity) {
  switch (complexity) {
    case oN:
      return [](int n) -> double { return n; };
    case oNSquared:
      return [](int n) -> double { return std::pow(n, 2); };
    case oNCubed:
      return [](int n) -> double { return std::pow(n, 3); };
    case oLogN:
      return [](int n) { return log2(n); };
    case oNLogN:
      return [](int n) { return n * log2(n); };
    case o1:
    default:
      return [](int) { return 1.0; };
  }
}

// Function to return an string for the calculated complexity
std::string GetBigOString(BigO complexity) {
  switch (complexity) {
    case oN:
      return "N";
    case oNSquared:
      return "N^2";
    case oNCubed:
      return "N^3";
    case oLogN:
      return "lgN";
    case oNLogN:
      return "NlgN";
    case o1:
      return "(1)";
    default:
      return "f(N)";
  }
}

// Find the coefficient for the high-order term in the running time, by
// minimizing the sum of squares of relative error, for the fitting curve
// given by the lambda expresion.
//   - n             : Vector containing the size of the benchmark tests.
//   - time          : Vector containing the times for the benchmark tests.
//   - fitting_curve : lambda expresion (e.g. [](int n) {return n; };).

// For a deeper explanation on the algorithm logic, look the README file at
// http://github.com/ismaelJimenez/Minimal-Cpp-Least-Squared-Fit

LeastSq MinimalLeastSq(const std::vector<int>& n,
                       const std::vector<double>& time,
                       BigOFunc* fitting_curve) {
  double sigma_gn = 0.0;
  double sigma_gn_squared = 0.0;
  double sigma_time = 0.0;
  double sigma_time_gn = 0.0;

  // Calculate least square fitting parameter
  for (size_t i = 0; i < n.size(); ++i) {
    double gn_i = fitting_curve(n[i]);
    sigma_gn += gn_i;
    sigma_gn_squared += gn_i * gn_i;
    sigma_time += time[i];
    sigma_time_gn += time[i] * gn_i;
  }

  LeastSq result;
  result.complexity = oLambda;

  // Calculate complexity.
  result.coef = sigma_time_gn / sigma_gn_squared;

  // Calculate RMS
  double rms = 0.0;
  for (size_t i = 0; i < n.size(); ++i) {
    double fit = result.coef * fitting_curve(n[i]);
    rms += pow((time[i] - fit), 2);
  }

  // Normalized RMS by the mean of the observed values
  double mean = sigma_time / n.size();
  result.rms = sqrt(rms / n.size()) / mean;

  return result;
}

// Find the coefficient for the high-order term in the running time, by
// minimizing the sum of squares of relative error.
//   - n          : Vector containing the size of the benchmark tests.
//   - time       : Vector containing the times for the benchmark tests.
//   - complexity : If different than oAuto, the fitting curve will stick to
//                  this one. If it is oAuto, it will be calculated the best
//                  fitting curve.
LeastSq MinimalLeastSq(const std::vector<int>& n,
                       const std::vector<double>& time, const BigO complexity) {
  CHECK_EQ(n.size(), time.size());
  CHECK_GE(n.size(), 2);  // Do not compute fitting curve is less than two
                          // benchmark runs are given
  CHECK_NE(complexity, oNone);

  LeastSq best_fit;

  if (complexity == oAuto) {
    std::vector<BigO> fit_curves = {oLogN, oN, oNLogN, oNSquared, oNCubed};

    // Take o1 as default best fitting curve
    best_fit = MinimalLeastSq(n, time, FittingCurve(o1));
    best_fit.complexity = o1;

    // Compute all possible fitting curves and stick to the best one
    for (const auto& fit : fit_curves) {
      LeastSq current_fit = MinimalLeastSq(n, time, FittingCurve(fit));
      if (current_fit.rms < best_fit.rms) {
        best_fit = current_fit;
        best_fit.complexity = fit;
      }
    }
  } else {
    best_fit = MinimalLeastSq(n, time, FittingCurve(complexity));
    best_fit.complexity = complexity;
  }

  return best_fit;
}

JSON ComputeBigO(const internal::BenchmarkInstance& instance,
                 std::vector<JSON> const& reports) {
  if (reports.size() < 2) return JSON{};

  // Accumulators.
  std::vector<int> n;
  std::vector<double> real_time;
  std::vector<double> cpu_time;

  // Populate the accumulators.
  for (JSON const& Run : reports) {
    // CHECK_GT(run.complexity_n, 0) << "Did you forget to call
    // SetComplexityN?";
    n.push_back(Run.at("complexity_n").get<int>());
    auto iters = Run.at("iterations").get<int64_t>();
    real_time.push_back(Run.at("real_accumulated_time").get<double>() / iters);
    cpu_time.push_back(Run.at("cpu_accumulated_time").get<double>() / iters);
  }

  LeastSq result_cpu;
  LeastSq result_real;

  if (instance.info->complexity == oLambda) {
    result_cpu = MinimalLeastSq(n, cpu_time, instance.info->complexity_lambda);
    result_real =
        MinimalLeastSq(n, real_time, instance.info->complexity_lambda);
  } else {
    result_cpu = MinimalLeastSq(n, cpu_time, instance.info->complexity);
    result_real = MinimalLeastSq(n, real_time, result_cpu.complexity);
  }
  std::string benchmark_name = instance.name.substr(0, instance.name.find('/'));

  // All the time results are reported after being multiplied by the
  // time unit multiplier. But since RMS is a relative quantity it
  // should not be multiplied at all. So, here, we _divide_ it by the
  // multiplier so that when it is multiplied later the result is the
  // correct one.
  double multiplier = GetTimeUnitMultiplier(instance.info->time_unit);

  JSON json_report = {
      {"name", benchmark_name},
      {"kind", "complexity"},
      {"complexity", result_cpu.complexity},
      {"complexity_string", GetBigOString(result_cpu.complexity)},
      {"big_o",
       {{"real_time_coefficient", result_real.coef},
        {"cpu_time_coefficient", result_cpu.coef}}},
      {"rms",
       {
           {"real_time", result_real.rms / multiplier},
           {"cpu_time", result_cpu.rms / multiplier},
       }}};
  return json_report;
}

}  // end namespace benchmark
