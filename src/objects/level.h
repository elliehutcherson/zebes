#pragma once

#include <cstdint>
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
  // Not part of the definition, but a runtime component.
  Camera camera;

  // BOUNDARIES
  double width = 0;
  double height = 0;

  // GAMEPLAY
  Vec spawn_point;  // Where to start

  // TILE DATA (The World)
  // Stored in chunks for memory efficiency
  absl::flat_hash_map<int64_t, TileChunk> tile_chunks;

  // ENTITY DATA (The Population)
  // The Level OWNS the entities. using unique_ptr ensures auto-cleanup.
  std::vector<std::unique_ptr<Entity>> entities;

  // SPATIAL LOOKUP (Optimization)
  // A separate map to find entities by ID without looping through the vector
  std::unordered_map<uint64_t, Entity*> entity_lookup;

  // ENVIRONMENT
  // Parallax layers, background color, music track ID, etc.
  std::vector<ParallaxLayer> parallax_layers;

  std::string name_id() const { return absl::StrCat(name, "-", id); }

  // Returns a deep copy of the level.
  // Entities are cloned (deep copied), other data is copied by value.
  Level GetCopy() const {
    Level copy = {
        .id = id,
        .name = name,
        .camera = camera,
        .tile_chunks = tile_chunks,
        .parallax_layers = parallax_layers,
    };

    // Reserve entity space
    copy.entities.reserve(entities.size());

    // Deep copy entities
    for (const std::unique_ptr<Entity>& entity : entities) {
      if (!entity) continue;

      copy.AddEntity(std::make_unique<Entity>(*entity));
    }

    return copy;
  }

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
