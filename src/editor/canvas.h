#pragma once

#include "imgui.h"
#include "objects/vec.h"

namespace zebes {

class Canvas {
 public:
  struct Options {
    float zoom = 1.0f;
    ImVec2 offset = {0, 0};
    bool snap_grid = false;
  };

  explicit Canvas(Options options)
      : zoom_(options.zoom), offset_(options.offset), snap_grid_(options.snap_grid) {}

  void Begin(const char* id, const ImVec2& size);
  void End();
  void Reset();

  // The key coordinate math
  ImVec2 WorldToScreen(const Vec& v) const;
  Vec ScreenToWorld(const ImVec2& p) const;

  // Renders grid, axis, and rulers
  void DrawGrid();

  // Handles Pan/Zoom inputs automatically
  void HandleInput();

  // Getters for tools to use
  float GetZoom() const { return zoom_; }
  bool GetSnap() const { return snap_grid_; }
  ImDrawList* GetDrawList() { return draw_list_; }

 private:
  float zoom_ = 0.0;
  ImVec2 offset_ = {0, 0};
  bool snap_grid_ = false;

  ImVec2 origin_;  // Calculated every frame
  ImVec2 p0_;      // Screen position of canvas top-left
  ImDrawList* draw_list_ = nullptr;
};

}  // namespace zebes