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
    std::optional<Vec> world_min;
    std::optional<Vec> world_max;
  };

  explicit Canvas(Options options);

  void Begin(const char* id, const ImVec2& size, Camera& camera);
  void End();

  // The key coordinate math
  ImVec2 WorldToScreen(const Vec& v) const;
  Vec ScreenToWorld(const ImVec2& p) const;

  // Renders grid, axis, and rulers
  void DrawGrid();

  // Handles Pan/Zoom inputs automatically
  void HandleInput();

  // Getters for tools to use
  float GetZoom() const;
  bool GetSnap() const { return snap_grid_; }
  ImDrawList* GetDrawList() { return draw_list_; }

  // Method to update bounds. Should be called this before Begin or when level changes.
  void SetWorldBounds(Vec min, Vec max);

 private:
  // Helper to draw ruler ticks and grid lines for a specific axis
  void DrawRulerAndGrid(double start_val, double step, double max_dim, bool is_x_axis);

  // Helper to force camera inside bounds
  void ClampCamera();

  GuiInterface* gui_;
  bool snap_grid_ = false;

  Camera* camera_ = nullptr;
  ImVec2 p0_;  // Screen position of canvas top-left
  ImDrawList* draw_list_ = nullptr;

  // Bounds State
  std::optional<Vec> world_min_;
  std::optional<Vec> world_max_;
};

}  // namespace zebes