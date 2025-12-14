#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "common/config.h"
#include "objects/texture.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"

namespace zebes {

class Api {
 public:
  struct Options {
    const GameConfig* config;
    TextureManager* texture_manager;
    SpriteManager* sprite_manager;
  };

  static absl::StatusOr<std::unique_ptr<Api>> Create(const Options& options);

  Api(const Options& options);
  ~Api() = default;

  // Get reading access to the config
  const GameConfig* GetConfig() const { return &config_; }

  // Save the config to disk
  absl::Status SaveConfig(const GameConfig& config);

  absl::StatusOr<std::string> CreateTexture(Texture texture);
  absl::Status DeleteTexture(const std::string& texture_id);
  absl::StatusOr<std::vector<Texture>> GetAllTextures();
  absl::Status UpdateTexture(const Texture& texture);
  absl::StatusOr<Texture*> GetTexture(const std::string& sprite_id);

  absl::StatusOr<std::string> CreateSprite(Sprite sprite);
  absl::Status UpdateSprite(Sprite sprite);
  absl::Status DeleteSprite(const std::string& sprite_id);
  std::vector<Sprite> GetAllSprites();
  absl::StatusOr<Sprite*> GetSprite(const std::string& sprite_id);

 private:
  const GameConfig& config_;
  TextureManager* texture_manager_;
  SpriteManager* sprite_manager_;
};

}  // namespace zebes