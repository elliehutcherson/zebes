#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "db/db_interface.h"
#include "objects/sprite_interface.h"
#include "sqlite3.h"

namespace zebes {

class Db : public DbInterface {
 public:
  struct Options {
    const std::string db_path;
  };
  static absl::StatusOr<std::unique_ptr<Db>> Create(const Options& options);


  absl::StatusOr<uint16_t> InsertTexture(const std::string& path) override;

  absl::Status DeleteTexture(const std::string& path) override;

  absl::StatusOr<bool> TextureExists(const std::string &path) override;

  absl::StatusOr<std::vector<std::string>> GetAllTexturePaths() override;

  absl::StatusOr<int> NumSpritesWithTexture(const std::string& path) override;

  absl::StatusOr<uint16_t> InsertSprite(
      const SpriteConfig& sprite_config) override;

  absl::Status DeleteSprite(uint16_t sprite_id) override;

  absl::StatusOr<SpriteConfig> GetSprite(uint16_t sprite_id) override;

  absl::StatusOr<std::vector<SpriteConfig>> GetAllSprites() override;

  absl::StatusOr<uint16_t> InsertSubSprite(
      uint16_t sprite_id, const SubSprite& sub_sprite) override;

  absl::Status DeleteSubSprite(uint16_t sub_sprite_id) override;

  absl::StatusOr<SubSprite> GetSubSprite(uint16_t sub_sprite_id) override;

  absl::StatusOr<std::vector<SubSprite>> GetSubSprites(
      uint16_t sprite_id) override;

 private:
  Db(const Options& options);

  absl::StatusOr<sqlite3*> OpenDb();

  const std::string db_path_;
};

}  // namespace zebes