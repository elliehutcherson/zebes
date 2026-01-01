#pragma once

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "objects/entity.h"

namespace zebes {

// Definition of a Tile Chunk (Optimized Storage)
struct TileChunk {
  static constexpr int kSize = 16;
  std::array<int, kSize * kSize> tiles;
};

// Definition of Parallax Layer (Visuals)
struct ParallaxLayer {
  std::string texture_id;
  Vec scroll_factor;
  bool repeat_x = false;
};

struct Level {
  // 1. TILE DATA (The World)
  // Stored in chunks for memory efficiency
  absl::flat_hash_map<int64_t, TileChunk> tile_chunks;

  // 2. ENTITY DATA (The Population)
  // The Level OWNS the entities. using unique_ptr ensures auto-cleanup.
  std::vector<std::unique_ptr<Entity>> entities;

  // 3. SPATIAL LOOKUP (Optimization)
  // A separate map to find entities by ID without looping through the vector
  std::unordered_map<uint64_t, Entity*> entity_lookup;

  // 4. ENVIRONMENT
  // Parallax layers, background color, music track ID, etc.
  std::vector<ParallaxLayer> parallax_layers;

  // Helper to spawn entities
  void AddEntity(std::unique_ptr<Entity> e) {
    entity_lookup[e->id] = e.get();
    entities.push_back(std::move(e));
  }

  void RemoveEntity(uint64_t id) {
    // Logic to remove from vector and lookup map
  }
};

}  // namespace zebes
