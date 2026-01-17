#pragma once

#include "absl/status/status.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "objects/camera.h"
#include "objects/level.h"

namespace zebes {

class ViewportTab {
 public:
  explicit ViewportTab(Api& api, GuiInterface* gui);

  // Main render loop for the viewport tab.
  // Handles grid rendering, parallax background rendering, and level bounds.
  absl::Status Render(Level& level);

  // Resets the viewport view (zoom/offset) to default.
  void Reset();

 private:
  // Renders the parallax layers for the given level.
  // Each layer is rendered with its specific scroll factor relative to the camera view.
  void RenderParallax(const Level& level);

  // Renders the boundaries of the level and potentially the camera start box.
  void RenderLevelBounds(const Level& level);

  Api& api_;
  GuiInterface* gui_;
  Canvas canvas_;
  Camera camera_;
};

}  // namespace zebes
