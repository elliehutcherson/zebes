#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "common/config.h"
#include "db/db_interface.h"
#include "engine/sprite_manager.h"
#include "objects/sprite_interface.h"

namespace zebes {

class Api {
 public:
  struct Options {
    const GameConfig* config;
    DbInterface* db;
    SpriteManager* sprite_manager;
  };

  static absl::StatusOr<std::unique_ptr<Api>> Create(const Options& options);

  Api(const Options& options);
  ~Api() = default;
  
  absl::StatusOr<std::string> CreateTexture(const std::string& texture_path);

  absl::Status DeleteTexture(const std::string& texture_path);

  absl::StatusOr<uint16_t> UpsertSprite(const SpriteConfig& sprite_config);
  
  absl::Status DeleteSprite(uint16_t sprite_id);

 private:
  const GameConfig* config_;
  DbInterface* db_;
  SpriteManager* sprite_manager_;
};

}  // namespace zebes