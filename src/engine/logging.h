#pragma once

#include "absl/log/log_sink.h"

namespace zebes {

class TerminalLogSink : public absl::LogSink {
public:
  static TerminalLogSink *Get();
  void Send(const absl::LogEntry &entry) override;

private:
  TerminalLogSink() = default;
};

class HudLogSink : public absl::LogSink {
public:
  static HudLogSink *Get();

  void Send(const absl::LogEntry &entry) override;

  std::string* log();

private:
  HudLogSink() = default;
  std::string log_;
};

} // namespace zebes
