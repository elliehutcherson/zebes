#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace zebes {

struct CodexAppServerProcessConfig {
  std::string executable = "codex";
  size_t maximum_protocol_line_bytes = 96 * 1024 * 1024;
  size_t maximum_pending_write_bytes = 1024 * 1024;
  size_t maximum_diagnostic_bytes = 64 * 1024;
};

// A non-blocking JSONL connection to one Codex App Server process.
//
// SendLine queues a complete message and Poll advances pending writes, drains
// stdout and stderr, and returns every complete response currently available.
// Neither method waits for the child. Stop owns process-group termination and
// is idempotent; destruction also stops the child and removes its private
// working directory.
class CodexAppServerTransport {
 public:
  virtual ~CodexAppServerTransport() = default;

  virtual absl::Status Start() = 0;
  virtual absl::Status SendLine(std::string line) = 0;
  virtual absl::StatusOr<std::vector<std::string>> Poll() = 0;
  virtual void Stop() noexcept = 0;

  virtual const std::filesystem::path& working_directory() const = 0;
  virtual std::string diagnostics() const = 0;
};

// Creates the platform process transport without starting Codex. Process
// startup remains lazy until the first image request reaches the adapter.
absl::StatusOr<std::unique_ptr<CodexAppServerTransport>> CreateCodexAppServerProcess(
    CodexAppServerProcessConfig config);

}  // namespace zebes
