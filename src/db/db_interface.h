#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/sprite_interface.h"
#include "objects/texture.h"

namespace zebes {

class DbInterface {
 public:
  virtual ~DbInterface() = default;

  virtual absl::StatusOr<uint16_t> InsertTexture(const std::string& path) = 0;

  virtual absl::Status DeleteTexture(const std::string& path) = 0;

  virtual absl::StatusOr<bool> TextureExists(const std::string& path) = 0;

  virtual absl::StatusOr<std::vector<Texture>> GetAllTextures() = 0;

  virtual absl::StatusOr<std::vector<std::string>> GetAllTexturePaths() = 0;

  virtual absl::StatusOr<int> NumSpritesWithTexture(const std::string& path) = 0;

  virtual absl::StatusOr<uint16_t> InsertSprite(const SpriteConfig& sprite_config) = 0;

  virtual absl::Status DeleteSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<SpriteConfig> GetSprite(uint16_t sprite_id) = 0;

  virtual absl::StatusOr<std::vector<SpriteConfig>> GetAllSprites() = 0;

  virtual absl::StatusOr<uint16_t> InsertSpriteFrame(uint16_t sprite_id,
                                                     const SpriteFrame& sprite_frame) = 0;

  virtual absl::Status DeleteSpriteFrame(uint16_t sprite_frame_id) = 0;

  virtual absl::StatusOr<SpriteFrame> GetSpriteFrame(uint16_t sprite_frame_id) = 0;

  virtual absl::StatusOr<std::vector<SpriteFrame>> GetSpriteFrames(uint16_t sprite_id) = 0;

  virtual absl::Status UpdateSprite(const SpriteConfig& config) = 0;

  virtual absl::Status UpdateSpriteFrame(const SpriteFrame& frame) = 0;
};

}  // namespace zebes
