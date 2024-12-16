#include "scene_manager.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "collision_manager.h"
#include "config.h"
#include "mobile_object.h"
#include "object_interface.h"
#include "polygon.h"
#include "shape.h"
#include "sprite_manager.h"
#include "sprite_object.h"
#include "status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SceneManager>> SceneManager::Create(
    const SceneManager::Options &options) {
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config is null.");
  if (options.collision_manager == nullptr)
    return absl::InvalidArgumentError("CollisionManager is null.");
  if (options.sprite_manager == nullptr)
    return absl::InvalidArgumentError("SpriteManager is null.");

  std::unique_ptr<SceneManager> scene_manager =
      std::unique_ptr<SceneManager>(new SceneManager(options));
  return scene_manager;
}

SceneManager::SceneManager(SceneManager::Options options)
    : config_(options.config),
      collision_manager_(options.collision_manager),
      sprite_manager_(options.sprite_manager) {}

uint16_t SceneManager::AddScene(int width, int height) {
  Scene scene = {
      .id = static_cast<uint16_t>(scenes_.size()),
      .width = width,
      .height = height,
  };
  active_scene_ = scene.id;
  scenes_.push_back(std::move(scene));
  return active_scene_;
}

absl::Status SceneManager::AddLayerToScene(uint16_t scene_id, int layer,
                                           std::string path) {
  ASSIGN_OR_RETURN(Bitmap bitmap, Bitmap::LoadFromBmp(path));
  return AddLayerToScene(scene_id, layer, bitmap);
}

absl::Status SceneManager::AddLayerToScene(uint16_t scene_id, int layer,
                                           const Bitmap &bitmap) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  Scene &scene = scenes_[scene_id];
  // SceneLayer *scene_layer = scenes_.back().layers[layer].get();

  for (int x = 0; x < bitmap.width(); ++x) {
    for (int y = 0; y < bitmap.height(); ++y) {
      Shape::State state;

      RETURN_IF_ERROR(bitmap.Get(x, y, &state.raw_eight[0], &state.raw_eight[1],
                                 &state.raw_eight[2], &state.raw_eight[3]));

      if (state.sprite_id == 0) continue;

      Shape shape(state);
    }
  }
  return absl::OkStatus();
}

absl::Status SceneManager::AddObjectToLayer(
    uint16_t scene_id, int layer_id, std::unique_ptr<ObjectInterface> object) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");
  Scene &scene = scenes_[scene_id];

  auto it = scene.layers.find(layer_id);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");
  SceneLayer *scene_layer = it->second.get();

  scene_layer->objects[object->object_id()] = std::move(object);
  return absl::OkStatus();
}

absl::Status SceneManager::RemoveScene(uint16_t scene_id) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  if (scene_id == active_scene_) active_scene_ = -1;

  scenes_.erase(scenes_.begin() + scene_id);
  return absl::OkStatus();
}

absl::Status SceneManager::RemoveLayerFromScene(uint16_t scene_id, int layer) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");
  Scene &scene = scenes_[scene_id];

  auto it = scene.layers.find(layer);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");

  scene.layers.erase(it);
  return absl::OkStatus();
}

absl::Status SceneManager::RemoveObjectFromLayer(uint16_t scene_id, int layer,
                                                 uint64_t object_id) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  Scene &scene = scenes_[scene_id];
  auto it = scene.layers.find(layer);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");

  SceneLayer *scene_layer = it->second.get();
  auto object_it = scene_layer->objects.find(object_id);
  if (object_it == scene_layer->objects.end())
    return absl::InvalidArgumentError("Tile not found.");

  object_it->second->Destroy();
  return absl::OkStatus();
}

void SceneManager::Update() {
  Scene &scene = scenes_[active_scene_];
  for (const auto &it : scene.layers) {
    for (auto &object : it.second->objects) {
      if (object.second->object_type() == ObjectType::kObject) continue;

      SpriteObject *sprite_object =
          dynamic_cast<SpriteObject *>(object.second.get());
      sprite_object->Update();
    }
  }
}

void SceneManager::Render() {
  Scene &scene = scenes_[active_scene_];
  for (auto &it : scene.layers) {
    RenderLayer(*it.second);
  }
}

void SceneManager::RenderLayer(SceneLayer &layer) {
  for (auto &it : layer.objects) {
    if (it.second->object_type() == ObjectType::kObject) continue;

    SpriteObject *sprite_object = dynamic_cast<SpriteObject *>(it.second.get());
    uint16_t sprite_id = sprite_object->GetActiveSprite()->GetId();
    int sprite_index = sprite_object->GetActiveSpriteIndex();

    Point point = {
        .x = sprite_object->polygon()->x_min(),
        .y = sprite_object->polygon()->y_min(),
    };

    absl::Status result =
        sprite_manager_->Render(sprite_id, sprite_index, point);
    if (!result.ok()) {
      LOG(WARNING) << "Failed to render texture.";
      sprite_object->Destroy();
    }
  }

  auto cb = [](auto &it) -> bool {
    ObjectInterface *object = it.second.get();
    return object->IsDestroyed();
  };

  absl::erase_if(layer.objects, cb);
}

}  // namespace zebes
