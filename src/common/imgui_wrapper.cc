#include "common/imgui_wrapper.h"

#include "imgui_impl_sdl2.h"

namespace zebes {

namespace {

class ImGuiWrapperImpl : public ImGuiWrapper {
 public:
  bool ProcessEvent(const SDL_Event* event) override { return ImGui_ImplSDL2_ProcessEvent(event); }
};

}  // namespace

std::unique_ptr<ImGuiWrapper> ImGuiWrapper::Create() {
  return std::make_unique<ImGuiWrapperImpl>();
}

}  // namespace zebes
