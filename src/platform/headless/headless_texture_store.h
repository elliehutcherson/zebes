#pragma once

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "resources/texture_resource_store.h"

namespace zebes {

// Texture ownership adapter for tools that need the production managers but no
// SDL window or GPU. File loads decode the PNG once so a headless workspace
// rejects the same missing or malformed artwork that would stop the editor.
// Pixel storage remains on disk and is read through TextureManager when needed.
class HeadlessTextureStore : public TextureResourceStore {
 public:
  absl::StatusOr<TextureHandle> Load(const std::string& path) override;
  absl::StatusOr<TextureHandle> LoadFromPixels(int width, int height,
                                               absl::Span<const uint8_t> pixels) override;
  absl::Status Unload(TextureHandle handle) override;

 private:
  uint64_t next_id_ = 1;
  absl::flat_hash_set<uint64_t> loaded_ids_;
};

}  // namespace zebes
