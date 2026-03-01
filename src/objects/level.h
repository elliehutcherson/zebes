#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "objects/camera.h"
#include "objects/entity.h"

namespace zebes {

// Definition of a Tile Chunk (Optimized Storage)
struct TileChunk {
  static constexpr int kSize = 32;
  std::array<int, kSize * kSize> tiles;
};

// Definition of Parallax Layer (Visuals)
struct ParallaxLayer {
  std::string name;
  std::string texture_id;
  Vec scroll_factor;
  Vec offset;
  float base_scale = 1.0f;
  bool repeat_x = false;
  bool repeat_y = false;
};

struct ParallaxTheme {
  int id = 0;
  std::string name;
  std::vector<ParallaxLayer> layers;
};

struct ParallaxZone {
  int id = 0;
  std::string name;
  int theme_id = -1;
  // 2D Boundaries (World Coordinates)
  Vec min_point;
  Vec max_point;

  // Transition settings
  Vec fade_length;  // x = horizontal fade width, y = vertical fade height
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

  // Not part of the definition, but a runtime component.
  Camera camera;

  // TILE DATA (The World)
  // Stored in chunks for memory efficiency
  absl::flat_hash_map<int64_t, TileChunk> tile_chunks;

  // SPATIAL LOOKUP (Optimization)
  // A separate map to find entities by ID without looping through the vector
  std::map<uint64_t, Entity> entities;

  // ENVIRONMENT
  // Parallax layers, background color, music track ID, etc.'
  std::map<int, ParallaxTheme> themes;
  std::vector<ParallaxZone> zones;

  std::vector<ParallaxLayer> parallax_layers;

  std::string name_id() const { return absl::StrCat(name, "-", id); }

  void AddEntity(Entity entity) { entities[entity.id] = std::move(entity); }
};

}  // namespace zebes
