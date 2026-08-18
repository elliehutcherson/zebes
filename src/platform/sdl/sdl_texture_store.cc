#include "platform/sdl/sdl_texture_store.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {

SdlTextureStore::~SdlTextureStore() {
  for (const auto& [id, texture] : textures_) {
    sdl_.DestroyTexture(texture);
  }
}

absl::StatusOr<TextureHandle> SdlTextureStore::Load(const std::string& path) {
  ASSIGN_OR_RETURN(SDL_Texture * texture, sdl_.CreateTexture(path));

  const uint64_t id = next_id_++;
  textures_.emplace(id, texture);
  return MakeHandle(id);
}

absl::StatusOr<TextureHandle> SdlTextureStore::LoadFromPixels(int width, int height,
                                                              absl::Span<const uint8_t> pixels) {
  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (width <= 0 || height <= 0 || pixels.size() != expected) {
    return absl::InvalidArgumentError(absl::StrCat("expected ", expected, " bytes for a ", width,
                                                   "x", height, " RGBA image, got ",
                                                   pixels.size()));
  }

  ASSIGN_OR_RETURN(SDL_Texture * texture,
                   sdl_.CreateTextureFromPixels(width, height, pixels.data()));

  const uint64_t id = next_id_++;
  textures_.emplace(id, texture);
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
