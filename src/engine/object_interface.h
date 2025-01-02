#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "cereal/archives/binary.hpp"
#include "polygon.h"
#include "vector.h"

namespace zebes {

enum ObjectType : uint16_t {
  kObject = 0,
  kSprite = 1,
  kMobile = 2,
};

struct Collision {
  PolygonOverlap overlap;
  ObjectType object_type = kObject;
  bool hit_ground() const {
    return overlap.overlap && object_type == kSprite &&
           overlap.min_overlap_x() == 0 && overlap.min_overlap_y() > 0;
  }
};

struct ObjectOptions {
  // If object_id is zero, a new id will be generated.
  uint64_t object_id = 0;
  ObjectType object_type = kObject;
  std::vector<Point> vertices;
  std::vector<uint16_t> sprite_ids;

  template <class Archive>
  void serialize(Archive &archive) {
    archive(object_id, object_type, vertices, sprite_ids);
  }

  template <class Archive>
  void load(Archive &archive) {
    archive(object_id, object_type, vertices, sprite_ids);
  }
};

class ObjectInterface {
 public:
  virtual ~ObjectInterface() = default;
  // Return id of the object.
  virtual uint64_t object_id() const = 0;

  // Return type of the object.
  virtual ObjectType object_type() const = 0;

  // Return pointer to polygon.
  virtual const Polygon *polygon() const = 0;

  // Check if object can collide with another object.
  virtual bool IsInteractive(ObjectType type) const = 0;

  // Handle a collision.
  virtual absl::Status HandleCollision(Collision collision) = 0;

  // Clean up state right before the update pipeline, but only in
  // the case that a frame is advancing.
  virtual void PreUpdate() = 0;

  // Marks the object for destruction.
  virtual void Destroy() = 0;

  // Checks whether the object is marked for destruction.
  virtual bool IsDestroyed() = 0;

  // Add primary axis index. This axis will be marked as a primary axis
  // when caculating the overlap between two objects.
  virtual absl::Status AddPrimaryAxisIndex(
      uint8_t index,
      AxisDirection axis_direction = AxisDirection::axis_none) = 0;

  // Move from one position to another.
  virtual void Move(float x, float y) = 0;
};

}  // namespace zebes
