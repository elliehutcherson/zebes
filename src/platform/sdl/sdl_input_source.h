#pragma once

#include "SDL_events.h"
#include "absl/functional/any_invocable.h"
#include "common/sdl_wrapper.h"
#include "engine/input_types.h"

namespace zebes {

// SDL adapter that translates native events and keyboard state into an
// engine-owned snapshot. An optional observer lets another platform adapter,
// such as ImGui, inspect each event without entering runtime-only consumers.
class SdlInputSource : public InputSource {
 public:
  using EventObserver = absl::AnyInvocable<void(const SDL_Event& event)>;

  explicit SdlInputSource(SdlWrapper& sdl, EventObserver event_observer = {});

  InputSnapshot Poll() override;

 private:
  SdlWrapper& sdl_;
  EventObserver event_observer_;
};

}  // namespace zebes
