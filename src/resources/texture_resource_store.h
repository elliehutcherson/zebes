#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/texture_handle.h"

namespace zebes {

// Renderer-independent ownership boundary for runtime texture resources.
class TextureResourceStore {
 public:
  virtual ~TextureResourceStore() = default;

  virtual absl::StatusOr<TextureHandle> Load(const std::string& path) = 0;
  virtual absl::Status Unload(TextureHandle handle) = 0;

 protected:
  TextureHandle MakeHandle(uint64_t id) const { return TextureHandleAccess::Create(id, this); }
};

}  // namespace zebes
