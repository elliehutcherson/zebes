#pragma once

#include "absl/status/statusor.h"
#include "editor/canvas/canvas.h"
#include "objects/collider.h"

namespace zebes {

// Draws a collider's polygons on the editor canvas and lets their vertices be
// dragged. Holds the collider by reference and edits it in place, so a drag is
// visible to whoever owns it immediately; Render reports whether it moved.
class CanvasCollider {
 public:
  explicit CanvasCollider(Collider& collider) : collider_(collider) {}

  // Returns true if the collider was modified (dragged).
  absl::StatusOr<bool> Render(Canvas& canvas, bool input_allowed);

  void ResetDragIndex() {
    drag_polygon_index_ = -1;
    drag_vertex_index_ = -1;
  }

  void Clear() {
    is_dragging_ = false;
    drag_acc_x_ = 0.0;
    drag_acc_y_ = 0.0;
  }

 private:
  // Moves val by delta, keeping the remainder in accumulator. With snap on, the
  // value only ever lands on integers, and the leftover is carried to the next
  // frame instead of discarded -- otherwise a slow drag would never reach the
  // next grid line, since each frame's delta rounds away to nothing on its own.
  static void ApplyDrag(double& val, double& accumulator, double delta, bool snap);

  Collider& collider_;

  bool is_dragging_ = false;
  double drag_acc_x_ = 0.0;
  double drag_acc_y_ = 0.0;
  int drag_polygon_index_ = -1;
  int drag_vertex_index_ = -1;

  double animation_timer_ = 0.0;
  bool is_animating_ = false;
};

}  // namespace zebes
