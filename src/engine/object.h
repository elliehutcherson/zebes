#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "camera.h"
#include "object_interface.h"
#include "polygon.h"
#include "sprite_interface.h"

namespace zebes {

class Object : virtual public ObjectInterface {
 public:
  static absl::StatusOr<std::unique_ptr<Object>> Create(ObjectOptions &options);

  ~Object() = default;

  uint64_t object_id() const override { return id_; }

  ObjectType object_type() const override { return type_; }

  const Polygon *polygon() const override { return &polygon_; }

  bool IsInteractive(ObjectType type) const override {
    return compatible_collisions_.contains(type);
  }

  void Destroy() override { is_destroyed_ = true; }

  bool IsDestroyed() override { return is_destroyed_; }

  absl::Status HandleCollision(Collision collision) override;

  absl::Status AddPrimaryAxisIndex(
      uint8_t index,
      AxisDirection axis_direction = AxisDirection::axis_none) override;

  void Move(float x, float y) override;

  void PreUpdate() override;

  // Convert points to SDL objects, and render to window.
  absl::Status Render(Camera *camera) const;

 protected:
  Object(ObjectOptions &options);

  uint64_t id_ = 0;
  ObjectType type_ = ObjectType::kObject;
  bool is_destroyed_ = false;
  bool collision_ = false;
  Polygon polygon_;
  absl::flat_hash_set<ObjectType> compatible_collisions_;
};

}  // namespace zebes
