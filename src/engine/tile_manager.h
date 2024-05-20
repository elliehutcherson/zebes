#pragma once

#include "absl/status/status.h"

#include <memory>

#include "camera.h"
#include "collision_manager.h"
#include "config.h"
#include "object.h"
#include "sprite_manager.h"
#include "tile_matrix.h"

namespace zebes {

class TileManager {
public:
  // Constructor
  TileManager(const GameConfig *config, Camera *camera,
              const TileMatrix *tile_matrix);
  // Destructor
  ~TileManager() = default;
  // Initialize with the sprite manager.
  absl::Status Init(CollisionManager *collision_manager,
                    SpriteManager *sprite_manager);
  // Draw player.
  void Render();
  // Move player.
  void Move();

private:
  const GameConfig *config_;
  Camera *camera_;
  const TileMatrix *tile_matrix_;
  std::vector<std::unique_ptr<SpriteObject>> sprite_objects_ = {};

  SDL_Texture *texture_;
};

} // namespace zebes