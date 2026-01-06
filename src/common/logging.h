#pragma once

#include "absl/log/log_sink.h"

namespace zebes {

class TerminalLogSink : public absl::LogSink {
 public:
  static TerminalLogSink* Get();
  void Send(const absl::LogEntry& entry) override;

 private:
  TerminalLogSink() = default;
};

}  // namespace zebes
