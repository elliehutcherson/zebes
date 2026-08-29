#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "engine/scene_types.h"
#include "engine/texture_handle.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/tileset.h"

namespace zebes {

// Rectangle measured in pixels relative to a source texture.
struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr bool IsValid() const { return x >= 0 && y >= 0 && width > 0 && height > 0; }
};

// Managed texture plus the source region used by one entity.
struct SceneSpriteRenderResource {
  TextureHandle texture;
  PixelRect source;
};

// Runtime-neutral entity geometry in authored draw order.
struct SceneEntityRenderItem {
  uint64_t entity_id = Entity::kInvalidId;
  int sort_order = 0;
  WorldRect bounds;
  std::optional<SceneSpriteRenderResource> sprite;
  Vec origin;
};

// Runtime-neutral description of one visible tile cell.
struct SceneTileRenderItem {
  int tile_id = 0;
  WorldRect bounds;
  PixelRect source;
  TileShape collision_shape = TileShape::kNone;
};

// Visible tiles sharing one atlas texture.
struct SceneTileRenderBatch {
  TextureHandle atlas_texture;
  std::vector<SceneTileRenderItem> items;
};

struct SceneLevelTileRenderOptions {
  const Level& level;
  const WorldLayer& layer;
  const Tileset& tileset;
  TextureHandle atlas_texture;
  const Camera& camera;
};

struct SceneTileRenderOptions {
  const Tile& tile;
  const Tileset& tileset;
  int64_t tile_x = 0;
  int64_t tile_y = 0;
  int tile_render_width = 0;
  int tile_render_height = 0;
};

// Managed texture bound to one stable authored parallax element ID.
struct SceneParallaxElementRenderResource {
  int element_id = -1;
  TextureHandle texture;
};

struct SceneParallaxRenderItem {
  ParallaxLayer layer;
  std::vector<SceneParallaxElementRenderResource> elements;
};

struct SceneParallaxRenderBatch {
  Camera camera;
  double opacity = 1.0;
  std::vector<SceneParallaxRenderItem> layers;
};

// A subset is useful for isolated review and editor previews; runtime rendering
// uses the default complete-theme selection.
struct SceneParallaxRenderOptions {
  double opacity = 1.0;
  std::optional<int> layer_index;
  std::optional<int> element_id;
};

// Composes one entity using shared placeholder, sprite-bound, and source-region
// rules. Native textures remain behind TextureHandle.
absl::StatusOr<SceneEntityRenderItem> ComposeSceneEntityRenderItem(uint64_t entity_id,
                                                                   const Entity& entity,
                                                                   const ResolvedSprite& resolved);

// Builds active entities in deterministic back-to-front order.
absl::StatusOr<std::vector<SceneEntityRenderItem>> ComposeSceneEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites);

// Composes only tiles intersecting the camera. Entire offscreen chunks are
// rejected before their cells are scanned.
absl::StatusOr<SceneTileRenderBatch> ComposeSceneLevelTileRenderBatch(
    const SceneLevelTileRenderOptions& options);

// Composes one exact tileset member at a grid coordinate. This is shared by
// editor placement previews without putting preview styling in the scene core.
absl::StatusOr<SceneTileRenderItem> ComposeSceneTileRenderItem(
    const SceneTileRenderOptions& options);

// Binds theme layers to managed textures without exposing native resources.
absl::StatusOr<SceneParallaxRenderBatch> ComposeSceneParallaxRenderBatch(
    const ParallaxTheme& theme, const Camera& camera,
    const std::map<std::string, TextureHandle>& textures,
    const SceneParallaxRenderOptions& options = {});

}  // namespace zebes
