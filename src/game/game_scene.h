#pragma once

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "engine/scene_composition.h"
#include "game/game_level_assets.h"
#include "objects/camera.h"

namespace zebes {

struct GameWorldLayerFrame {
  int layer_id = -1;
  SceneTileRenderBatch tiles;
  std::vector<SceneEntityRenderItem> entities;
};

// One complete runtime frame in stable back-to-front order. Native renderer
// types remain outside this aggregate.
struct GameSceneFrame {
  Camera camera;
  std::optional<ResolvedParallaxEnvironment> environment;
  std::vector<SceneParallaxRenderBatch> parallax;
  std::vector<GameWorldLayerFrame> world_layers;
};

absl::StatusOr<GameSceneFrame> ComposeGameSceneFrame(const GameLevelAssets& assets,
                                                     const Camera& camera);

}  // namespace zebes
