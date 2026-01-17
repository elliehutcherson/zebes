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
  };

  explicit Canvas(Options options);
  explicit Canvas(GuiInterface* gui);

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

 private:
  GuiInterface* gui_;
  bool snap_grid_ = false;

  Camera* camera_ = nullptr;
  ImVec2 p0_;  // Screen position of canvas top-left
  ImDrawList* draw_list_ = nullptr;
};

}  // namespace zebes