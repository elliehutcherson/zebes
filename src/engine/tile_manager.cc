#include "tile_manager.h"

#include <memory>
#include <set>
#include <vector>

#include "absl/status/status.h"

#include "engine/collision_manager.h"
#include "engine/config.h"
#include "engine/object.h"
#include "engine/polygon.h"
#include "engine/sprite_manager.h"
#include "engine/tile_matrix.h"

namespace zebes {
namespace {

std::vector<Point> GetRenderVertices(SpriteType type, int x, int y) {
  std::vector<Point> vertices;
  switch (type) {
  case SpriteType::kDirt1:
  case SpriteType::kGrass1:
  case SpriteType::kGrass2:
  case SpriteType::kGrass3:
  case SpriteType::kGrass1Down:
  case SpriteType::kGrass1Left:
  case SpriteType::kGrass1Right:
  case SpriteType::kGrassCornerDownLeft:
  case SpriteType::kGrassCornerDownRight:
  case SpriteType::kGrassCornerUpLeft:
  case SpriteType::kGrassCornerUpRight:
    vertices = {
        {.x = 0, .y = 0},
        {.x = 32, .y = 0},
        {.x = 32, .y = 32},
        {.x = 0, .y = 32},
    };
    break;
  case SpriteType::kGrassSlopeUpRight:
    vertices = {
        {.x = 0, .y = 32},
        {.x = 64, .y = 0},
        {.x = 64, .y = 64},
        {.x = 0, .y = 64},
    };
    break;
  default:
    break;
  }
  for (Point &vertex : vertices) {
    vertex.x += (x * 32);
    vertex.y += (y * 32);
  }
  return vertices;
}

std::vector<uint8_t> GetPrimaryAxes(SpriteType type) {
  std::vector<uint8_t> axes;
  switch (type) {
  case SpriteType::kGrass1:
  case SpriteType::kGrass2:
  case SpriteType::kGrass3:
    axes = {0};
    break;
  case SpriteType::kGrass1Right:
    axes = {1};
    break;
  case SpriteType::kGrass1Down:
    axes = {2};
    break;
  case SpriteType::kGrass1Left:
    axes = {3};
    break;
  case SpriteType::kGrassCornerUpRight:
    axes = {0, 1};
    break;
  case SpriteType::kGrassCornerDownRight:
    axes = {1, 2};
    break;
  case SpriteType::kGrassCornerDownLeft:
    axes = {2, 3};
    break;
  case SpriteType::kGrassCornerUpLeft:
    axes = {3, 0};
    break;
  default:
    break;
  }
  return axes;
}

} // namespace

TileManager::TileManager(const GameConfig *config, Camera *camera,
                         const TileMatrix *tile_matrix)
    : config_(config), camera_(camera), tile_matrix_(tile_matrix) {}

absl::Status TileManager::Init(CollisionManager *collision_manager,
                               SpriteManager *sprite_manager) {
  std::set<Point> dont_render = {};
  absl::Status result = absl::OkStatus();
  for (int x = 0; x < tile_matrix_->x_max(); ++x) {
    for (int y = 0; y < tile_matrix_->y_max(); ++y) {
      Point coordinates = {.x = (double)x, .y = (double)y};
      SpriteType type = static_cast<SpriteType>(tile_matrix_->Get(x, y));
      if (type == SpriteType::kEmpty)
        continue;
      if (type == SpriteType::kGrassSlopeUpRight) {
        if (dont_render.contains(coordinates))
          continue;
        dont_render.insert({x + 1.0, y + 0.0});
        dont_render.insert({x + 0.0, y + 1.0});
        dont_render.insert({x + 1.0, y + 1.0});
      }

      ObjectOptions options{
          .config = config_,
          .object_type = ObjectType::kTile,
          .vertices = GetRenderVertices(type, x, y),
      };

      absl::StatusOr<std::unique_ptr<SpriteObject>> sprite_result =
          SpriteObject::Create(options);
      if (!sprite_result.ok())
        return sprite_result.status();

      std::unique_ptr<SpriteObject>& sprite_object = *sprite_result;
      result = collision_manager->AddObject(sprite_object.get());
      if (!result.ok())
        return result;

      result = sprite_object->AddSpriteProfile({.type = type});
      if (!result.ok())
        return result;

      result = sprite_manager->AddSpriteObject(sprite_object.get());
      if (!result.ok())
        return result;

      for (const uint8_t axis : GetPrimaryAxes(type)) {
        result =
            sprite_object->AddPrimaryAxisIndex(axis, AxisDirection::axis_left);
        if (!result.ok())
          return result;
      }

      sprite_objects_.push_back(std::move(sprite_object));
    }
  }
  return absl::OkStatus();
}

} // namespace zebes