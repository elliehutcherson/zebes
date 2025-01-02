#include "object.h"

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "camera.h"
#include "object_interface.h"
#include "polygon.h"

namespace zebes {
namespace {

absl::flat_hash_set<ObjectType> GetCompatibleCollisions(ObjectType type) {
  switch (type) {
    case ObjectType::kMobile:
      return {ObjectType::kSprite};
    case ObjectType::kSprite:
      return {ObjectType::kMobile};
    default:
      return {};
  }
}

inline uint64_t GetObjectId() {
  static uint64_t id = 0;
  return ++id;
}

}  // namespace

absl::StatusOr<std::unique_ptr<Object>> Object::Create(ObjectOptions &options) {
  if (options.vertices.empty())
    return absl::InvalidArgumentError("Vertices must not be empty.");
  return std::unique_ptr<Object>(new Object(options));
}

Object::Object(ObjectOptions &options)
    : type_(options.object_type), polygon_(options.vertices) {
  id_ = GetObjectId();
  compatible_collisions_ = GetCompatibleCollisions(type_);
}

absl::Status Object::AddPrimaryAxisIndex(uint8_t index,
                                         AxisDirection axis_direction) {
  return polygon_.AddPrimaryAxisIndex(index, axis_direction);
}

void Object::Move(float x, float y) { polygon_.Move(x, y); }

absl::Status Object::HandleCollision(Collision collision) {
  if (collision.overlap.overlap) collision_ = true;
  LOG(INFO) << "Object " << object_id() << " collided with object "
            << collision.object_type;
  return absl::OkStatus();
}

void Object::PreUpdate() { collision_ = false; }

absl::Status Object::Render(Camera *camera) const {
  DrawColor color =
      collision_ ? DrawColor::kColorCollide : DrawColor::kColorTile;
  return camera->RenderLines(*polygon_.vertices(), color,
                             /*static_position=*/false);
}

}  // namespace zebes
