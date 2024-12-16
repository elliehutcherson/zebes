#pragma once

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "engine/bitmap.h"
#include "engine/collision_manager.h"
#include "engine/config.h"
#include "engine/mobile_object.h"
#include "engine/object.h"
#include "engine/shape.h"
#include "engine/sprite_manager.h"
#include "engine/sprite_object.h"
#include "engine/vector.h"

namespace zebes {

struct SceneLayer {
  int layer_id = 0;
  absl::flat_hash_map<int, std::unique_ptr<ObjectInterface>> objects;
};

struct Scene {
  uint16_t id = 0;
  int width = 100000;
  int height = 100000;
  absl::flat_hash_map<int, std::unique_ptr<SceneLayer>> layers;
};

class SceneManager {
 public:
  struct Options {
    const GameConfig *config;
    SpriteManager *sprite_manager;
    CollisionManager *collision_manager;
  };

  static absl::StatusOr<std::unique_ptr<SceneManager>> Create(
      const Options &options);

  ~SceneManager() = default;

  uint16_t AddScene(int width, int height);

  absl::Status AddLayerToScene(uint16_t scene_id, int layer, std::string path);

  absl::Status AddLayerToScene(uint16_t scene_id, int layer,
                               const Bitmap &bitmap);

  absl::Status AddObjectToLayer(uint16_t scene_id, int layer_id,
                                std::unique_ptr<ObjectInterface> object);

  absl::Status RemoveScene(uint16_t scene_id);

  absl::Status RemoveLayerFromScene(uint16_t scene_id, int layer);

  absl::Status RemoveObjectFromLayer(uint16_t scene_id, int layer,
                                     uint64_t object_id);

  void Update();

  void Render();

  void Clean();

 private:
  SceneManager(Options options);

  void RenderLayer(SceneLayer &layer);

  int active_scene_ = 0;
  std::vector<Scene> scenes_;

  const GameConfig *config_;
  CollisionManager *collision_manager_;
  SpriteManager *sprite_manager_;
};

}  // namespace zebes
