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
#include "utility.h"

namespace benchmark {
namespace {

std::ostream*& GetOutputStreamImp() {
  static std::ostream* Out = &std::cout;
  return Out;
}

std::ostream*& GetErrorStreamImp() {
  static std::ostream* Err = &std::cerr;
  return Err;
}

}  // end namespace

std::ostream* SetOutputStream(std::ostream* In) {
  if (In == nullptr) In = &std::cout;
  std::ostream*& Out = GetOutputStreamImp();
  std::ostream* Last = Out;
  Out = In;
  return Last;
}

std::ostream& GetOutputStream() { return *GetOutputStreamImp(); }

std::ostream* SetErrorStream(std::ostream* In) {
  if (In == nullptr) In = &std::cerr;
  std::ostream*& Out = GetErrorStreamImp();
  std::ostream* Last = Out;
  Out = In;
  return Last;
}

std::ostream& GetErrorStream() { return *GetErrorStreamImp(); }

static void FlushStreams() {
  std::flush(GetOutputStream());
  std::flush(GetErrorStream());
}

void PrintBasicContext(std::ostream* out, JSON const& context) {
  CHECK(out) << "cannot be null";
  auto &Out = *out;

  Out << LocalDateTimeString() << "\n";

  JSON info = context.at("cpu_info");
  int num_cpus = info.get_at<int>("num_cpus");
  Out << "Run on (" << num_cpus << " X "
      << (info.get_at<double>("cycles_per_second") / 1000000.0) << " MHz CPU "
      << ((info.get_at<int>("num_cpus") > 1) ? "s" : "") << ")\n";
  std::vector<CPUInfo::CacheInfo> caches = info.at("caches");
  if (caches.size() != 0) {
    Out << "CPU Caches:\n";
    for (auto &CInfo : caches) {
      Out << "  L" << CInfo.level << " " << CInfo.type << " "
          << (CInfo.size / 1000) << "K";
      if (CInfo.num_sharing != 0)
        Out << " (x" << (num_cpus / CInfo.num_sharing) << ")";
      Out << "\n";
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

void ConsoleReporter::Report(JSON const& result) {
  if (result.is_array()) {
    for (auto& R : result) ReportResults(R);
  } else {
    ReportResults(result);
  }
}

void ConsoleReporter::ReportResults(JSON const& result) {
  auto ReportSingle = [&, this](const JSON run) {
    // print the header:
    // --- if none was printed yet
    bool print_header = !printed_header_;
    // --- or if the format is tabular and this run
    //     has different fields from the prev header
    UserCounters counters;
    if (output_options_ & OO_Tabular && run.count("user_data") != 0) {
      auto data = run.at("user_data");
      for (auto It = data.begin(); It != data.end(); ++It) {
        if (It.value().value("kind", "") != "counter") continue;
        if (It.key() == "bytes_per_second" || It.key() == "items_per_second")
          continue;
        counters[It.key()] = It.value().get<Counter>();
      }
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
  std::string Kind = result.at("kind");
  if (Kind == "comparison") {
    ReportSingle(result);
  } else {
    JSON runs = result.at("runs");
    if (runs.size() == 1 || !result.at("report_aggregates_only").get<bool>()) {
      for (JSON R : runs) ReportSingle(R);
    }
    JSON stats = result.at("stats");
    for (JSON R : stats) ReportSingle(R);
  }
  FlushStreams();
}

static void PrintNormalRun(std::ostream& Out, PrinterFn* printer,
                           LogColor name_color,
                           ConsoleReporter::OutputOptions output_options,
                           size_t name_field_width, const JSON& result) {
  std::string Name = result.at("name");
  // std::string Kind = result.at("kind");
  printer(Out, name_color, "%-*s ", name_field_width, Name.c_str());
  if (result.get_at<std::string>("kind") == "error") {
    printer(Out, COLOR_RED, "ERROR OCCURRED: \'%s\'",
            result.get_at<std::string>("error_message").c_str());
    printer(Out, COLOR_DEFAULT, "\n");
    return;
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

  if (result.count("user_data") != 0) {
    JSON ucounters = result.at("user_data");
    for (auto It = ucounters.begin(); It != ucounters.end(); ++It) {
      std::string Key = It.key();
      if (Key == "items_per_second" || Key == "bytes_per_second") continue;
      if (It.value().value("kind", "") != "counter") continue;
      Counter C = It.value();

      auto const& s = HumanReadableNumber(C.value, 1000);
      if (output_options & ConsoleReporter::OO_Tabular) {
        if (C.flags & Counter::kIsRate) {
          printer(Out, COLOR_DEFAULT, " %8s/s", s.c_str());
        } else {
          printer(Out, COLOR_DEFAULT, " %10s", s.c_str());
        }
      } else {
        const char* unit = (C.flags & Counter::kIsRate) ? "/s" : "";
        printer(Out, COLOR_DEFAULT, " %s=%s%s", Key.c_str(), s.c_str(), unit);
      }
    }
    if (ucounters.count("bytes_per_second") != 0) {
      Counter C = ucounters.at("bytes_per_second");
      std::string rate = StrCat(" ", HumanReadableNumber(C.value), "B/s");
      printer(Out, COLOR_DEFAULT, " %*s", 13, rate.c_str());
    }
    if (ucounters.count("items_per_second") != 0) {
      Counter C = ucounters.at("items_per_second");
      std::string items = StrCat(" ", HumanReadableNumber(C.value), " items/s");
      printer(Out, COLOR_DEFAULT, " %*s", 18, items.c_str());
    }
  }

  std::string label = result.value("label", std::string{});
  if (!label.empty()) {
    printer(Out, COLOR_DEFAULT, " %s", label.c_str());
  }
}

static void PrintComplexityRun(std::ostream& Out, PrinterFn* printer,
                               const JSON& result, size_t name_field_width) {
  std::string Name = result.at("name");

  std::string BigOName = Name + "_BigO";
  printer(Out, COLOR_BLUE, "%-*s ", name_field_width, BigOName.c_str());

  JSON BigO = result.at("big_o");
  std::string big_o = result["complexity_string"];
  printer(Out, COLOR_YELLOW, "%10.2f %s %10.2f %s ",
          BigO.get_at<double>("real_time_coefficient"), big_o.c_str(),
          BigO.get_at<double>("cpu_time_coefficient"), big_o.c_str());

  printer(Out, COLOR_DEFAULT, "\n");
  std::string RMSName = Name + "_RMS";
  printer(Out, COLOR_BLUE, "%-*s ", name_field_width, RMSName.c_str());

  JSON RMS = result.at("rms");
  printer(Out, COLOR_YELLOW, "%10.0f %% %10.0f %% ",
          RMS.get_at<double>("real_time") * 100.0,
          RMS.get_at<double>("cpu_time") * 100.0);
}

static void PrintComparisonRun(std::ostream& Out, PrinterFn* printer,
                               ConsoleReporter::OutputOptions output_options,
                               size_t name_field_width, const JSON& result) {
  name_field_width = std::max(std::strlen("Comparison:"), name_field_width);
  PrintNormalRun(Out, printer, COLOR_YELLOW, output_options, name_field_width,
                 GetRunOrMeanStat(result.at("old_result")));
  printer(Out, COLOR_DEFAULT, "\n");
  PrintNormalRun(Out, printer, COLOR_YELLOW, output_options, name_field_width,
                 GetRunOrMeanStat(result.at("new_result")));
  printer(Out, COLOR_DEFAULT, "\n");
  JSON compare = result.at("comparison");
  const double real_time = compare.at("real_iteration_time");
  const double cpu_time = compare.at("cpu_iteration_time");

  printer(Out, COLOR_BLUE, "%-*s", name_field_width, "Comparison: ");

  // const double time_unit_mul = result.at("time_unit_multiplier");
  // FIXME:const char* timeLabel = GetTimeUnitString(result.time_unit);
  auto PrintPercent = [&](double P) {
    LogColor color = COLOR_CYAN;
    if (P > 0.05)
      color = COLOR_RED;
    else if (P > -0.07)
      color = COLOR_WHITE;
    volatile double Rounded = P;
    if (Rounded >= 1.0)
      printer(Out, color, " %10.2f x", Rounded);
    else
      printer(Out, color, " %10.2f %%", Rounded * 100.0);
  };
  PrintPercent(real_time);
  PrintPercent(cpu_time);

  // printer(Out, COLOR_YELLOW, "%10.2f %% %10.2f %% ", real_time, cpu_time);
}
void ConsoleReporter::PrintRunData(const JSON& result) {
  auto& Out = GetOutputStream();
  PrinterFn* printer =
      (output_options_ & OO_Color) ? (PrinterFn*)ColorPrintf : IgnoreColorPrint;

  std::string Name = result.at("name");
  std::string Kind = result.at("kind");

  if (Kind == "normal" || Kind == "error") {
    PrintNormalRun(Out, printer, COLOR_GREEN, output_options_,
                   name_field_width_, result);
  } else if (Kind == "comparison") {
    PrintComparisonRun(Out, printer, output_options_, name_field_width_,
                       result);
  } else if (Kind == "complexity") {
    PrintComplexityRun(Out, printer, result, name_field_width_);
  } else if (Kind == "statistic") {
    PrintNormalRun(Out, printer, COLOR_GREEN, output_options_,
                   name_field_width_, result);
  } else {
    // FIXME: Do something
    std::cerr << "Unknown thing!" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  printer(Out, COLOR_DEFAULT, "\n");
}

void ReportResults(JSON const& J) {
  if (J.is_array()) {
    assert(J.size() == 1);
    GetGlobalReporter()(CK_Report, J[0]);
  } else {
    GetGlobalReporter()(CK_Report, J);
  }
}

ConsoleReporter::ConsoleReporter()
    : output_options_(OO_Defaults),
      name_field_width_(0),
      prev_counters_(),
      printed_header_(false) {}

void ConsoleReporter::operator()(CallbackKind K, JSON const& J) {
  switch (K) {
    case CK_Initial:
      return Initialize(J);
    case CK_Context:
      PrintBasicContext(&GetErrorStream(), J);
      std::flush(GetErrorStream());
      break;
    case CK_Report:
      return ReportResults(J);
    case CK_Final:
      break;  // nothing to do
  }
}

ConsoleReporter& ConsoleReporter::Get() {
  static ConsoleReporter* reporter = new ConsoleReporter();
  return *reporter;
}

ConsoleReporter& GetGlobalReporter() { return ConsoleReporter::Get(); }

}  // end namespace benchmark
