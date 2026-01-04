#pragma once

#include <memory>

#include "SDL.h"

namespace zebes {

// Wrapper around ImGui SDL2 event processing to allow for mocking and
// dependency injection.
class ImGuiWrapper {
 public:
  virtual ~ImGuiWrapper() = default;

  // Factory function to create the default implementation.
  static std::unique_ptr<ImGuiWrapper> Create();

  // Processes an SDL event for ImGui.
  // Returns true if the event was handled by ImGui.
  virtual bool ProcessEvent(const SDL_Event* event) = 0;
};

}  // namespace zebes
