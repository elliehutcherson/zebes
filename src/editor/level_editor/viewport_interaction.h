#pragma once

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "editor/level_editor/terrain_brush.h"
#include "editor/level_editor/viewport_model.h"
#include "objects/blueprint.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/vec.h"

namespace zebes {

// Platform-neutral pointer state captured from the level canvas for one frame.
struct ViewportInteractionInput {
  // Pointer position in level/world coordinates.
  Vec world_position;
  // Whether the pointer is inside the persistent level bounds.
  bool pointer_in_level = false;
  // Whether the primary button was pressed over the canvas this frame.
  bool primary_pressed = false;
  // Whether a canvas-captured primary interaction remains held.
  bool primary_down = false;
  // Whether the secondary button was pressed over the canvas this frame.
  bool secondary_pressed = false;
  // Whether a canvas-captured secondary interaction remains held.
  bool secondary_down = false;
};

// Active authoring mode and selection supplied by the Level Editor each frame.
struct ViewportInteractionOptions {
  // Terrain painted while primary_down is true; empty disables terrain mode.
  // Takes priority over tile and blueprint placement.
  std::optional<int> paint_terrain_id;
  // Collision geometry the terrain brush lays down. Authored by the user, never
  // inferred from the gesture, and the artwork follows from it.
  TileShape paint_shape = TileShape::kFullBlock;
  // Terrain tables for the level's tileset. Required when paint_terrain_id is
  // set, and must outlive the Update() call.
  // Mutable because a derived terrain invents tiles while resolving a cell, and
  // the index has to learn about one before the cells around it are refreshed
  // against what it holds.
  TerrainIndex* terrain_index = nullptr;
  // Resolves a cell's artwork once its geometry is decided. Required when
  // paint_terrain_id is set, and must outlive the Update() call.
  TerrainTileProvider* terrain_provider = nullptr;
  // Tile ID painted while primary_down is true; empty disables tile mode.
  std::optional<int> paint_tile_id;
  // Blueprint placed on primary press; null disables blueprint mode.
  const Blueprint* placement_blueprint = nullptr;
  // Resolved sprite for placement_blueprint, or null for an invisible blueprint.
  const Sprite* placement_sprite = nullptr;
  // Entity currently selected by the Level Editor.
  uint64_t selected_entity_id = Entity::kInvalidId;
  // Sprites resolved for this frame, used to size entities during picking.
  // Entities without an entry fall back to placeholder bounds.
  const SpriteLookup* entity_sprites = nullptr;
  // Whether a secondary press requests deletion instead of ordinary interaction.
  bool delete_mode = false;
};

// Discrete actions produced for the Level Editor after processing one frame.
struct ViewportInteractionResult {
  // Entity ready for insertion into the active level.
  std::optional<Entity> placed_entity;
  // Pick result, including Entity::kInvalidId for an empty-space click.
  std::optional<uint64_t> selected_entity_id;
  // Entity requested for deletion by stable ID.
  std::optional<uint64_t> delete_entity_id;
};

// Owns viewport authoring gestures without depending on ImGui, SDL, or Api.
class ViewportInteractionController {
 public:
  // Clears transient drag and ID-allocation state when the active level changes.
  void Reset();

  absl::StatusOr<ViewportInteractionResult> Update(Level& level,
                                                   const ViewportInteractionInput& input,
                                                   const ViewportInteractionOptions& options);

 private:
  struct EntityDrag {
    uint64_t entity_id = Entity::kInvalidId;
    Vec pointer_offset;
  };

  absl::StatusOr<ViewportInteractionResult> UpdateTile(Level& level,
                                                       const ViewportInteractionInput& input,
                                                       int tile_id);
  absl::StatusOr<ViewportInteractionResult> UpdateTerrain(Level& level,
                                                          const ViewportInteractionInput& input,
                                                          const ViewportInteractionOptions& options);
  absl::StatusOr<ViewportInteractionResult> UpdateEntity(Level& level,
                                                         const ViewportInteractionInput& input,
                                                         const ViewportInteractionOptions& options);

  // The most recent cell written during the current drag. Erasing a cell that
  // was just painted is a different operation, so the flag is part of the key.
  struct PaintedCell {
    TileCoordinate coordinate;
    bool erasing = false;
  };

  // Returns true when this write differs from the last one in the current drag,
  // and records it. Without this a held button rewrites the same cell every
  // frame, which for terrain also re-resolves its eight neighbours.
  bool ClaimPaintCell(TileCoordinate coordinate, bool erasing);

  std::optional<uint64_t> next_entity_id_ = 1;
  std::optional<EntityDrag> entity_drag_;
  std::optional<PaintedCell> last_painted_;
};

}  // namespace zebes
