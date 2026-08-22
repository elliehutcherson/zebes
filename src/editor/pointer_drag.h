#pragma once

#include <optional>

#include "objects/vec.h"

namespace zebes {

// Platform-neutral state for dragging one world-space point with a pointer.
// The caller owns picking, button translation, constraints, and the object;
// this class owns only the grab offset and gesture lifetime. Keeping those
// concerns separate lets every Canvas-backed editor use identical no-jump
// movement without depending on ImGui.
class PointerDragController {
 public:
  void Begin(Vec pointer_position, Vec object_position) {
    pointer_offset_ = {
        pointer_position.x - object_position.x,
        pointer_position.y - object_position.y,
    };
  }

  // Returns the requested object position while the gesture remains held.
  // Releasing ends the gesture and deliberately returns no final duplicate.
  std::optional<Vec> Update(Vec pointer_position, bool primary_down) {
    if (!pointer_offset_.has_value()) return std::nullopt;
    if (!primary_down) {
      Reset();
      return std::nullopt;
    }
    return Vec{
        pointer_position.x - pointer_offset_->x,
        pointer_position.y - pointer_offset_->y,
    };
  }

  bool active() const { return pointer_offset_.has_value(); }
  void Reset() { pointer_offset_.reset(); }

 private:
  std::optional<Vec> pointer_offset_;
};

}  // namespace zebes
