#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sprite_interface.h"

namespace zebes {

class DbInterface {
 public:
  virtual ~DbInterface() = default;

  // Sprite interfaces.
  virtual absl::StatusOr<uint16_t> InsertSprite(
      const SpriteConfig &sprite_config) = 0;

  virtual absl::Status DeleteSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<SpriteConfig> GetSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<std::vector<SubSprite>> GetSubSprites(
      uint16_t sprite_id) = 0;
};

}  // namespace zebes
