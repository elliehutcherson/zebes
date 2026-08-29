#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "common/config.h"
#include "common/sdl_wrapper.h"
#include "engine/input_types.h"
#include "game/game_renderer.h"
#include "objects/game_view.h"
#include "platform/sdl/sdl_game_renderer.h"
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_subsystem.h"
#include "platform/sdl/sdl_texture_store.h"
#include "resources/texture_resource_store.h"

namespace zebes {

// SDL composition root for the standalone game. The host owns every native
// object and exposes only Zebes interfaces to GameRuntime.
class SdlGameHost {
 public:
  struct Options {
    WindowConfig window;
    GameViewSize game_view;
  };

  static absl::StatusOr<std::unique_ptr<SdlGameHost>> Create(Options options);

  SdlGameHost(const SdlGameHost&) = delete;
  SdlGameHost& operator=(const SdlGameHost&) = delete;

  InputSource& input_source() { return *input_source_; }
  TextureResourceStore& texture_resources() { return *texture_store_; }
  GameRenderer& renderer() { return *renderer_; }

 private:
  SdlGameHost() = default;
  absl::Status Init(const Options& options);

  // Reverse declaration order is destruction order. The subsystem shuts down
  // only after renderer, input, textures, window, and native renderer are gone.
  std::unique_ptr<SdlSubsystem> subsystem_;
  std::unique_ptr<SdlWrapper> sdl_;
  std::unique_ptr<SdlTextureStore> texture_store_;
  std::unique_ptr<SdlInputSource> input_source_;
  std::unique_ptr<SdlGameRenderer> renderer_;
};

}  // namespace zebes
