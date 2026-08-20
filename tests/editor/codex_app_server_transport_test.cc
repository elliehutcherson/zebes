#include "editor/image_generation/codex_app_server_transport.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "macros.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace zebes {
namespace {

#if !defined(_WIN32)

TEST(CodexAppServerTransportTest, ExchangesJsonlWithoutPassingTheApiKey) {
  const std::filesystem::path test_directory =
      std::filesystem::temp_directory_path() / "zebes_codex_transport_test";
  std::filesystem::remove_all(test_directory);
  std::filesystem::create_directories(test_directory);
  absl::Cleanup cleanup = [&test_directory] { std::filesystem::remove_all(test_directory); };

  const std::filesystem::path executable = test_directory / "fake-codex";
  {
    std::ofstream script(executable);
    script << "#!/bin/sh\n"
              "if [ \"${OPENAI_API_KEY+x}\" = x ]; then\n"
              "  echo api-key-leaked >&2\n"
              "  exit 9\n"
              "fi\n"
              "IFS= read -r line\n"
              "printf '%s\\n' \"$line\"\n";
  }
  ASSERT_EQ(chmod(executable.c_str(), 0700), 0);

  const char* const previous_key = std::getenv("OPENAI_API_KEY");
  const std::string saved_key = previous_key == nullptr ? "" : previous_key;
  ASSERT_EQ(setenv("OPENAI_API_KEY", "must-not-reach-child", 1), 0);
  absl::Cleanup restore_environment = [previous_key, saved_key] {
    if (previous_key == nullptr) {
      unsetenv("OPENAI_API_KEY");
      return;
    }
    setenv("OPENAI_API_KEY", saved_key.c_str(), 1);
  };

  CodexAppServerProcessConfig config;
  config.executable = executable.string();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CodexAppServerTransport> transport,
                       CreateCodexAppServerProcess(config));
  const std::filesystem::path working_directory = transport->working_directory();
  ASSERT_TRUE(std::filesystem::exists(working_directory));
  ASSERT_OK(transport->Start());
  ASSERT_OK(transport->SendLine(R"({"id":1,"method":"test","params":{}})"));

  std::vector<std::string> lines;
  for (int attempt = 0; attempt < 100 && lines.empty(); ++attempt) {
    absl::StatusOr<std::vector<std::string>> polled = transport->Poll();
    ASSERT_OK(polled.status()) << transport->diagnostics();
    lines = *std::move(polled);
    if (lines.empty()) absl::SleepFor(absl::Milliseconds(5));
  }

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0], R"({"id":1,"method":"test","params":{}})");
  EXPECT_EQ(transport->Start().code(), absl::StatusCode::kFailedPrecondition);
  transport->Stop();
  EXPECT_EQ(transport->SendLine(R"({"id":2})").code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->Poll().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->Start().code(), absl::StatusCode::kFailedPrecondition);
  transport.reset();
  EXPECT_FALSE(std::filesystem::exists(working_directory));
}

#else

TEST(CodexAppServerTransportTest, ReportsUnsupportedWindowsProcessTransport) {
  EXPECT_EQ(CreateCodexAppServerProcess(CodexAppServerProcessConfig{}).status().code(),
            absl::StatusCode::kUnimplemented);
}

#endif

TEST(CodexAppServerTransportTest, RejectsAnEmptyExecutable) {
  CodexAppServerProcessConfig config;
  config.executable.clear();

  EXPECT_EQ(CreateCodexAppServerProcess(config).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
