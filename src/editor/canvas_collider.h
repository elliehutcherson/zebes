#pragma once

#include "absl/status/statusor.h"
#include "editor/canvas.h"
#include "objects/collider.h"

namespace zebes {

// Handles rendering and interacting with a sprite on the editor canvas.
class CanvasCollider {
 public:
  CanvasCollider(Canvas* canvas) : canvas_(canvas) {}

  // Returns true if the collider was modified (dragged).
  absl::StatusOr<bool> Render(bool input_allowed);

  void ResetDragIndex() {
    drag_polygon_index_ = -1;
    drag_vertex_index_ = -1;
  }

  void Clear() {
    collider_ = nullptr;
    is_dragging_ = false;
    drag_acc_x_ = 0.0;
    drag_acc_y_ = 0.0;
  }

  void SetCollider(Collider* collider) { collider_ = collider; }

 private:
  // Helper for smooth dragging
  void ApplyDrag(double& val, double& accumulator, double delta, bool snap);

  Canvas* canvas_ = nullptr;
  Collider* collider_ = nullptr;

  // Dragging state
  bool is_dragging_ = false;
  double drag_acc_x_ = 0.0;
  double drag_acc_y_ = 0.0;
  int drag_polygon_index_ = -1;
  int drag_vertex_index_ = -1;

  // Animation state
  double animation_timer_ = 0.0;
  bool is_animating_ = false;
};

}  // namespace zebes