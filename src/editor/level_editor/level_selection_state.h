#pragma once

#include <cstdint>

namespace zebes {

// Unified selection state for the editor
struct SelectionState {
  enum class Type : uint8_t {
    kNone = 0,
    kLevel = 1,
    kZone = 2,
    kTheme = 3,
    kLayer = 8,
    kEntity = 9,
  };

  Type type = Type::kNone;

  // Context Data
  int zone_index = -1;
  int theme_id = -1;
  int layer_index = -1;
  uint64_t entity_id = 0;  // Entity::kInvalidId

  // Clears selection
  void Clear() {
    type = Type::kNone;
    zone_index = -1;
    theme_id = -1;
    layer_index = -1;
    entity_id = 0;
  }
};

}  // namespace zebes