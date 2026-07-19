#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/parallax_layout.h"
#include "objects/blueprint.h"
#include "objects/camera.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

// Returns the id of the entity whose bounding box contains world_pos, or
// Entity::kInvalidId if none match. The bounding box is derived from the
// sprite's first frame dimensions and offset; a 32x32 default is used when
// the entity has no sprite.
// TODO: This is O(n) over all entities. Replace with a spatial index (e.g.
// a quadtree) to support large entity counts efficiently.
static inline uint64_t PickEntity(const std::map<uint64_t, Entity>& entities, Vec world_pos) {
  constexpr int kDefaultHalfSize = 16;
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;

    Vec min_pt;
    Vec max_pt;
    if (entity.sprite != nullptr && !entity.sprite->frames.empty()) {
      const SpriteFrame& frame = entity.sprite->frames[0];
      min_pt = {entity.transform.position.x + frame.offset_x,
                entity.transform.position.y + frame.offset_y};
      max_pt = {min_pt.x + frame.render_w, min_pt.y + frame.render_h};
    } else {
      min_pt = {entity.transform.position.x - kDefaultHalfSize,
                entity.transform.position.y - kDefaultHalfSize};
      max_pt = {entity.transform.position.x + kDefaultHalfSize,
                entity.transform.position.y + kDefaultHalfSize};
    }

    if (world_pos.x >= min_pt.x && world_pos.x <= max_pt.x && world_pos.y >= min_pt.y &&
        world_pos.y <= max_pt.y) {
      return id;
    }
  }
  return Entity::kInvalidId;
}

// Returns the next available entity ID for a level: one past the maximum
// existing entity ID, or 1 if the level has no entities. Because
// level.entities is a std::map the largest key is always at rbegin().
static inline uint64_t NextAvailableEntityId(const std::map<uint64_t, Entity>& entities) {
  if (entities.empty()) return 1;
  return entities.rbegin()->first + 1;
}

// Builds a new Entity at world_pos from the given blueprint state.
// Sets id, blueprint_id, blueprint_state_index, and transform.position.
// The sprite and collider pointers are left null; resolve them via the Api
// after placement.
static inline Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index,
                                               Vec world_pos, uint64_t id) {
  Entity entity;
  entity.id = id;
  entity.blueprint_id = blueprint.id;
  entity.blueprint_state_index = state_index;
  entity.transform.position = world_pos;
  return entity;
}

// Encodes a (chunk_x, chunk_y) pair as the flat-hash-map key used by Level::tile_chunks.
static inline int64_t ChunkKey(int chunk_x, int chunk_y) {
  return (static_cast<int64_t>(chunk_y) << 32) | static_cast<uint32_t>(chunk_x);
}

// Sets the tile ID at a world-tile coordinate, creating the chunk if absent.
// Pass tile_id=0 to erase a tile.
static inline void SetTileAt(Level& level, int tile_x, int tile_y, int tile_id) {
  constexpr int kSize = TileChunk::kSize;
  int chunk_x = tile_x / kSize;
  int chunk_y = tile_y / kSize;
  int local_x = tile_x % kSize;
  int local_y = tile_y % kSize;
  level.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * kSize + local_x] = tile_id;
}

// Returns the tile ID at a world-tile coordinate, or 0 if the chunk does not exist.
static inline int GetTileAt(const Level& level, int tile_x, int tile_y) {
  constexpr int kSize = TileChunk::kSize;
  int chunk_x = tile_x / kSize;
  int chunk_y = tile_y / kSize;
  int local_x = tile_x % kSize;
  int local_y = tile_y % kSize;
  auto it = level.tile_chunks.find(ChunkKey(chunk_x, chunk_y));
  if (it == level.tile_chunks.end()) return 0;
  return it->second.tiles[local_y * kSize + local_x];
}

// Computes the entity transform origin such that the collider or sprite bounding box is
// horizontally centered within the hovered tile cell and vertically bottom-aligned with it.
// The collider bounding box takes priority over sprite render dimensions.
// Returns InvalidArgument and logs an error if neither provides usable dimensions.
absl::StatusOr<Vec> SnapEntityToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                      const Collider* collider, const Sprite* sprite);

// Per-frame inputs to ViewportTab::Render(). All fields are transient — they
// are not stored between frames.
struct ViewportRenderOptions {
  // Level to render and interact with. Must outlive the Render() call.
  Level* level = nullptr;
  // Blueprint to place on the next canvas click; nullptr = no placement mode.
  const Blueprint* placement_blueprint = nullptr;
  // Entity currently selected by the LevelEditor; kInvalidId = none.
  uint64_t selected_entity_id = Entity::kInvalidId;
  // Whether entity placement and movement should snap to the grid relative to origin.
  bool snap_to_grid = true;
  // Whether to draw bounding-box borders around all entities in the viewport.
  bool show_entity_borders = false;
  // When true, a right-click on the canvas deletes the entity under the cursor.
  bool delete_mode = false;
  // Tile to paint when non-null; nullptr = not in tile-painting mode.
  const Tile* placement_tile = nullptr;
  // Tileset owning placement_tile. Must be non-null when placement_tile is set.
  const Tileset* placement_tileset = nullptr;
  // Whether to draw a thin border around every placed tile cell in the viewport.
  bool show_tile_frame = false;
  // Whether to draw the collision-shape overlay on every placed tile.
  bool show_tile_collision = false;
  // Blue overlay alpha [0,1] drawn on top of every tile cell. 0 = off.
  float tile_overlay_opacity = 0.0f;
  // Yellow overlay alpha [0,1] drawn on top of every entity. 0 = off.
  float entity_overlay_opacity = 0.0f;
  // Zone selected in the editor navigator. Used only for gizmo highlighting
  // and frame-selected behavior; it does not override runtime activation.
  std::optional<int> selected_zone_id;
};

