#pragma once

#include "engine/input_types.h"

namespace zebes {

class ImGuiWrapper;
class SdlWrapper;

// SDL adapter that translates native events and keyboard state into an
// engine-owned snapshot. ImGui event forwarding stays at this platform edge.
class SdlInputSource : public InputSource {
 public:
  SdlInputSource(SdlWrapper& sdl, ImGuiWrapper& imgui);

  InputSnapshot Poll() override;

 private:
  SdlWrapper& sdl_;
  ImGuiWrapper& imgui_;
};

}  // namespace zebes
