#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/sprite_interface.h"

namespace zebes {

class DbInterface {
 public:
  virtual ~DbInterface() = default;

  virtual absl::StatusOr<uint16_t> InsertTexture(const std::string &path) = 0;

  virtual absl::Status DeleteTexture(const std::string &path) = 0;

  virtual absl::StatusOr<bool> TextureExists(const std::string &path) = 0;

  virtual absl::StatusOr<std::vector<std::string>> GetAllTexturePaths() = 0;

  virtual absl::StatusOr<int> NumSpritesWithTexture(
      const std::string &path) = 0;

  virtual absl::StatusOr<uint16_t> InsertSprite(
      const SpriteConfig &sprite_config) = 0;

  virtual absl::Status DeleteSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<SpriteConfig> GetSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<std::vector<SpriteConfig>> GetAllSprites() = 0;

  virtual absl::StatusOr<uint16_t> InsertSubSprite(
      uint16_t sprite_id, const SubSprite &sub_sprite) = 0;

  virtual absl::Status DeleteSubSprite(uint16_t sub_sprite_id) = 0;

  virtual absl::StatusOr<SubSprite> GetSubSprite(uint16_t sub_sprite_id) = 0;

  virtual absl::StatusOr<std::vector<SubSprite>> GetSubSprites(
      uint16_t sprite_id) = 0;
};

}  // namespace zebes
