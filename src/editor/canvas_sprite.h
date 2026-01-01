#pragma once

#include <optional>

#include "editor/animator.h"
#include "editor/canvas.h"
#include "objects/sprite.h"

namespace zebes {

// Handles rendering and interacting with a sprite on the editor canvas.
class CanvasSprite {
 public:
  CanvasSprite(Canvas* canvas) : canvas_(canvas) {}

  // Renders the sprite and handles input.
  // Returns true if the sprite was modified (dragged).
  absl::StatusOr<bool> Render(int frame_index, bool input_allowed);

  void SetIsAnimating(bool is_animating);

  void SetSprite(Sprite sprite);

  Sprite GetSprite() { return *sprite_; }

  void Clear() {
    sprite_ = std::nullopt;
    is_animating_ = false;
    is_dragging_ = false;
    drag_acc_x_ = 0.0;
    drag_acc_y_ = 0.0;
  }

 private:
  // Helper for smooth dragging
  void ApplyDrag(double& val, double& accumulator, double delta, bool snap);

  // Updates the animation ticks.
  void UpdateAnimation();

  Canvas* canvas_ = nullptr;
  std::optional<Sprite> sprite_ = std::nullopt;
  Animator animator_;

  // Dragging state
  bool is_dragging_ = false;
  double drag_acc_x_ = 0.0;
  double drag_acc_y_ = 0.0;

  // Animation state
  double animation_timer_ = 0.0;
  bool is_animating_ = false;
};

}  // namespace zebes