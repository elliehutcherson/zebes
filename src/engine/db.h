#pragma once

#include <cstdint>

#include "db_interface.h"
#include "sprite_interface.h"
#include "sqlite3.h"

namespace zebes {

class Db : public DbInterface {
 public:
  struct Options {
    const std::string db_path;
  };
  static absl::StatusOr<std::unique_ptr<Db>> Create(const Options& options);

  // Sprite interfaces.
  absl::StatusOr<uint16_t> InsertSprite(
      const SpriteConfig& sprite_config) override;

  absl::Status DeleteSprite(uint16_t sprite_id) override;

  absl::StatusOr<SpriteConfig> GetSprite(uint16_t sprite_id) override;

  absl::StatusOr<std::vector<SpriteConfig>> GetAllSprites() override;

  absl::StatusOr<std::vector<SubSprite>> GetSubSprites(
      uint16_t sprite_id) override;

 private:
  Db(const Options& options);

  absl::StatusOr<sqlite3*> OpenDb();

  const std::string db_path_;
};

}  // namespace zebes