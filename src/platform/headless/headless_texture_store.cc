#include "platform/headless/headless_texture_store.h"

#include <cstddef>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_io.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<TextureHandle> HeadlessTextureStore::Load(const std::string& path) {
  RETURN_IF_ERROR(ReadPng(path).status());
  const uint64_t id = next_id_++;
  loaded_ids_.insert(id);
  return MakeHandle(id);
}

absl::StatusOr<TextureHandle> HeadlessTextureStore::LoadFromPixels(
    int width, int height, absl::Span<const uint8_t> pixels) {
  if (width <= 0 || height <= 0 || pixels.size() != static_cast<size_t>(width) * height * 4) {
    return absl::InvalidArgumentError("headless texture pixels have invalid dimensions");
  }
  const uint64_t id = next_id_++;
  loaded_ids_.insert(id);
  return MakeHandle(id);
}

absl::Status HeadlessTextureStore::Unload(TextureHandle handle) {
  if (TextureHandleAccess::Owner(handle) != this) {
    return absl::InvalidArgumentError("texture handle belongs to another resource store");
  }
  if (!loaded_ids_.erase(handle.id())) {
    return absl::NotFoundError(absl::StrCat("texture handle not found: ", handle.id()));
  }
  return absl::OkStatus();
}

}  // namespace zebes
