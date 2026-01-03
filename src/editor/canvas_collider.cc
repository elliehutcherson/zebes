#include "editor/canvas_collider.h"

namespace zebes {

absl::StatusOr<bool> CanvasCollider::Render(Canvas& canvas, bool input_allowed) {
  ImDrawList* draw_list = canvas.GetDrawList();

  for (int i = 0; i < collider_.polygons.size(); ++i) {
    Polygon& poly = collider_.polygons[i];
    if (poly.empty()) continue;

    std::vector<ImVec2> points;
    for (const Vec& v : poly) {
      // --- COORD CONVERSION VIA CANVAS ---
      points.push_back(canvas.WorldToScreen(v));
    }

    ImU32 color =
        drag_polygon_index_ == i ? IM_COL32(255, 0, 0, 255) : IM_COL32(200, 200, 200, 255);

    for (size_t j = 0; j < points.size(); ++j) {
      draw_list->AddLine(points[j], points[(j + 1) % points.size()], color, 2.0f);
    }

    for (size_t j = 0; j < points.size(); ++j) {
      ImVec2 p = points[j];
      draw_list->AddCircleFilled(p, 4.0f, color);

      // Vertex Interaction
      ImVec2 mouse_pos = ImGui::GetMousePos();
      float dist_sq =
          (mouse_pos.x - p.x) * (mouse_pos.x - p.x) + (mouse_pos.y - p.y) * (mouse_pos.y - p.y);
      bool near_vertex = dist_sq < 64.0f;

      if (near_vertex && input_allowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        is_dragging_ = true;
        drag_acc_x_ = 0;
        drag_acc_y_ = 0;
        drag_polygon_index_ = i;
        drag_vertex_index_ = j;
      }
    }
  }

  if (is_dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    Polygon& poly = collider_.polygons[drag_polygon_index_];
    Vec& v = poly[drag_vertex_index_];

    // --- ZOOM COMPENSATION VIA CANVAS ---
    double dx = ImGui::GetIO().MouseDelta.x / canvas.GetZoom();
    double dy = ImGui::GetIO().MouseDelta.y / canvas.GetZoom();

    ApplyDrag(v.x, drag_acc_x_, dx, canvas.GetSnap());
    ApplyDrag(v.y, drag_acc_y_, dy, canvas.GetSnap());
  }

  if (is_dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    is_dragging_ = false;
  }

  return is_dragging_;
}

void CanvasCollider::ApplyDrag(double& val, double& accumulator, double delta, bool snap) {
  static constexpr double kDragThreshold = 1e-4;

  accumulator += delta;
  if (!snap) {
    // Free movement
    val += accumulator;
    accumulator = 0;
    return;
  }

  // Snap to nearest integer
  double target = std::round(val + accumulator);
  double diff = target - val;
  // Apply if there is significant change
  if (std::abs(diff) > kDragThreshold) {
    val += diff;
    accumulator -= diff;
  }
}

}  // namespace zebes