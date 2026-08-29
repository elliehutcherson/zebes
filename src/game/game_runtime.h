#pragma once

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "engine/input_manager.h"
#include "engine/input_types.h"
#include "game/game_engine.h"
#include "game/game_renderer.h"
#include "game/player_simulation.h"
#include "resources/loaded_level_assets.h"
#include "resources/texture_resource_store.h"

namespace zebes {

// Platform-neutral M1 runtime. Boot performs asset I/O before Run starts; the
// loop only polls input, advances GameEngine, composes a frame, and presents it
// through borrowed interfaces supplied by the process composition root.
class GameRuntime {
 public:
  struct Options {
    EngineConfig config;
    std::string asset_root;
    InputSource* input_source = nullptr;
    TextureResourceStore* texture_resources = nullptr;
    GameRenderer* renderer = nullptr;
    SimulationPacingMode pacing_mode = SimulationPacingMode::kRealtime;
  };

  static absl::StatusOr<std::unique_ptr<GameRuntime>> Create(Options options);

  ~GameRuntime() = default;
  GameRuntime(const GameRuntime&) = delete;
  GameRuntime& operator=(const GameRuntime&) = delete;

  absl::Status Run();

 private:
  explicit GameRuntime(Options options);

  absl::Status Init();

  EngineConfig config_;
  std::string asset_root_;
  InputSource& input_source_;
  TextureResourceStore& texture_resources_;
  GameRenderer& renderer_;
  SimulationPacingMode pacing_mode_;

  // Platform dependencies are borrowed from a host that outlives GameRuntime.
  // Reverse declaration order releases borrowers and handles first.
  std::unique_ptr<AssetWorkspace> workspace_;
  std::optional<LoadedLevelAssets> level_assets_;
  std::unique_ptr<InputManager> input_manager_;
  PlayerSimulation* simulation_ = nullptr;
  std::unique_ptr<GameEngine> game_engine_;
};

}  // namespace zebes
