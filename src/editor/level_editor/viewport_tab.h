#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "absl/status/status.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "objects/blueprint.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/vec.h"

namespace zebes {

// Calculates the screen-space offset (in pixels) for a parallax layer.
// - camera_pos: the camera's world position along one axis.
// - scroll_factor: how much the layer moves relative to the camera.
//   1.0 = moves with the camera (foreground).
//   0.0 = completely fixed (distant background).
// - zoom: the current canvas zoom level.
static inline double CalculateParallaxOffset(double camera_pos, double scroll_factor, double zoom) {
  return camera_pos * scroll_factor * zoom;
}

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
};

class ViewportTab {
 public:
  explicit ViewportTab(Api& api, GuiInterface* gui);

  // Main render loop for the viewport tab.
  absl::Status Render(const ViewportRenderOptions& options);

  // Resets the viewport view (zoom/offset) to default.
  void Reset();

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

 private:
  // Renders all active entities in the level, highlighting the selected one.
  // When show_borders is true, draws a bounding-box outline around every entity.
  void RenderEntities(const Level& level, uint64_t selected_entity_id, bool show_borders);

  // Renders a semi-transparent ghost of the placement blueprint at world_pos.
  void RenderPlacementGhost(const Blueprint& blueprint, Vec world_pos);

  // Handles all entity-related mouse input for a single frame:
  // placement clicks, selection clicks, and drag-to-move.
  // Must be called after canvas_.HandleInput() so ImGui item queries target the canvas.
  absl::Status HandleEntityInput(Level& level, const Blueprint* placement_blueprint,
                                 Vec mouse_world, uint64_t selected_entity_id);

  // Renders the parallax layers for the given level.
  // Each layer is rendered with its specific scroll factor relative to the camera view.
  void RenderZones(const Level& level);

  // Renders the boundaries of the level and potentially the camera start box.
  void RenderLevelBounds(const Level& level);

  Api& api_;
  GuiInterface* gui_;
  Canvas canvas_;
  Camera camera_;

  // Placement state
  std::optional<Entity> pending_entity_;
  uint64_t next_entity_id_ = 1;

  // Selection state (output to LevelEditor)
  // nullopt = no click this frame; Some(kInvalidId) = deselect; Some(id) = select.
  // Only valid for one frame; consumed via TakeClickSelection().
  std::optional<uint64_t> click_selected_entity_id_;

  // Drag state
  bool dragging_entity_ = false;
  bool entity_moved_ = false;
  Vec drag_offset_;
  uint64_t drag_entity_id_ = Entity::kInvalidId;
};

}  // namespace zebes
