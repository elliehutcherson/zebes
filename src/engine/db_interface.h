#pragma once

#include "absl/status/statusor.h"
#include "sprite_interface.h"

namespace zebes {

class DbInterface {
 public:
  virtual ~DbInterface() = default;

  // Sprite interfaces.
  virtual void InsertSprite(const SpriteConfig& sprite_config) = 0;
  virtual void DeleteSprite(int id) = 0;
  virtual absl::StatusOr<SpriteConfig> GetSprite(int id) = 0;
};

}  // namespace zebes
