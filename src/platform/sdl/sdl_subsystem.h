#pragma once

#include <memory>

#include "absl/status/statusor.h"

namespace zebes {

// RAII owner for the process-wide SDL subsystems used by Zebes. Declaring this
// before other SDL objects makes SDL_Quit run after those objects are gone.
class SdlSubsystem {
 public:
  static absl::StatusOr<std::unique_ptr<SdlSubsystem>> Create();

  ~SdlSubsystem();
  SdlSubsystem(const SdlSubsystem&) = delete;
  SdlSubsystem& operator=(const SdlSubsystem&) = delete;

 private:
  SdlSubsystem() = default;
};

}  // namespace zebes
