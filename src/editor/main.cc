#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "editor/editor_engine.h"

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  zebes::EditorEngine engine;

  auto init_status = engine.Init();
  if (!init_status.ok()) {
    LOG(ERROR) << "Failed to initialize editor: " << init_status;
    return -1;
  }

  auto run_status = engine.Run();
  if (!run_status.ok()) {
    LOG(ERROR) << "Editor run failed: " << run_status;
    engine.Shutdown();
    return -1;
  }

  engine.Shutdown();
  return 0;
}
