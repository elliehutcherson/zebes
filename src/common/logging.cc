#include "logging.h"

#include <iostream>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"

ABSL_FLAG(std::string, log_dir, "/tmp",
          "Path to the directory where logs should be written.");
ABSL_FLAG(int, minloglevel, 3,
          "Minimum log level to log. (0=INFO, 1=WARNING, 2=ERROR, 3=FATAL)");

namespace zebes {

TerminalLogSink *TerminalLogSink::Get() {
  static TerminalLogSink sink;
  return &sink;
}

void TerminalLogSink::Send(const absl::LogEntry &entry) {
  std::cout << absl::LogSeverityName(entry.log_severity()) << ": "
            << entry.text_message() << std::endl;
}

HudLogSink *HudLogSink::Get() {
  static HudLogSink sink;
  return &sink;
}

void HudLogSink::Send(const absl::LogEntry &entry) {
  absl::StrAppend(&log_, entry.text_message_with_prefix_and_newline());
}

std::string *HudLogSink::log() { return &log_; }

} // namespace zebes
