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

#include "reporter.h"
#include "benchmark/benchmark.h"

#include <cstdlib>

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "colorprint.h"
#include "counter.h"
#include "internal_macros.h"
#include "string_util.h"
#include "sysinfo.h"

#include "benchmark_commandline.h"
#include "timers.h"

namespace benchmark {

static void FlushStreams() {
  std::flush(GetOutputStream());
  std::flush(GetErrorStream());
}

void PrintBasicContext(std::ostream* out, JSON const& context) {
  CHECK(out) << "cannot be null";
  auto &Out = *out;

  Out << LocalDateTimeString() << "\n";

  JSON info = context.at("cpu_info");
  Out << "Run on (" << info.get_at<int>("num_cpus") << " X "
      << (info.get_at<double>("cycles_per_second") / 1000000.0) << " MHz CPU "
      << ((info.get_at<int>("num_cpus") > 1) ? "s" : "") << ")\n";
  std::vector<CPUInfo::CacheInfo> caches = info.at("caches");
  if (caches.size() != 0) {
    Out << "CPU Caches:\n";
    for (auto &CInfo : caches) {
      Out << "  L" << CInfo.level << " " << CInfo.type << " "
          << (CInfo.size / 1000) << "K\n";
    }
  }

  if (info.get_at<bool>("scaling_enabled")) {
    Out << "***WARNING*** CPU scaling is enabled, the benchmark "
           "real time measurements may be noisy and will incur extra "
           "overhead.\n";
  }

#ifndef NDEBUG
  Out << "***WARNING*** Library was built as DEBUG. Timings may be "
         "affected.\n";
#endif
}

void ConsoleReporter::Initialize(const JSON& info) {
  name_field_width_ = info.at("name_field_width");
  printed_header_ = false;
  prev_counters_.clear();
#ifdef BENCHMARK_OS_WINDOWS
  if ((output_options_ & OO_Color) && &std::cout != &GetOutputStream()) {
    GetErrorStream()
        << "Color printing is only supported for stdout on windows."
           " Disabling color printing\n";
    output_options_ = static_cast<OutputOptions>(output_options_ & ~OO_Color);
  }
#endif
}

void ConsoleReporter::PrintHeader(const JSON& run) {
  std::string str =
      FormatString("%-*s %13s %13s %10s", static_cast<int>(name_field_width_),
                   "Benchmark", "Time", "CPU", "Iterations");

  if (run.count("counters") != 0 && !run.at("counters").empty()) {
    UserCounters counters = run.at("counters");
    if (output_options_ & OO_Tabular) {
      for (auto const& c : counters) {
        str += FormatString(" %10s", c.first.c_str());
      }
    } else {
      str += " UserCounters...";
    }
  }
  str += "\n";
  std::string line = std::string(str.length(), '-');
  GetOutputStream() << line << "\n" << str << line << "\n";
}

static void IgnoreColorPrint(std::ostream& out, LogColor, const char* fmt,
                             ...) {
  va_list args;
  va_start(args, fmt);
  out << FormatString(fmt, args);
  va_end(args);
}

typedef void(PrinterFn)(std::ostream&, LogColor, const char*, ...);

void ConsoleReporter::ReportResults(JSON const& result) {
  auto ReportSingle = [&, this](const JSON run) {
    // print the header:
    // --- if none was printed yet
    bool print_header = !printed_header_;
    // --- or if the format is tabular and this run
    //     has different fields from the prev header
    UserCounters counters;
    if (output_options_ & OO_Tabular && run.count("counters") != 0) {
      counters = run.at("counters").get<UserCounters>();
      print_header |= !internal::SameNames(counters, prev_counters_);
    }
    if (print_header) {
      printed_header_ = true;
      prev_counters_ = counters;
      PrintHeader(run);
    }
    // As an alternative to printing the headers like this, we could sort
    // the benchmarks by header and then print. But this would require
    // waiting for the full results before printing, or printing twice.
    PrintRunData(run);
  };
  JSON runs = result.at("runs");
  if (runs.size() == 1 || !result.at("report_aggregates_only").get<bool>()) {
    for (JSON R : runs) ReportSingle(R);
  }
  JSON stats = result.at("stats");
  for (JSON R : stats) ReportSingle(R);
  FlushStreams();
}

static void PrintNormalRun(std::ostream& Out, PrinterFn* printer,
                           ConsoleReporter::OutputOptions output_options,
                           const JSON& result) {
  std::string Name = result.at("name");
  std::string Kind = result.at("kind");

  if (result.get_at<std::string>("kind") == "error") {
    printer(Out, COLOR_RED, "ERROR OCCURRED: \'%s\'",
            result.get_at<std::string>("error_message").c_str());
    printer(Out, COLOR_DEFAULT, "\n");
    return;
  }

  // Format bytes per second
  if (result.count("bytes_per_second") != 0) {
    std::string rate = StrCat(
        " ", HumanReadableNumber(result.get_at<double>("bytes_per_second")),
        "B/s");
    printer(Out, COLOR_DEFAULT, " %*s", 13, rate.c_str());
  }

  // Format items per second
  if (result.count("items_per_second") != 0) {
    std::string items = StrCat(
        " ", HumanReadableNumber(result.get_at<double>("items_per_second")),
        " items/s");
    printer(Out, COLOR_DEFAULT, " %*s", 18, items.c_str());
  }

  const double real_time = result.at("real_iteration_time");
  const double cpu_time = result.at("cpu_iteration_time");
  const std::string timeLabel = result.at("time_unit");
  // const double time_unit_mul = result.at("time_unit_multiplier");
  // FIXME:const char* timeLabel = GetTimeUnitString(result.time_unit);
  printer(Out, COLOR_YELLOW, "%10.0f %s %10.0f %s ", real_time,
          timeLabel.c_str(), cpu_time, timeLabel.c_str());

  if (result.count("iterations") != 0) {
    printer(Out, COLOR_CYAN, "%10lld", result.get_at<int64_t>("iterations"));
  }
  if (result.count("label") != 0) {
    printer(Out, COLOR_DEFAULT, " %s",
            result.get_at<std::string>("label").c_str());
  }

  if (result.at("counters") != 0) {
    UserCounters counters = result.at("counters");
    for (auto& c : counters) {
      auto const& s = HumanReadableNumber(c.second.value, 1000);
      if (output_options & ConsoleReporter::OO_Tabular) {
        if (c.second.flags & Counter::kIsRate) {
          printer(Out, COLOR_DEFAULT, " %8s/s", s.c_str());
        } else {
          printer(Out, COLOR_DEFAULT, " %10s", s.c_str());
        }
      } else {
        const char* unit = (c.second.flags & Counter::kIsRate) ? "/s" : "";
        printer(Out, COLOR_DEFAULT, " %s=%s%s", c.first.c_str(), s.c_str(),
                unit);
      }
    }
  }
}

static void PrintComplexityRun(std::ostream& Out, PrinterFn* printer,
                               const JSON& result) {
  std::string Name = result.at("name");
  std::string Kind = result.at("kind");

  JSON BigO = result.at("big_o");
  JSON RMS = result.at("rms");

  std::string big_o = result["complexity_string"];
  printer(Out, COLOR_YELLOW, "%10.2f %s %10.2f %s ",
          BigO.get_at<double>("real_time_coefficient"), big_o.c_str(),
          BigO.get_at<double>("cpu_time_coefficient"), big_o.c_str());
  printer(Out, COLOR_YELLOW, "%10.0f %% %10.0f %% ",
          RMS.get_at<double>("real_time") * 100,
          RMS.get_at<double>("cpu_time") * 100);
}

void ConsoleReporter::PrintRunData(const JSON& result) {
  auto& Out = GetOutputStream();
  PrinterFn* printer =
      (output_options_ & OO_Color) ? (PrinterFn*)ColorPrintf : IgnoreColorPrint;

  std::string Name = result.at("name");
  std::string Kind = result.at("kind");

  auto name_color = (Kind == "complexity") ? COLOR_BLUE : COLOR_GREEN;
  printer(Out, name_color, "%-*s ", name_field_width_, Name.c_str());
  if (Kind == "normal" || Kind == "error") {
    PrintNormalRun(Out, printer, output_options_, result);
  } else if (Kind == "complexity") {
    PrintComplexityRun(Out, printer, result);
  } else if (Kind == "statistic") {
    PrintNormalRun(Out, printer, output_options_, result);
  } else {
    // FIXME: Do something
    std::cerr << "Unknown thing!" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  printer(Out, COLOR_DEFAULT, "\n");
}

ConsoleReporter::OutputOptions ConsoleReporter::GetCommandLineOutputOptions(
    bool force_no_color) {
  int output_opts = ConsoleReporter::OO_Defaults;
  if ((FLAGS_benchmark_color == "auto" && IsColorTerminal()) ||
      internal::IsTruthyFlagValue(FLAGS_benchmark_color)) {
    output_opts |= ConsoleReporter::OO_Color;
  } else {
    output_opts &= ~ConsoleReporter::OO_Color;
  }
  if (force_no_color) {
    output_opts &= ~ConsoleReporter::OO_Color;
  }
  if (FLAGS_benchmark_counters_tabular) {
    output_opts |= ConsoleReporter::OO_Tabular;
  } else {
    output_opts &= ~ConsoleReporter::OO_Tabular;
  }
  return static_cast<ConsoleReporter::OutputOptions>(output_opts);
}

ConsoleReporter& GetGlobalConsoleReporter() {
  static ConsoleReporter CR = ConsoleReporter::GetCommandLineReporter();
  return CR;
}

void ReportResults(JSON const& J) {
  if (J.is_array()) {
    assert(J.size() == 1);
    GetGlobalConsoleReporter()(CK_Report, J[0]);
  } else {
    GetGlobalConsoleReporter()(CK_Report, J);
  }
}

}  // end namespace benchmark
