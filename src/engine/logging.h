#include <iostream>

#include "absl/flags/flag.h"
#include "absl/log/log_sink.h"
#include "absl/strings/str_cat.h"

ABSL_FLAG(std::string, log_dir, "/tmp",
          "Path to the directory where logs should be written.");
ABSL_FLAG(int, minloglevel, 3,
          "Minimum log level to log. (0=INFO, 1=WARNING, 2=ERROR, 3=FATAL)");

namespace zebes {

class TerminalLogSink : public absl::LogSink {
public:
  static TerminalLogSink *Get() {
    static TerminalLogSink sink;
    return &sink;
  }
  void Send(const absl::LogEntry &entry) override {
    std::cout << absl::LogSeverityName(entry.log_severity()) << ": "
              << entry.text_message() << std::endl;
  }

private:
  TerminalLogSink() = default;
};

class HudLogSink : public absl::LogSink {
public:
  static HudLogSink *Get() {
    static HudLogSink sink;
    return &sink;
  }
  void Send(const absl::LogEntry &entry) override {
    absl::StrAppend(&log_, entry.text_message_with_prefix_and_newline());
  }
  const std::string *get_log() { return &log_; }

private:
  HudLogSink() = default;
  std::string log_;
};

} // namespace zebes