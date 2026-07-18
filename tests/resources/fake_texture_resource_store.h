#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "resources/texture_resource_store.h"

namespace zebes {

class FakeTextureResourceStore : public TextureResourceStore {
 public:
  absl::StatusOr<TextureHandle> Load(const std::string& path) override {
    loaded_paths.push_back(path);
    return MakeHandle(next_id_++);
  }

  absl::Status Unload(TextureHandle handle) override {
    if (!handle) return absl::InvalidArgumentError("Invalid texture handle");
    unloaded_ids.push_back(handle.id());
    return absl::OkStatus();
  }

  std::vector<std::string> loaded_paths;
  std::vector<uint64_t> unloaded_ids;

 private:
  uint64_t next_id_ = 1;
};

}  // namespace zebes
