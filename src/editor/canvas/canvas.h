#pragma once

#include "editor/gui_interface.h"
#include "imgui.h"
#include "objects/camera.h"
#include "objects/vec.h"

namespace zebes {

// A pannable, zoomable drawing surface with rulers and a grid, used by the
// editor tabs that show something in world space.
//
// Use is a strict Begin/End pair, and no other method means anything outside
// one: Begin borrows the caller's Camera and End drops it. The camera belongs
// to the caller, which is what lets a tab keep its view across frames while the
// canvas holds no view state itself.
//
// Begin writes the camera's viewport dimensions, since the canvas is the only
// thing that knows its own size and clamping depends on it. The camera is then
// clamped twice per frame: in Begin, so a resize draws in bounds immediately,
// and at the end of HandleInput, so this frame's pan or zoom is corrected
// before anything draws with it.
//
// World bounds are optional. Without them the camera pans freely; with them the
// view cannot leave the level, and zoom gains a lower limit so the viewport is
// never larger than the world.
class Canvas {
 public:
  static constexpr CameraZoomRange NavigationZoomRange() {
    return {.minimum = 0.1, .maximum = 10.0};
  }

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

  ImVec2 WorldToScreen(const Vec& v) const;
  Vec ScreenToWorld(const ImVec2& p) const;
  Vec SnapToGrid(Vec world_pos) const;

  void DrawGrid();

  // Applies wheel zoom, keyboard pan, and middle-drag pan to the camera.
  void HandleInput();

  float GetZoom() const;
  bool GetSnap() const { return snap_grid_; }
  float GetGridSize() const { return grid_size_; }
  ImDrawList* GetDrawList() { return draw_list_; }

  // Call outside a Begin/End pair; the new bounds take effect at the next
  // Begin, which is where the camera is clamped into them.
  void SetWorldBounds(Vec min, Vec max);

  void SetSnap(bool snap) { snap_grid_ = snap; }

  // World units between grid lines, not pixels.
  void SetGridSize(float size) { grid_size_ = size; }

 private:
  void DrawRulerAndGrid(double start_val, double step, double max_dim, bool is_x_axis);

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

  // Unset means unbounded panning; see the class comment.
  std::optional<Vec> world_min_;
  std::optional<Vec> world_max_;

  // Cursor position on the previous frame of a middle-button drag. Held only
  // for the duration of the drag: a drag that begins on this canvas keeps
  // panning it even once the pointer leaves, which is what makes panning to
  // off-screen content possible at all.
  std::optional<ImVec2> pan_cursor_;
};

}  // namespace zebes
