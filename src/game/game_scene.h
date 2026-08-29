#pragma once

#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "engine/scene_composition.h"
#include "objects/camera.h"
#include "objects/transform.h"
#include "resources/loaded_level_assets.h"

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

struct GameSceneCompositionOptions {
  const absl::flat_hash_map<uint64_t, Transform>* transform_overrides = nullptr;
};

absl::StatusOr<GameSceneFrame> ComposeGameSceneFrame(
    const LoadedLevelAssets& assets, const Camera& camera,
    const GameSceneCompositionOptions& options = {});

}  // namespace zebes
