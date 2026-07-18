#include "platform/sdl/sdl_texture_store.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {

SdlTextureStore::~SdlTextureStore() {
  for (const auto& [id, texture] : textures_) {
    sdl_.DestroyTexture(texture);
  }
}

absl::StatusOr<TextureHandle> SdlTextureStore::Load(const std::string& path) {
  absl::StatusOr<SDL_Texture*> texture = sdl_.CreateTexture(path);
  if (!texture.ok()) return texture.status();

  const uint64_t id = next_id_++;
  textures_.emplace(id, *texture);
  return MakeHandle(id);
}

absl::Status SdlTextureStore::Unload(TextureHandle handle) {
  if (TextureHandleAccess::Owner(handle) != this) {
    return absl::InvalidArgumentError("Texture handle belongs to another resource store");
  }
  auto it = textures_.find(handle.id());
  if (it == textures_.end()) {
    return absl::NotFoundError(absl::StrCat("Texture handle not found: ", handle.id()));
  }
  sdl_.DestroyTexture(it->second);
  textures_.erase(it);
  return absl::OkStatus();
}

}  // namespace zebes
