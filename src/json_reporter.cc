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

#include "benchmark/benchmark.h"
#include "complexity.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>  // for setprecision
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "string_util.h"
#include "timers.h"

namespace benchmark {

static std::string IndentJSONString(std::string const& In, int Num) {
  std::string NewStr;
  std::string Indent(' ', Num);
  for (auto ch : In) {
    NewStr += ch;
    if (ch == '\n') NewStr += Indent;
    if (ch == '\r') std::exit(EXIT_FAILURE);
  }
  return NewStr;
}

bool JSONReporter::ReportContext(const JSON& context) {
  std::ostream& out = GetOutputStream();

  out << "{\n";
  std::string inner_indent(2, ' ');

  // Open context block and print context information.
  out << inner_indent << "\"context\":";

  auto TmpStr = IndentJSONString(context.dump(2), 4);
  CHECK_EQ(TmpStr.back(), '}');
  TmpStr.erase(TmpStr.size() - 1);
  out << TmpStr;

  // Close context block and open the list of benchmarks.
  out << inner_indent << "},\n";
  out << inner_indent << "\"benchmarks\": [\n";
  return true;
}

void JSONReporter::ReportResults(const JSON& result) {
  if (!first_report_) GetOutputStream() << ",\n";
  first_report_ = false;
  GetOutputStream() << IndentJSONString(result.dump(2), 4);
}

void JSONReporter::Finalize() {
  // Close the list of benchmarks and the top level object.
  GetOutputStream() << "\n  ]\n}\n";
}


} // end namespace benchmark