class ViewportTab {
 public:
  explicit ViewportTab(Api& api, GuiInterface* gui);

  // Main render loop for the viewport tab.
  absl::Status Render(const ViewportRenderOptions& options);

  // Resets the viewport view (zoom/offset) to default.
  void Reset();

  // Requests that the next viewport frame center and fit this zone.
  void FrameZone(const ParallaxZone& zone);

  // Returns the entity placed this frame (if any), then clears it.
  // The returned entity has its sprite pointer resolved via the Api.
  std::optional<Entity> TakeNewEntity();

  // Returns the result of a canvas click for selection, then clears it.
  // - nullopt: no click occurred this frame.
  // - Entity::kInvalidId: click on empty space (deselect).
  // - any other value: click on that entity id (select).
  std::optional<uint64_t> TakeClickSelection();

  // Returns true if an entity was moved by drag this frame, then clears the flag.
  bool TakeEntityMoved();

  // Returns the ID of an entity the user right-clicked to delete (if any), then clears it.
  // Only populated when delete_mode is true in the render options.
  std::optional<uint64_t> TakeDeleteRequest();

 private:
  friend class ViewportTabTestPeer;

  // Renders all active entities in the level, highlighting the selected one.
  // When show_borders is true, draws a bounding-box outline around every entity.
  void RenderEntities(const Level& level, uint64_t selected_entity_id, bool show_borders,
                      float entity_overlay_opacity);

  // Renders a semi-transparent ghost of the placement blueprint at world_pos.
  void RenderPlacementGhost(const Blueprint& blueprint, Vec world_pos);

  // Renders all placed tile chunks using the given tileset's texture atlas.
  // Optionally draws cell borders (show_frame) and collision overlays (show_collision).
  void RenderTileChunks(const Level& level, const Tileset& tileset, bool show_frame,
                        bool show_collision, float tile_overlay_opacity);

  // Renders a semi-transparent ghost of the selected tile snapped to the tile render grid.
  // tile_render_w/h are the world-space pixel dimensions used for snapping and screen extents.
  void RenderTilePlacementGhost(const Tile& tile, const Tileset& tileset, Vec mouse_world,
                                int tile_render_w, int tile_render_h);

  // Handles tile-painting input: left-held paints tile at snapped position,
  // right-click erases (sets tile_id=0).
  // tile_render_w/h are the world-space pixel dimensions used for grid snapping.
  absl::Status HandleTileInput(Level& level, const Tile& tile, const Tileset& tileset,
                               Vec mouse_world, int tile_render_w, int tile_render_h);

  // Handles all entity-related mouse input for a single frame:
  // placement clicks, selection clicks, and drag-to-move.
  // Must be called after canvas_.HandleInput() so ImGui item queries target the canvas.
  absl::Status HandleEntityInput(Level& level, const Blueprint* placement_blueprint,
                                 Vec mouse_world, uint64_t selected_entity_id, bool delete_mode);

  // Resolves and renders the single active parallax environment at the camera
  // center. Returns the stable active-zone identity, when one exists.
  std::optional<ActiveParallaxZone> RenderParallaxBackground(const Level& level);

  // Renders one theme's layers using the current camera.
  void RenderParallaxTheme(const ParallaxTheme& theme);

  // Draws visible zone bounds independently of background activation.
  void RenderZoneGizmos(const Level& level, std::optional<int> selected_zone_id,
                        std::optional<int> active_zone_id);

  // Applies a queued frame request once the current viewport and world
  // dimensions are known.
  void ApplyPendingCameraFrame(const ImVec2& viewport_size, VisibleWorldBounds world_bounds);

  // Renders the boundaries of the level and potentially the camera start box.
  void RenderLevelBounds(const Level& level);

  // Resolves the blueprint's collider and sprite via the Api, then delegates to
  // SnapEntityToGrid. Returns mouse_world unchanged when snap is disabled.
  absl::StatusOr<Vec> SnapBlueprintToGrid(Vec mouse_world, const Blueprint& blueprint,
                                           int tile_render_w, int tile_render_h) const;

  Api& api_;
  GuiInterface* gui_;
  Canvas canvas_;
  Camera camera_;
  std::optional<VisibleWorldBounds> pending_camera_frame_;

  // Placement state
  std::optional<Entity> pending_entity_;
  uint64_t next_entity_id_ = 1;

  // Selection state (output to LevelEditor)
  // nullopt = no click this frame; Some(kInvalidId) = deselect; Some(id) = select.
  // Only valid for one frame; consumed via TakeClickSelection().
  std::optional<uint64_t> click_selected_entity_id_;

  // Delete-mode state (output to LevelEditor)
  // nullopt = no delete request this frame; Some(id) = entity to delete.
  std::optional<uint64_t> delete_requested_entity_id_;

  // Drag state
  bool dragging_entity_ = false;
  bool entity_moved_ = false;
  Vec drag_offset_;
  uint64_t drag_entity_id_ = Entity::kInvalidId;
};

}  // namespace zebes
