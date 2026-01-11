#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "objects/camera.h"
#include "objects/entity.h"

namespace zebes {

// Definition of a Tile Chunk (Optimized Storage)
struct TileChunk {
  static constexpr int kSize = 16;
  std::array<int, kSize * kSize> tiles;
};

// Definition of Parallax Layer (Visuals)
struct ParallaxLayer {
  std::string name;
  std::string texture_id;
  Vec scroll_factor;
  bool repeat_x = false;
};

struct Level {
  std::string id;
  std::string name;

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
  // Parallax layers, background color, music track ID, etc.
  std::vector<ParallaxLayer> parallax_layers;

  std::string name_id() const { return absl::StrCat(name, "-", id); }

  void AddEntity(Entity entity) { entities[entity.id] = std::move(entity); }
};

}  // namespace zebes
