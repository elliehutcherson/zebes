#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "common/config.h"
#include "db/db_interface.h"
#include "objects/sprite_interface.h"
#include "objects/texture.h"

namespace zebes {

class Api {
 public:
  struct Options {
    const GameConfig* config;
    DbInterface* db;
  };

  static absl::StatusOr<std::unique_ptr<Api>> Create(const Options& options);

  Api(const Options& options);
  ~Api() = default;

  absl::StatusOr<std::string> CreateTexture(const std::string& texture_path);

  absl::Status DeleteTexture(const std::string& texture_path);

  absl::StatusOr<std::vector<Texture>> GetAllTextures();

  absl::StatusOr<uint16_t> UpsertSprite(const SpriteConfig& sprite_config);

  absl::Status UpdateSprite(const SpriteConfig& sprite_config);

  absl::Status DeleteSprite(uint16_t sprite_id);

  absl::StatusOr<std::vector<SpriteConfig>> GetAllSprites();

  absl::StatusOr<std::vector<SpriteFrame>> GetSpriteFrames(uint16_t sprite_id);

  absl::StatusOr<uint16_t> InsertSpriteFrame(uint16_t sprite_id, const SpriteFrame& sprite_frame);

  absl::Status DeleteSpriteFrame(uint16_t sprite_frame_id);

  absl::Status UpdateSpriteFrame(const SpriteFrame& sprite_frame);

 private:
  const GameConfig* config_;
  DbInterface* db_;
};

}  // namespace zebes