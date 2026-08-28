#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "objects/entity.h"

namespace zebes {

// Definition of a Tile Chunk (Optimized Storage)
struct TileChunk {
  static constexpr int kSize = 32;
  // Zero-initialized: zero means "no tile", so a default-constructed chunk has
  // to read as empty rather than as whatever the memory held.
  std::array<int, kSize * kSize> tiles{};

  bool operator==(const TileChunk& other) const = default;
};

struct ParallaxZone {
  int id = 0;
  std::string name;
  std::string theme_id;
  // 2D Boundaries (World Coordinates)
  Vec min_point;
  Vec max_point;

  // Transition settings
  Vec fade_length;  // x = horizontal fade width, y = vertical fade height

  bool operator==(const ParallaxZone& other) const = default;
};

// One theme participating in the environment at a world-space reference
// point. The zone ID remains available for editor status and future runtime
// diagnostics without making either consumer retain a ParallaxZone pointer.
struct ResolvedParallaxTheme {
  int zone_id = -1;
  std::string theme_id;

  bool operator==(const ResolvedParallaxTheme& other) const = default;
};

// Platform-neutral result of resolving one hard-cut zone or one supported
// two-theme fade. active_zone_id preserves authored half-open containment;
// primary and secondary preserve stable left-to-right or top-to-bottom render
// order across a seam. secondary_weight is normalized to [0, 1].
struct ResolvedParallaxEnvironment {
  int active_zone_id = -1;
  ResolvedParallaxTheme primary;
  std::optional<ResolvedParallaxTheme> secondary;
  double secondary_weight = 0.0;

  bool operator==(const ResolvedParallaxEnvironment& other) const = default;
};

// Identifies one 32x32 TileChunk in WorldLayer::tile_chunks. Chunk-key
// encoding is part of the persisted level model, not a viewport concern.
struct TileChunkCoordinate {
  int x = 0;
  int y = 0;

  constexpr bool operator<(const TileChunkCoordinate& other) const {
    if (y != other.y) return y < other.y;
    return x < other.x;
  }
};

int64_t ChunkKey(int chunk_x, int chunk_y);
TileChunkCoordinate DecodeChunkKey(int64_t key);

// One persistent world-space depth slice. Layers are stored back to front in
// Level::layers; within one layer tiles draw before entities, and entity
// sort_order breaks ties among those entities.
//
// This is deliberately not a ParallaxLayer. Parallax layers are camera-relative
// textures owned by reusable themes, while a WorldLayer owns level geometry and
// placed entities in world coordinates.
struct WorldLayer {
  int id = -1;
  std::string name;
  absl::flat_hash_map<int64_t, TileChunk> tile_chunks;
  std::map<uint64_t, Entity> entities;

  bool operator==(const WorldLayer& other) const = default;
};

struct Level {
  std::string id;
  std::string name;

  // The UUID of the tileset whose tile definitions back tile_chunks.
  // Empty string means no tileset is associated with this level.
  std::string tileset_id;

  // World-space pixel size each tile is rendered at. Independent of the source
  // tile dimensions in the tileset atlas (Tileset::tile_width / tile_height).
  int tile_render_width = 16;
  int tile_render_height = 16;

  // BOUNDARIES
  double width = 0;
  double height = 0;

  // GAMEPLAY
  Vec spawn_point;  // Where to start

  // WORLD CONTENT
  // Ordered back to front. A level always has at least one layer so every
  // placement operation has an explicit destination.
  std::vector<WorldLayer> layers = {WorldLayer{.id = 0, .name = "Base"}};

  // ENVIRONMENT
  // Zones own placement and transition geometry. Their artwork is a reusable
  // ParallaxTheme resource resolved through `theme_id` by the API/renderer.
  std::vector<ParallaxZone> zones;

  // Value equality over every authored field, tile chunks included. The editor
  // compares a level against the copy it opened to know whether closing would
  // discard work, so equality has to mean identical in every respect a save
  // would write.
  bool operator==(const Level& other) const = default;

  std::string name_id() const { return absl::StrCat(name, "-", id); }

  // Adds an entity to one layer while preserving the level-wide entity-ID
  // invariant. Refuses an invalid layer, ID zero, or a duplicate ID.
  absl::Status AddEntity(int layer_id, Entity entity);
};

WorldLayer* FindWorldLayer(Level& level, int layer_id);
const WorldLayer* FindWorldLayer(const Level& level, int layer_id);

// Finds an entity across every layer. Entity IDs are unique within the whole
// level, so the result is unambiguous.
Entity* FindEntity(Level& level, uint64_t entity_id);
const Entity* FindEntity(const Level& level, uint64_t entity_id);
WorldLayer* FindEntityLayer(Level& level, uint64_t entity_id);
const WorldLayer* FindEntityLayer(const Level& level, uint64_t entity_id);

const ParallaxZone* FindParallaxZoneById(const std::vector<ParallaxZone>& zones, int zone_id);

// Resolves the active zone at reference_point using half-open bounds and, when
// the point lies in one validated shared-edge band, its stable two-theme fade.
// No containing zone is a normal empty result. Invalid or ambiguous geometry
// is an error rather than an arbitrary render choice.
absl::StatusOr<std::optional<ResolvedParallaxEnvironment>> ResolveParallaxEnvironment(
    const std::vector<ParallaxZone>& zones, Vec reference_point);

absl::StatusOr<int> NextAvailableWorldLayerId(const Level& level);
absl::StatusOr<uint64_t> NextAvailableEntityId(const Level& level);

// Moves an entity without changing its ID. Validation happens before mutation,
// so failure never leaves it absent from both layers or present in both.
absl::Status MoveEntityToLayer(Level& level, uint64_t entity_id, int destination_layer_id);

// Intrinsic definition validation shared by loading and saving. Catalog-wide
// constraints such as unique level names remain LevelManager's responsibility.
absl::Status ValidateLevel(const Level& level);

}  // namespace zebes
