#pragma once

#include "SDL_render.h"
#include "engine/texture_handle.h"
#include "platform/sdl/sdl_texture_store.h"

namespace zebes {

// The only conversion point between the engine's opaque texture identifier
// and SDL's renderer resource type.
struct SdlTextureHandleAdapter {
  static SDL_Texture* ToNative(TextureHandle handle) {
    const void* owner = TextureHandleAccess::Owner(handle);
    if (owner == nullptr) return nullptr;
    return static_cast<const SdlTextureStore*>(owner)->Resolve(handle);
  }
};

}  // namespace zebes
