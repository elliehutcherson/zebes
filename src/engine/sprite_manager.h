#pragma once

#include <cstdint>

#include "SDL_render.h"
#include "absl/status/statusor.h"
#include "camera.h"
#include "config.h"
#include "db_interface.h"
#include "object.h"
#include "sprite.h"
#include "sprite_interface.h"
#include "sprite_object.h"

namespace zebes {

class SpriteManager {
 public:
  struct Options {
    const GameConfig *config;
    SDL_Renderer *renderer;
    Camera *camera;
    DbInterface *db;
  };

  static absl::StatusOr<std::unique_ptr<SpriteManager>> Create(
      const Options &options);

  ~SpriteManager() = default;

  // Initialize all sprites.
  absl::Status Init();

  // Initialize sprite, and potentially a new texture if this
  // texture has not been initialized before.
  absl::Status InitializeSprite(SpriteConfig config);

  // Get all textures by their paths.
  const absl::flat_hash_map<std::string, SDL_Texture *> *GetAllTextures() const;

  // Get all sprite_ids.
  std::vector<uint16_t> GetAllSpriteIds() const;

  // Get a sprite by type. Returns not found if sprite type does not exist.
  absl::StatusOr<const Sprite *> GetSprite(uint16_t sprite_id) const;

  // Get a sprite by type. Returns not found if sprite type does not exist.
  absl::StatusOr<const Sprite *> GetSpriteByType(uint16_t sprite_type) const;

  // Add any sprite object through this method. All sprite objects will be
  // rendered at the render step.
  absl::Status AddSpriteObject(SpriteObjectInterface *object);

  // Add texture to the path_to_texture_ map.
  absl::Status AddTexture(std::string path);

  // Render all sprite's subsprite at index and position.
  absl::Status Render(uint16_t sprite_id, int index, Point position);

 private:
  SpriteManager(const Options &options);

  // Retreive SubSpriteConfig records from database.
  absl::StatusOr<std::vector<SubSprite>> GetSubSprites(uint16_t sprite_id);

  // Stateful objects.
  const GameConfig *config_;
  SDL_Renderer *renderer_;
  Camera *camera_;
  DbInterface *db_;

  absl::flat_hash_map<std::string, SDL_Texture *> path_to_texture_;
  absl::flat_hash_map<uint16_t, std::unique_ptr<Sprite>> sprites_;
  absl::flat_hash_map<uint16_t, Sprite *> type_to_sprites_;
  std::vector<SpriteObjectInterface *> sprite_objects_;

  // Experiment method to render a texture in HUD.
  std::vector<SDL_Texture *> experiment_textures_;
};

}  // namespace zebes