#pragma once

#include "editor/gui_interface.h"
#include "imgui.h"
#include "objects/camera.h"
#include "objects/vec.h"

namespace zebes {

class Canvas {
 public:
  struct Options {
    GuiInterface* gui = nullptr;
    bool snap_grid = false;
    float grid_size = 50.0f;
    std::optional<Vec> world_min;
    std::optional<Vec> world_max;
  };

  explicit Canvas(Options options);

  void Begin(const char* id, const ImVec2& size, Camera& camera);
  void End();

  // The key coordinate math
  ImVec2 WorldToScreen(const Vec& v) const;
  Vec ScreenToWorld(const ImVec2& p) const;
  Vec SnapToGrid(Vec world_pos) const;

  // Renders grid, axis, and rulers
  void DrawGrid();

  // Handles Pan/Zoom inputs automatically
  void HandleInput();

  // Getters for tools to use
  float GetZoom() const;
  bool GetSnap() const { return snap_grid_; }
  float GetGridSize() const { return grid_size_; }
  ImDrawList* GetDrawList() { return draw_list_; }

  // Method to update bounds. Should be called this before Begin or when level changes.
  void SetWorldBounds(Vec min, Vec max);

  // Updates the snap to grid setting.
  void SetSnap(bool snap) { snap_grid_ = snap; }

  // Updates the grid cell size (world units per grid line).
  void SetGridSize(float size) { grid_size_ = size; }

 private:
  // Draws ruler ticks and grid lines for one axis.
  void DrawRulerAndGrid(double start_val, double step, double max_dim, bool is_x_axis);

  // Helper to force camera inside bounds
  void ClampCamera();

  // Drags the camera with the middle mouse button. Panning follows the cursor
  // exactly, so the world point under the pointer stays under it at any zoom.
  void HandleMousePan(bool is_hovered);

  GuiInterface* gui_;
  bool snap_grid_ = false;
  float grid_size_ = 50.0f;

  Camera* camera_ = nullptr;
  ImVec2 canvas_origin_;
  ImVec2 canvas_size_;
  ImVec2 content_origin_;
  ImVec2 content_size_;
  ImDrawList* draw_list_ = nullptr;

  // Bounds State
  std::optional<Vec> world_min_;
  std::optional<Vec> world_max_;

  // Cursor position on the previous frame of a middle-button drag. Held only
  // for the duration of the drag: a drag that begins on this canvas keeps
  // panning it even once the pointer leaves, which is what makes panning to
  // off-screen content possible at all.
  std::optional<ImVec2> pan_cursor_;
};

}  // namespace zebes
