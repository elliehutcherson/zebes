#include "game/game_runtime.h"

#include <memory>
#include <string_view>
#include <utility>

#include "SDL.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "engine/input_manager.h"
#include "game/free_fly_simulation.h"
#include "game/game_engine.h"
#include "game/game_level_assets.h"
#include "game/game_scene.h"
#include "objects/camera.h"
#include "platform/sdl/sdl_game_renderer.h"
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_texture_store.h"

namespace zebes {
namespace {

constexpr std::string_view kInitialLevelId = "9e20ee58-f4d2-4931-b74b-5555d4b35c00";

}  // namespace

absl::StatusOr<std::unique_ptr<GameRuntime>> GameRuntime::Create(Options options) {
  ASSIGN_OR_RETURN(EngineConfig config, EngineConfig::Create());
  auto runtime = std::unique_ptr<GameRuntime>(new GameRuntime(std::move(config), options));
  RETURN_IF_ERROR(runtime->Init());
  return runtime;
}

GameRuntime::GameRuntime(EngineConfig config, Options options)
    : config_(std::move(config)), options_(options) {}

GameRuntime::~GameRuntime() { Shutdown(); }

absl::Status GameRuntime::Init() {
  if (options_.pacing_mode != SimulationPacingMode::kRealtime &&
      options_.pacing_mode != SimulationPacingMode::kUnpaced) {
    return absl::InvalidArgumentError("Game runtime pacing mode is invalid");
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    return absl::InternalError(absl::StrCat("SDL initialization failed: ", SDL_GetError()));
  }
  sdl_initialized_ = true;

  ASSIGN_OR_RETURN(sdl_, SdlWrapper::Create(config_.window));
  ASSIGN_OR_RETURN(renderer_, SdlGameRenderer::Create(*sdl_, config_.game_view));
  texture_store_ = std::make_unique<SdlTextureStore>(*sdl_);
  ASSIGN_OR_RETURN(workspace_, AssetWorkspace::Create({
                                   .config = &config_,
                                   .texture_resources = texture_store_.get(),
                                   .asset_root = config_.paths.assets(),
                                   .access = AssetWorkspace::Access::kReadOnly,
                                   .load_profile = AssetWorkspace::LoadProfile::kRuntime,
                               }));
  ASSIGN_OR_RETURN(level_assets_, LoadGameLevelAssets(workspace_->api(), kInitialLevelId));

  input_source_ = std::make_unique<SdlInputSource>(*sdl_);
  ASSIGN_OR_RETURN(input_manager_, InputManager::Create({.input_source = input_source_.get()}));

  const Level& level = level_assets_->level;
  ASSIGN_OR_RETURN(std::unique_ptr<FreeFlySimulation> simulation,
                   FreeFlySimulation::Create({
                       .input_manager = input_manager_.get(),
                       .camera = {.position = level.spawn_point,
                                  .zoom = 1.0,
                                  .viewport_width = config_.game_view.width,
                                  .viewport_height = config_.game_view.height},
                   }));
  FreeFlySimulation* simulation_pointer = simulation.get();
  ASSIGN_OR_RETURN(game_engine_,
                   GameEngine::Create({}, options_.pacing_mode, std::move(simulation)));
  simulation_ = simulation_pointer;
  return absl::OkStatus();
}

absl::Status GameRuntime::Run() {
  if (input_manager_ == nullptr || game_engine_ == nullptr || simulation_ == nullptr ||
      renderer_ == nullptr || !level_assets_.has_value()) {
    return absl::FailedPreconditionError("Game runtime is not initialized");
  }

  while (!input_manager_->QuitRequested()) {
    input_manager_->Update();
    if (input_manager_->QuitRequested()) break;

    ASSIGN_OR_RETURN(const RunResult run_result, game_engine_->Run());
    (void)run_result;
    ASSIGN_OR_RETURN(const GameSceneFrame frame,
                     ComposeGameSceneFrame(*level_assets_, simulation_->camera()));
    RETURN_IF_ERROR(renderer_->Render(frame));
  }
  return absl::OkStatus();
}

void GameRuntime::Shutdown() {
  renderer_.reset();
  game_engine_.reset();
  simulation_ = nullptr;
  input_manager_.reset();
  input_source_.reset();
  level_assets_.reset();
  workspace_.reset();
  texture_store_.reset();
  sdl_.reset();
  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

}  // namespace zebes
