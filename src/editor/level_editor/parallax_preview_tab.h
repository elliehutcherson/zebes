#pragma once

#include "api/api.h"
#include "editor/gui_interface.h"

namespace zebes {

class ParallaxPreviewTab {
 public:
  ParallaxPreviewTab(Api& api, GuiInterface* gui);

  absl::Status Render(std::optional<std::string> texture_id);

  void Reset();

 private:
  void RenderZoom();

  Api& api_;
  GuiInterface* gui_;

  // Zoom control set on every frame
  float zoom_ = 1.0f;
  float preview_w_ = 512;
  float preview_h_ = 0;
};

}  // namespace zebes
