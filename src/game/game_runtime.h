#pragma once

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config.h"
#include "game/game_engine.h"
#include "game/game_level_assets.h"

namespace zebes {

class AssetWorkspace;
class FreeFlySimulation;
class InputManager;
class SdlGameRenderer;
class SdlInputSource;
class SdlTextureStore;
class SdlWrapper;

// M1's process composition root. Boot performs config and asset I/O before
// Run starts; the running loop only polls input, advances GameEngine, composes
// a platform-neutral frame, and presents it on the SDL-owning main thread.
class GameRuntime {
 public:
  struct Options {
    SimulationPacingMode pacing_mode = SimulationPacingMode::kRealtime;
  };

  static absl::StatusOr<std::unique_ptr<GameRuntime>> Create(Options options);

  ~GameRuntime();
  GameRuntime(const GameRuntime&) = delete;
  GameRuntime& operator=(const GameRuntime&) = delete;

  absl::Status Run();
  void Shutdown();

 private:
  GameRuntime(EngineConfig config, Options options);

  absl::Status Init();

  bool sdl_initialized_ = false;
  EngineConfig config_;
  Options options_;

  // Reverse declaration order is destruction order. Runtime handles and
  // borrowers disappear before the SDL renderer and window they depend on.
  std::unique_ptr<SdlWrapper> sdl_;
  std::unique_ptr<SdlTextureStore> texture_store_;
  std::unique_ptr<AssetWorkspace> workspace_;
  std::optional<GameLevelAssets> level_assets_;
  std::unique_ptr<SdlInputSource> input_source_;
  std::unique_ptr<InputManager> input_manager_;
  FreeFlySimulation* simulation_ = nullptr;
  std::unique_ptr<GameEngine> game_engine_;
  std::unique_ptr<SdlGameRenderer> renderer_;
};

}  // namespace zebes
