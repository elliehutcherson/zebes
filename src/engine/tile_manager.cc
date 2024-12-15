#include "tile_manager.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "engine/collision_manager.h"
#include "engine/config.h"
#include "engine/object.h"
#include "engine/polygon.h"
#include "engine/shape.h"
#include "engine/texture_manager.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<TileManager>> TileManager::Create(
    const TileManager::Options &options) {
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config is null.");
  if (options.collision_manager == nullptr)
    return absl::InvalidArgumentError("CollisionManager is null.");
  if (options.texture_manager == nullptr)
    return absl::InvalidArgumentError("TextureManager is null.");
  if (options.sprite_manager == nullptr)
    return absl::InvalidArgumentError("SpriteManager is null.");

  std::unique_ptr<TileManager> tile_manager =
      std::unique_ptr<TileManager>(new TileManager(options));
  return tile_manager;
}

TileManager::TileManager(TileManager::Options options)
    : config_(options.config),
      collision_manager_(options.collision_manager),
      sprite_manager_(options.sprite_manager),
      texture_manager_(options.texture_manager) {}

uint16_t TileManager::AddScene(int width, int height) {
  Scene scene = {
      .id = static_cast<uint16_t>(scenes_.size()),
      .width = width,
      .height = height,
  };
  active_scene_ = scene.id;
  scenes_.push_back(std::move(scene));
  return active_scene_;
}

absl::Status TileManager::AddLayerToScene(uint16_t scene_id, int layer,
                                          std::string path) {
  absl::StatusOr<Bitmap> bitmap_result = Bitmap::LoadFromBmp(path);
  if (!bitmap_result.ok()) return bitmap_result.status();
  return AddLayerToScene(scene_id, layer, *bitmap_result);
}

absl::Status TileManager::AddLayerToScene(uint16_t scene_id, int layer,
                                          const Bitmap &bitmap) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  Scene &scene = scenes_[scene_id];
  SceneLayer &scene_layer = scenes_.back().layers[layer];

  for (int x = 0; x < bitmap.width(); ++x) {
    for (int y = 0; y < bitmap.height(); ++y) {
      Shape::State state;

      absl::Status result =
          bitmap.Get(x, y, &state.raw_eight[0], &state.raw_eight[1],
                     &state.raw_eight[2], &state.raw_eight[3]);
      if (!result.ok()) return result;

      if (state.sprite_id == 0) continue;

      Shape shape(state);
    }
  }
  return absl::OkStatus();
}

absl::Status TileManager::AddTileToLayer(uint16_t scene_id, int layer_id,
                                         const Shape &shape) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");
  Scene &scene = scenes_[scene_id];

  auto it = scene.layers.find(layer_id);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");
  SceneLayer &scene_layer = it->second;

  ObjectOptions options = {.object_type = ObjectType::kTile,
                           .vertices = *shape.polygon()->vertices()};

  absl::StatusOr<std::unique_ptr<SpriteObject>> sprite_result =
      SpriteObject::Create(options);
  if (!sprite_result.ok()) return sprite_result.status();

  std::unique_ptr<SpriteObject> &sprite_object = *sprite_result;
  absl::Status result = collision_manager_->AddObject(sprite_object.get());
  if (!result.ok()) return result;

  absl::StatusOr<const SpriteInterface *> sprite =
      sprite_manager_->GetSprite(shape.state().sprite_id);
  if (!sprite.ok()) return sprite.status();

  result = sprite_object->AddSprite(*sprite);

  if (shape.state().primary0)
    result = sprite_object->AddPrimaryAxisIndex(0, AxisDirection::axis_left);
  if (result.ok() && shape.state().primary1)
    result = sprite_object->AddPrimaryAxisIndex(1, AxisDirection::axis_left);
  if (result.ok() && shape.state().primary2)
    result = sprite_object->AddPrimaryAxisIndex(2, AxisDirection::axis_left);
  if (result.ok() && shape.state().primary3)
    result = sprite_object->AddPrimaryAxisIndex(3, AxisDirection::axis_left);
  if (!result.ok()) return result;

  scene_layer.tiles[shape.position()] = std::move(sprite_object);
  return absl::OkStatus();
}

absl::Status TileManager::RemoveScene(uint16_t scene_id) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  if (scene_id == active_scene_) active_scene_ = -1;

  scenes_.erase(scenes_.begin() + scene_id);
  return absl::OkStatus();
}

absl::Status TileManager::RemoveLayerFromScene(uint16_t scene_id, int layer) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");
  Scene &scene = scenes_[scene_id];

  auto it = scene.layers.find(layer);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");

  scene.layers.erase(it);
  return absl::OkStatus();
}

absl::Status TileManager::RemoveTileFromLayer(uint16_t scene_id, int layer,
                                              const Point &position) {
  if (scene_id >= scenes_.size())
    return absl::InvalidArgumentError("Scene ID out of bounds.");

  Scene &scene = scenes_[scene_id];
  auto it = scene.layers.find(layer);
  if (it == scene.layers.end())
    return absl::InvalidArgumentError("Layer ID out of bounds.");

  SceneLayer &scene_layer = it->second;
  auto tile_it = scene_layer.tiles.find(position);
  if (tile_it == scene_layer.tiles.end())
    return absl::InvalidArgumentError("Tile not found.");

  tile_it->second->Destroy();
  return absl::OkStatus();
}

void TileManager::Update() {
  Scene &scene = scenes_[active_scene_];
  for (const auto &it : scene.layers) {
    for (auto &sprite_object : it.second.tiles) {
      sprite_object.second->Update();
    }
  }
}

void TileManager::Render() {
  Scene &scene = scenes_[active_scene_];
  for (auto &it : scene.layers) {
    RenderLayer(it.second);
  }
}

void TileManager::RenderLayer(SceneLayer &layer) {
  for (auto &it : layer.tiles) {
    const Point &position = it.first;
    std::unique_ptr<SpriteObject> &sprite_object = it.second;

    uint16_t sprite_id = sprite_object->GetActiveSprite()->GetId();
    int sprite_index = sprite_object->GetActiveSpriteIndex();
    Point point = {
        .x = sprite_object->polygon()->x_min(),
        .y = sprite_object->polygon()->y_min(),
    };

    absl::Status result =
        texture_manager_->Render(sprite_id, sprite_index, point);
    if (!result.ok()) {
      LOG(WARNING) << "Failed to render texture.";
      sprite_object->Destroy();
    }
  }

  auto cb = [](auto &it) -> bool {
    std::unique_ptr<SpriteObject> &sprite_object = it.second;
    return sprite_object->IsDestroyed();
  };

  absl::erase_if(layer.tiles, cb);
}

}  // namespace zebes
