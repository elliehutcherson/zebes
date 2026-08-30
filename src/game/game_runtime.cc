#include "game/game_runtime.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "engine/input_manager.h"
#include "engine/input_types.h"
#include "game/game_engine.h"
#include "game/game_renderer.h"
#include "game/game_scene.h"
#include "game/player_simulation.h"
#include "game/runtime_world.h"
#include "objects/camera.h"
#include "objects/level.h"
#include "resources/texture_resource_store.h"

namespace zebes {
namespace {

constexpr std::string_view kInitialLevelId = "9e20ee58-f4d2-4931-b74b-5555d4b35c00";

absl::Status ValidateOptions(const GameRuntime::Options& options) {
  RETURN_IF_ERROR(options.config.Validate());
  if (options.asset_root.empty()) {
    return absl::InvalidArgumentError("Game runtime asset root is empty");
  }
  if (options.input_source == nullptr) {
    return absl::InvalidArgumentError("Game runtime input source is null");
  }
  if (options.texture_resources == nullptr) {
    return absl::InvalidArgumentError("Game runtime texture resource store is null");
  }
  if (options.renderer == nullptr) {
    return absl::InvalidArgumentError("Game runtime renderer is null");
  }
  if (options.pacing_mode != SimulationPacingMode::kRealtime &&
      options.pacing_mode != SimulationPacingMode::kUnpaced) {
    return absl::InvalidArgumentError("Game runtime pacing mode is invalid");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<GameRuntime>> GameRuntime::Create(Options options) {
  RETURN_IF_ERROR(ValidateOptions(options));
  auto runtime = std::unique_ptr<GameRuntime>(new GameRuntime(std::move(options)));
  RETURN_IF_ERROR(runtime->Init());
  return runtime;
}

GameRuntime::GameRuntime(Options options)
    : config_(std::move(options.config)),
      asset_root_(std::move(options.asset_root)),
      input_source_(*options.input_source),
      texture_resources_(*options.texture_resources),
      renderer_(*options.renderer),
      pacing_mode_(options.pacing_mode) {}

absl::Status GameRuntime::Init() {
  ASSIGN_OR_RETURN(workspace_, AssetWorkspace::Create({
                                   .config = &config_,
                                   .texture_resources = &texture_resources_,
                                   .asset_root = asset_root_,
                                   .access = AssetWorkspace::Access::kReadOnly,
                                   .load_profile = AssetWorkspace::LoadProfile::kRuntime,
                               }));
  ASSIGN_OR_RETURN(level_assets_, workspace_->LoadLevelAssets(kInitialLevelId));

  ASSIGN_OR_RETURN(input_manager_, InputManager::Create({.input_source = &input_source_}));

  const Level& level = level_assets_->content.level;
  ASSIGN_OR_RETURN(std::unique_ptr<RuntimeWorld> world,
                   RuntimeWorld::Create(level_assets_->content,
                                        {.player_blueprint_id = std::string(kPlayerBlueprintId)}));
  ASSIGN_OR_RETURN(std::unique_ptr<PlayerSimulation> simulation,
                   PlayerSimulation::Create({
                       .input_manager = input_manager_.get(),
                       .world = std::move(world),
                       .camera = {.position = level.spawn_point,
                                  .zoom = 1.0,
                                  .viewport_width = config_.game_view.width,
                                  .viewport_height = config_.game_view.height},
                   }));
  PlayerSimulation* simulation_pointer = simulation.get();
  ASSIGN_OR_RETURN(game_engine_, GameEngine::Create({}, pacing_mode_, std::move(simulation)));
  simulation_ = simulation_pointer;
  return absl::OkStatus();
}

absl::Status GameRuntime::Run() {
  if (input_manager_ == nullptr || game_engine_ == nullptr || simulation_ == nullptr ||
      !level_assets_.has_value()) {
    return absl::FailedPreconditionError("Game runtime is not initialized");
  }

  while (!input_manager_->QuitRequested()) {
    input_manager_->Update();
    if (input_manager_->QuitRequested()) break;

    ASSIGN_OR_RETURN(const RunResult run_result, game_engine_->Run());
    (void)run_result;
    ASSIGN_OR_RETURN(
        const GameSceneFrame frame,
        ComposeGameSceneFrame(*level_assets_, simulation_->camera(),
                              {.transform_overrides = &simulation_->world().transforms(),
                               .sprite_id_overrides = &simulation_->world().sprite_ids(),
                               .frame_index_overrides = &simulation_->world().frame_indices()}));
    RETURN_IF_ERROR(renderer_.Render(frame));
  }
  return absl::OkStatus();
}

}  // namespace zebes
