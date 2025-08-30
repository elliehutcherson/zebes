#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/game.h"
#include "common/logging.h"

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
  // std::string config_path = zebes::GameConfig::GetDefaultConfigPath();
  // absl::StatusOr<zebes::GameConfig> config =
  //     zebes::GameConfig::Create(config_path);
  absl::StatusOr<zebes::GameConfig> config = zebes::GameConfig::Create();
  if (!config.ok()) {
    LOG(ERROR) << "Zebes: failed to create game config...";
    LOG(ERROR) << config.status();
    return 1;
  }

  if (program == "player") {
    config->mode = zebes::GameConfig::Mode::kPlayerMode;
  } else if (program == "creator") {
    config->mode = zebes::GameConfig::Mode::kCreatorMode;
  } else {
    LOG(INFO) << "Zebes command not recognized...";
    return 1;
  }

  zebes::Game game(std::move(*config));
  absl::Status result = game.Init();
  if (result.ok()) result = game.Run();
  LOG(INFO) << "Zebes: exiting, " << result.ToString();

  return 0;
}