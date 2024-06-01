#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

#include "engine/game.h"

ABSL_FLAG(std::string, log_dir, "/tmp",
          "Path to the directory where logs should be written.");
ABSL_FLAG(int, minloglevel, 0,
          "Minimum log level to log. (0=INFO, 1=WARNING, 2=ERROR, 3=FATAL)");

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);
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