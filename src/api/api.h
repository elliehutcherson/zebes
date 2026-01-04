#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "common/config.h"
#include "objects/texture.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"

namespace zebes {

class Api {
 public:
  struct Options {
    const GameConfig* config;
    TextureManager* texture_manager;
    SpriteManager* sprite_manager;
    ColliderManager* collider_manager;
    BlueprintManager* blueprint_manager;
    LevelManager* level_manager;
  };

  static absl::StatusOr<std::unique_ptr<Api>> Create(const Options& options);

  Api(const Options& options);
  virtual ~Api() = default;

  // Get reading access to the config
  const GameConfig* GetConfig() const { return &config_; }

  // Save the config to disk
  virtual absl::Status SaveConfig(const GameConfig& config);

  virtual absl::StatusOr<std::string> CreateTexture(Texture texture);
  virtual absl::Status DeleteTexture(const std::string& texture_id);
  virtual absl::StatusOr<std::vector<Texture>> GetAllTextures();
  virtual absl::Status UpdateTexture(const Texture& texture);
  virtual absl::StatusOr<Texture*> GetTexture(const std::string& sprite_id);

  virtual absl::StatusOr<std::string> CreateSprite(Sprite sprite);
  virtual absl::Status UpdateSprite(Sprite sprite);
  virtual absl::Status DeleteSprite(const std::string& sprite_id);
  virtual std::vector<Sprite> GetAllSprites();
  virtual absl::StatusOr<Sprite*> GetSprite(const std::string& sprite_id);

  virtual absl::StatusOr<std::string> CreateCollider(Collider collider);
  virtual absl::Status UpdateCollider(Collider collider);
  virtual absl::Status DeleteCollider(const std::string& collider_id);
  virtual std::vector<Collider> GetAllColliders();
  virtual absl::StatusOr<Collider*> GetCollider(const std::string& collider_id);

  virtual absl::StatusOr<std::string> CreateBlueprint(Blueprint blueprint);
  virtual absl::Status UpdateBlueprint(Blueprint blueprint);
  virtual absl::Status DeleteBlueprint(const std::string& blueprint_id);
  virtual std::vector<Blueprint> GetAllBlueprints();
  virtual absl::StatusOr<Blueprint*> GetBlueprint(const std::string& blueprint_id);

  virtual absl::StatusOr<std::string> CreateLevel(Level level);
  virtual absl::Status UpdateLevel(Level level);
  virtual absl::Status DeleteLevel(const std::string& level_id);
  virtual std::vector<Level> GetAllLevels();
  virtual absl::StatusOr<Level*> GetLevel(const std::string& level_id);

 protected:
  // Allow default construction for mocks
  Api()
      : config_(*(new GameConfig())),
        texture_manager_(nullptr),
        sprite_manager_(nullptr),
        collider_manager_(nullptr),
        blueprint_manager_(nullptr),
        level_manager_(nullptr) {}

 private:
  const GameConfig& config_;
  TextureManager* texture_manager_;
  SpriteManager* sprite_manager_;
  ColliderManager* collider_manager_;
  BlueprintManager* blueprint_manager_;
  LevelManager* level_manager_;
};

}  // namespace zebes