#pragma once

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "engine/texture_handle.h"

namespace zebes {

// Renderer-independent ownership boundary for runtime texture resources.
class TextureResourceStore {
 public:
  virtual ~TextureResourceStore() = default;

  virtual absl::StatusOr<TextureHandle> Load(const std::string& path) = 0;

  // Loads tightly packed RGBA8 pixels with no file behind them.
  //
  // Derived terrain grows its atlas while a level is painted, and the artwork
  // has to be on screen the moment the cell referencing it is. Making that
  // durable is a separate question with a separate answer -- unsaved paint
  // stays unsaved, artwork included -- so this deliberately does not touch
  // disk.
  virtual absl::StatusOr<TextureHandle> LoadFromPixels(int width, int height,
                                                       absl::Span<const uint8_t> pixels) = 0;

  virtual absl::Status Unload(TextureHandle handle) = 0;

 protected:
  TextureHandle MakeHandle(uint64_t id) const { return TextureHandleAccess::Create(id, this); }
};

}  // namespace zebes
