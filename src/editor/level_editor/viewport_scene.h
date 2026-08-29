#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "editor/level_editor/viewport_model.h"
#include "engine/scene_composition.h"
#include "objects/blueprint.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

using SpriteRenderItem = SceneSpriteRenderResource;
using TileRenderItem = SceneTileRenderItem;
using ParallaxElementRenderResource = SceneParallaxElementRenderResource;
using ParallaxRenderItem = SceneParallaxRenderItem;
using ParallaxRenderBatch = SceneParallaxRenderBatch;
using ParallaxRenderOptions = SceneParallaxRenderOptions;

enum class EntityRenderMode {
  kLevel,
  kPlacementGhost,
};

// Editor presentation layered over one runtime-neutral entity item.
struct EntityRenderItem {
  EntityRenderMode mode = EntityRenderMode::kLevel;
  uint64_t entity_id = Entity::kInvalidId;
  int sort_order = 0;
  WorldRect bounds;
  std::optional<SpriteRenderItem> sprite;
  float overlay_opacity = 0.0f;
  bool show_border = false;
  bool selected = false;
  Vec origin;
  bool show_origin = false;
  std::optional<BlueprintPlacementMode> placement_mode;
};

struct EntityRenderOptions {
  uint64_t selected_entity_id = Entity::kInvalidId;
  bool show_borders = false;
  float overlay_opacity = 0.0f;
  std::optional<BlueprintPlacementMode> selected_placement_mode;
};

enum class ZoneGizmoState {
  kNormal,
  kActive,
  kSelected,
};

struct ZoneGizmoItem {
  int zone_id = -1;
  WorldRect bounds;
  ZoneGizmoState state = ZoneGizmoState::kNormal;
};

enum class TileRenderMode {
  kLevel,
  kPlacementGhost,
};

// Editor presentation layered over one runtime-neutral tile batch.
struct TileRenderBatch {
  TextureHandle atlas_texture;
  TileRenderMode mode = TileRenderMode::kLevel;
  float overlay_opacity = 0.0f;
  bool show_frame = false;
  bool show_collision = false;
  std::vector<TileRenderItem> items;
};

struct TileRenderOptions {
  float overlay_opacity = 0.0f;
  bool show_frame = false;
  bool show_collision = false;
};

// Decorates shared entity composition with selection and editor overlays.
absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites,
    const EntityRenderOptions& options);

// Composes one transient placement preview using shared entity geometry.
absl::StatusOr<EntityRenderItem> ComposeEntityPlacementItem(Vec world_position,
                                                            const ResolvedSprite& resolved,
                                                            BlueprintPlacementMode placement_mode);

// Builds editor-only zone gizmos intersecting the current camera.
absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id);

// Decorates the shared visible-tile batch with editor overlays.
absl::StatusOr<TileRenderBatch> ComposeLevelTileRenderBatch(
    const Level& level, const WorldLayer& layer, const Tileset& tileset,
    TextureHandle atlas_texture, const Camera& camera, const TileRenderOptions& options);

// Composes the selected tile snapped to the level's render grid.
absl::StatusOr<TileRenderBatch> ComposeTilePlacementBatch(const Tile& tile, const Tileset& tileset,
                                                          TextureHandle atlas_texture,
                                                          Vec mouse_world, int tile_render_width,
                                                          int tile_render_height);

// Compatibility name for editor callers; the implementation is the common
// runtime-neutral scene composer.
absl::StatusOr<ParallaxRenderBatch> ComposeParallaxRenderBatch(
    const ParallaxTheme& theme, const Camera& camera,
    const std::map<std::string, TextureHandle>& textures,
    const ParallaxRenderOptions& options = {});

}  // namespace zebes
