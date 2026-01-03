#pragma once

#include "editor/animator.h"
#include "editor/canvas.h"
#include "objects/sprite.h"

namespace zebes {

// Handles rendering and interacting with a sprite on the editor canvas.
class CanvasSprite {
 public:
  CanvasSprite(Sprite& sprite) : sprite_(sprite) {}

  // Renders the sprite and handles input.
  // Returns true if the sprite was modified (dragged).
  absl::StatusOr<bool> Render(Canvas& canvas, int frame_index, bool input_allowed);

  void SetIsAnimating(bool is_animating);

  void Clear() {
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

  Sprite& sprite_;
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