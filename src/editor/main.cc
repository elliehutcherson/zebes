#include "absl/cleanup/cleanup.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "common/status_macros.h"
#include "editor/editor_engine.h"

absl::Status Run() {
  ASSIGN_OR_RETURN(auto engine, zebes::EditorEngine::Create());

  absl::Cleanup shutdown = [&]() { engine->Shutdown(); };
  RETURN_IF_ERROR(engine->Run());

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  absl::Status result = Run();
  if (!result.ok()) {
    LOG(ERROR) << "Editor run failed: " << result;
    return -1;
  }

  return 0;
}
