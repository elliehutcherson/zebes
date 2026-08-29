#include "platform/sdl/sdl_game_host.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"
#include "platform/sdl/sdl_game_renderer.h"
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_subsystem.h"
#include "platform/sdl/sdl_texture_store.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SdlGameHost>> SdlGameHost::Create(Options options) {
  auto host = std::unique_ptr<SdlGameHost>(new SdlGameHost());
  RETURN_IF_ERROR(host->Init(options));
  return host;
}

absl::Status SdlGameHost::Init(const Options& options) {
  ASSIGN_OR_RETURN(subsystem_, SdlSubsystem::Create());
  ASSIGN_OR_RETURN(sdl_, SdlWrapper::Create(options.window));
  texture_store_ = std::make_unique<SdlTextureStore>(*sdl_);
  input_source_ = std::make_unique<SdlInputSource>(*sdl_);
  ASSIGN_OR_RETURN(renderer_, SdlGameRenderer::Create(*sdl_, options.game_view));
  return absl::OkStatus();
}

}  // namespace zebes
