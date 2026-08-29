#include "platform/sdl/sdl_subsystem.h"

#include <memory>

#include "SDL.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SdlSubsystem>> SdlSubsystem::Create() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    const absl::Status status =
        absl::InternalError(absl::StrCat("SDL initialization failed: ", SDL_GetError()));
    SDL_Quit();
    return status;
  }
  return std::unique_ptr<SdlSubsystem>(new SdlSubsystem());
}

SdlSubsystem::~SdlSubsystem() { SDL_Quit(); }

}  // namespace zebes
