#pragma once

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "engine/bitmap.h"
#include "engine/collision_manager.h"
#include "engine/config.h"
#include "engine/object.h"
#include "engine/shape.h"
#include "engine/sprite_manager.h"
#include "engine/texture_manager.h"
#include "engine/vector.h"

namespace zebes {

using SceneTiles =
    absl::flat_hash_map<Point, std::unique_ptr<SpriteObject>, PointHash>;

struct SceneLayer {
  SceneTiles tiles;
};

struct Scene {
  uint16_t id = 0;
  int width = 100000;
  int height = 100000;
  absl::flat_hash_map<int, SceneLayer> layers;
};

class TileManager {
 public:
  struct Options {
    const GameConfig *config;
    TextureManager *texture_manager;
    SpriteManager *sprite_manager;
    CollisionManager *collision_manager;
  };

  static absl::StatusOr<std::unique_ptr<TileManager>> Create(
      const Options &options);

  ~TileManager() = default;

  uint16_t AddScene(int width, int height);

  absl::Status AddLayerToScene(uint16_t scene_id, int layer, std::string path);

  absl::Status AddLayerToScene(uint16_t scene_id, int layer,
                               const Bitmap &bitmap);

  absl::Status AddTileToLayer(uint16_t scene_id, int layer, const Shape &shape);

  absl::Status RemoveScene(uint16_t scene_id);

  absl::Status RemoveLayerFromScene(uint16_t scene_id, int layer);

  absl::Status RemoveTileFromLayer(uint16_t scene_id, int layer,
                                   const Point &position);

  void Update();

  void Render();

  void Clean();

 private:
  TileManager(Options options);

  void RenderLayer(SceneLayer &layer);

  int active_scene_ = 0;
  std::vector<Scene> scenes_;

  const GameConfig *config_;
  CollisionManager *collision_manager_;
  SpriteManager *sprite_manager_;
  TextureManager *texture_manager_;
};

}  // namespace zebes
