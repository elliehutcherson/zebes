#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "game/game_runtime.h"
#include "platform/sdl/sdl_game_host.h"

ABSL_FLAG(bool, unpaced, false,
          "Run bounded fixed simulation steps as fast as presentation allows");

namespace zebes {
namespace {

absl::Status RunGame() {
  const SimulationPacingMode pacing_mode = absl::GetFlag(FLAGS_unpaced)
                                               ? SimulationPacingMode::kUnpaced
                                               : SimulationPacingMode::kRealtime;
  ASSIGN_OR_RETURN(EngineConfig config, EngineConfig::Create());
  const std::string asset_root = config.paths.assets();
  ASSIGN_OR_RETURN(std::unique_ptr<SdlGameHost> host,
                   SdlGameHost::Create({.window = config.window, .game_view = config.game_view}));
  ASSIGN_OR_RETURN(std::unique_ptr<GameRuntime> runtime,
                   GameRuntime::Create({
                       .config = std::move(config),
                       .asset_root = asset_root,
                       .input_source = &host->input_source(),
                       .texture_resources = &host->texture_resources(),
                       .renderer = &host->renderer(),
                       .pacing_mode = pacing_mode,
                   }));
  return runtime->Run();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  const absl::Status status = zebes::RunGame();
  if (!status.ok()) {
    LOG(ERROR) << "Game run failed: " << status;
    return 1;
  }
  return 0;
}
