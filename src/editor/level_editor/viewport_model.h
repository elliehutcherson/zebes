#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "engine/texture_handle.h"
#include "objects/blueprint.h"
#include "objects/entity.h"
#include "objects/entity_factory.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

// Opposing corners in level/world coordinates, measured in logical pixels.
struct WorldRect {
  Vec min;
  Vec max;

  constexpr bool IsValid() const { return max.x > min.x && max.y > min.y; }
};

// Identifies one tile cell in the level-wide grid. Multiplying x and y by the
// level's tile render dimensions produces the cell's world-space origin.
struct TileCoordinate {
  int x = 0;
  int y = 0;
};

// A sprite definition paired with the GPU handle for its texture.
//
// Sprite is a pure definition and names its texture by ID only, so the handle
// is resolved alongside it rather than stored on it. An invalid handle is an
// ordinary state: the sprite exists but its texture has not loaded.
struct ResolvedSprite {
  const Sprite* sprite = nullptr;
  TextureHandle texture;
};

// Sprites resolved for one frame, keyed by Sprite::id.
//
// Entities store only a sprite ID, so rendering and picking resolve them once
// per frame and pass the result explicitly rather than reading a pointer off
// the level definition.
using SpriteLookup = absl::flat_hash_map<std::string, ResolvedSprite>;

// Returns the world-space bounds used consistently for rendering and picking.
// A null sprite, or one with no frames, uses a centered 32x32 placeholder.
absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity, const Sprite* sprite);

// Returns the sprite and texture for an ID. An absent ID yields a default
// ResolvedSprite, whose null sprite callers already handle as "unresolved".
ResolvedSprite FindSprite(const SpriteLookup& sprites, const std::string& sprite_id);

// Returns the active entity whose bounding box contains world_pos, or
// Entity::kInvalidId. Entities with no resolvable sprite use a 32x32 fallback.
//
// When several overlap, the topmost wins: the greatest Entity::sort_order, and
// among equals the greatest ID. That is the order ComposeEntityRenderItems draws
// in, and picking must agree with it or a click selects something the user
// cannot see.
// A spatial index can replace the linear scan if profiling shows level size requires it.
absl::StatusOr<uint64_t> PickEntity(const std::map<uint64_t, Entity>& entities, Vec world_pos,
                                    const SpriteLookup& sprites);

// Converts a world position to the containing tile-grid coordinate. Rejects
// invalid cell dimensions, non-finite positions, and coordinates that cannot
// be represented by the level's integer chunk storage.
absl::StatusOr<TileCoordinate> WorldToTileCoordinate(Vec world_position, int tile_render_width,
                                                     int tile_render_height);

// Sets the tile at a world-tile coordinate, creating its chunk if necessary.
// A tile_id of zero erases the tile.
absl::Status SetTileAt(WorldLayer& layer, int tile_x, int tile_y, int tile_id);

// Returns the tile at a world-tile coordinate, or zero when its chunk is absent.
absl::StatusOr<int> GetTileAt(const WorldLayer& layer, int tile_x, int tile_y);

// What the palette is offering to paint this frame. Only one of the two modes
// is ever active, and each carries the tileset that owns it.
struct PaletteSelection {
  // Tileset shown by the Tiles palette, and the tile picked from it.
  const Tileset* tile_tileset = nullptr;
  const Tile* tile = nullptr;
  // Tileset shown by the Terrain palette, and the terrain picked from it.
  const Tileset* terrain_tileset = nullptr;
  std::optional<int> terrain_id;
};

// What the viewport may act on, after enforcing that a level's tiles are IDs
// into exactly one tileset.
struct PaletteBinding {
  // The tileset ID the level should carry after this frame.
  std::string tileset_id;
  // Selections the level can store. Empty when the palette is showing some
  // other tileset, since a bare tile ID from it would name different artwork
  // under the level's own tileset.
  const Tile* tile = nullptr;
  std::optional<int> terrain_id;
  // The tileset being refused, for the editor to explain. Null when there is
  // nothing to explain.
  const Tileset* rejected_tileset = nullptr;
};

// Resolves the binding for one frame. A level that has never been bound
// adopts the palette's tileset; otherwise the level's tileset is authoritative
// and is changed only through the level's own Tileset field, so that clicking
// a palette swatch can never silently repoint a level.
PaletteBinding ResolvePaletteBinding(const Level& level, const PaletteSelection& selection);

// Maps the blueprint's authored origin to the attachment point of the hovered
// tile: bottom-center for grounded, top-center for ceiling, and center for
// free placement. Sprite and collider bounds deliberately do not participate;
// both are already authored relative to this origin.
absl::StatusOr<Vec> SnapBlueprintOriginToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                              BlueprintPlacementMode placement_mode);

// Moves an existing authored origin to the nearest valid anchor for its
// placement mode. Unlike pointer placement, this considers the anchor lattice
// rather than a containing cell, so applying it repeatedly is idempotent.
// Grounded and ceiling origins lie on horizontal grid lines; free origins lie
// at cell centers. Every mode uses a cell-centered X coordinate.
absl::StatusOr<Vec> SnapBlueprintOriginToNearestGridAnchor(Vec origin, int tile_render_w,
                                                           int tile_render_h,
                                                           BlueprintPlacementMode placement_mode);

}  // namespace zebes
