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
  int zone_id = -1;
  int theme_id = -1;
  int layer_index = -1;
  uint64_t entity_id = 0;  // Entity::kInvalidId

  // Clears selection
  void Clear() {
    type = Type::kNone;
    zone_id = -1;
    theme_id = -1;
    layer_index = -1;
    entity_id = 0;
  }

  // Applies an entity pick from the viewport. Clicking empty space clears an
  // entity selection, but preserves unrelated level, zone, theme, or layer
  // authoring context.
  void ApplyEntityPick(uint64_t picked_entity_id) {
    if (picked_entity_id == 0) {
      if (type == Type::kEntity) Clear();
      return;
    }

    Clear();
    type = Type::kEntity;
    entity_id = picked_entity_id;
  }
};

}  // namespace zebes
