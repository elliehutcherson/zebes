#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/status/status.h"

#include "engine/game.h"
#include "engine/logging.h"

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  absl::AddLogSink(zebes::TerminalLogSink::Get());
  absl::AddLogSink(zebes::HudLogSink::Get());

  absl::InitializeLog();

  if (argc < 2) {
    LOG(ERROR) << "Zebes: no command received...";
    return 1;
  }

  std::string program = argv[1];
  zebes::GameConfig config = zebes::GameConfig::Create();
  if (program == "zebes") {
    config.mode = zebes::GameConfig::Mode::kPlayerMode;
  } else if (program == "creator") {
    config.mode = zebes::GameConfig::Mode::kCreatorMode;
  } else {
    LOG(INFO) << "Zebes command not recognized...";
    return 1;
  }

  zebes::Game game(config);
  absl::Status result = game.Init();
  if (result.ok())
    result = game.Run();
  LOG(INFO) << "Zebes: exiting, " << result.ToString();

  return 0;
}