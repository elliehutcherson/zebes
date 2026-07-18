#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/sdl_wrapper.h"
#include "engine/texture_handle.h"
#include "resources/texture_resource_store.h"

namespace zebes {

class SdlTextureStore : public TextureResourceStore {
 public:
  explicit SdlTextureStore(SdlWrapper& sdl) : sdl_(sdl) {}
  ~SdlTextureStore() override;

  absl::StatusOr<TextureHandle> Load(const std::string& path) override;
  absl::Status Unload(TextureHandle handle) override;

 private:
  SDL_Texture* Resolve(TextureHandle handle) const {
    if (TextureHandleAccess::Owner(handle) != this) return nullptr;
    auto it = textures_.find(handle.id());
    return it == textures_.end() ? nullptr : it->second;
  }

  SdlWrapper& sdl_;
  uint64_t next_id_ = 1;
  std::unordered_map<uint64_t, SDL_Texture*> textures_;

  friend struct SdlTextureHandleAdapter;
};

}  // namespace zebes
