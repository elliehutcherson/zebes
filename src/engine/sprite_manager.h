#pragma once 

#include <_types/_uint16_t.h>

#include "SDL_render.h"

#include "absl/status/statusor.h"

#include "camera.h"
#include "config.h"
#include "object.h"
#include "sprite.h"

namespace zebes {

class SpriteManager {
 public:
  SpriteManager(const GameConfig* config, SDL_Renderer* renderer, Camera* camera);
  ~SpriteManager() = default;
  // Initialize all sprites.
  absl::Status Init();
  // Initialize sprite, and potentially a new texture if this
  // texture has not been initialized before.
  absl::Status InitializeSprite(SpriteConfig config);
  // Get a sprite by type. Returns not found if sprite type does not exist.
  absl::StatusOr<const Sprite*> GetSprite(SpriteType type) const;
  // Add any sprite object through this method. All sprite objects will be rendered
  // at the render step.
  absl::Status AddSpriteObject(const SpriteObjectInterface* object);
  // Render all objects registered.
  void Render();

 private:
  // Retreive SubSpriteConfig records from database. 
  absl::StatusOr<std::vector<SubSprite>> GetSubSprites(SpriteType type);
  // The game config.
  const GameConfig* config_;
  SDL_Renderer* renderer_;
  Camera* camera_;
  absl::flat_hash_map<std::string, SDL_Texture*> path_to_texture_  = {};
  absl::flat_hash_map<SpriteType, Sprite> sprites_ = {};
  std::vector<const SpriteObjectInterface*> sprite_objects_;
};

}  // namespace zebes