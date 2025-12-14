#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "common/status_macros.h"

absl::Status Run() { return absl::OkStatus(); }

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  absl::Status result = Run();
  if (!result.ok()) {
    LOG(ERROR) << "Engine run failed: " << result;
    return -1;
  }

  LOG(INFO) << "Engine run complete, bye =)";
  return 0;
}
