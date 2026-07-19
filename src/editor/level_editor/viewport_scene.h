#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "engine/texture_handle.h"
#include "editor/level_editor/viewport_model.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

// Rectangle measured in pixels relative to a source texture. For an atlas
// entry, x and y locate its top-left corner and width and height describe the
// sampled region. This deliberately does not use an SDL rectangle type.
struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr bool IsValid() const { return x >= 0 && y >= 0 && width > 0 && height > 0; }
};

// Platform-neutral reference to the image region used for an entity. The
// texture store owns the native resource; the renderer resolves the opaque
// handle only while issuing draw commands.
struct SpriteRenderItem {
  TextureHandle texture;
  PixelRect source;
};

// Platform-neutral description of one entity in the editor viewport. Styling
// and native texture conversion remain the renderer's responsibility.
struct EntityRenderItem {
  uint64_t entity_id = Entity::kInvalidId;
  WorldRect bounds;
  std::optional<SpriteRenderItem> sprite;
  float overlay_opacity = 0.0f;
  bool show_border = false;
  bool selected = false;
};

struct EntityRenderOptions {
  uint64_t selected_entity_id = Entity::kInvalidId;
  bool show_borders = false;
  float overlay_opacity = 0.0f;
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
  // A tile already stored in the level. Normal editor overlays may be drawn.
  kLevel,
  // A transient preview of the tile under the placement cursor.
  kPlacementGhost,
};

// Platform-neutral description of one non-empty tile cell to draw. It contains
// geometry and tile metadata, but no SDL texture, ImGui coordinate, or draw
// command.
struct TileRenderItem {
  // Stable ID of the Tile definition represented by this cell.
  int tile_id = 0;
  // Destination rectangle in world coordinates using the level's tile render size.
  WorldRect bounds;
  // Region in the shared atlas using the tileset's source tile size.
  PixelRect source;
  // Gameplay collision metadata used by the optional editor overlay.
  TileShape collision_shape = TileShape::kNone;
};

// Complete platform-neutral description of one tile-rendering pass. Tiles in
// the batch share an atlas and presentation options. Keeping the handle here
// avoids repeating backend ownership metadata in every visible item.
struct TileRenderBatch {
  // Opaque atlas reference resolved by ViewportRenderer; the texture store owns it.
  TextureHandle atlas_texture;
  // Selects presentation rules for persistent tiles versus placement previews.
  TileRenderMode mode = TileRenderMode::kLevel;
  // Blue editor overlay opacity in the inclusive range [0, 1].
  float overlay_opacity = 0.0f;
  // Draw a border around each persistent tile cell.
  bool show_frame = false;
  // Visualize collision_shape for each persistent tile cell.
  bool show_collision = false;
  // Visible tiles in deterministic row-major spatial order.
  std::vector<TileRenderItem> items;
};

// User-controlled presentation settings applied while composing persistent
// level tiles. Placement previews use their fixed ghost presentation instead.
struct TileRenderOptions {
  float overlay_opacity = 0.0f;
  bool show_frame = false;
  bool show_collision = false;
};

// Builds render items in stable entity-ID order. Inactive entities are omitted.
// Invalid sprite geometry and opacity are rejected instead of being rendered
// with undefined bounds or color values.
absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const EntityRenderOptions& options);

// Builds gizmos for zones intersecting the current camera. Selection takes
// visual precedence over active-zone state.
absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id);

// Composes only tiles intersecting the camera. Entire offscreen chunks are
// rejected before their cells are scanned.
absl::StatusOr<TileRenderBatch> ComposeLevelTileRenderBatch(
    const Level& level, const Tileset& tileset, TextureHandle atlas_texture,
    const Camera& camera, const TileRenderOptions& options);

// Composes the selected tile snapped to the level's render grid.
absl::StatusOr<TileRenderBatch> ComposeTilePlacementBatch(
    const Tile& tile, const Tileset& tileset, TextureHandle atlas_texture,
    Vec mouse_world, int tile_render_width, int tile_render_height);

}  // namespace zebes
