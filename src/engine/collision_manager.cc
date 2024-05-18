#include "collision_manager.h"

#include <iostream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "absl/status/status.h"
#include "config.h"
#include "object.h"
#include "polygon.h"

namespace zebes {
namespace {

int CollisionHash(int id_a, int id_b) {
  return ((id_a + id_b) * (id_a + id_b + 1) / 2) + id_a;
}

} // namespace

CollisionManager::CollisionManager(const GameConfig *config)
    : config_(config) {}

absl::Status CollisionManager::Init() {
  if (config_->collisions.area_width <= 0 ||
      config_->collisions.area_height <= 0)
    return absl::FailedPreconditionError("Invalid config.");
  for (int x = 0; x < NumberAreasX(); ++x) {
    for (int y = 0; y < NumberAreasY(); ++y) {
      CollisionArea area{
          .id = GetCollisionAreaId(x, y),
          .x_min =
              config_->boundaries.x_min + (x * config_->collisions.area_width),
          .x_max = area.x_min + config_->collisions.area_width,
          .y_min =
              config_->boundaries.y_min + (y * config_->collisions.area_height),
          .y_max = area.y_min + config_->collisions.area_height,
      };
      // std::cout << area.DebugString() << std::endl;
      areas_[area.id] = area;
    }
  }
  return absl::OkStatus();
}

int CollisionManager::NumberAreasX() const {
  static int world_width =
      config_->boundaries.x_max - config_->boundaries.x_min;
  static int num_areas_x = world_width / config_->collisions.area_width;
  return num_areas_x;
}

int CollisionManager::NumberAreasY() const {
  static int world_height =
      config_->boundaries.y_max - config_->boundaries.y_min;
  static int num_areas_y = world_height / config_->collisions.area_height;
  return num_areas_y;
}

int CollisionManager::AreaIndexX(float x) const {
  return x / config_->collisions.area_width;
}

int CollisionManager::AreaIndexY(float y) const {
  return y / config_->collisions.area_height;
}

int CollisionManager::GetCollisionAreaId(int x, int y) const {
  return x * NumberAreasY() + y;
}

absl::flat_hash_set<int>
CollisionManager::GetCollisionAreaIds(const Polygon *polygon) const {
  int x_area_min = AreaIndexX(polygon->x_min());
  int x_area_max = AreaIndexX(polygon->x_max());
  int y_area_min = AreaIndexY(polygon->y_min());
  int y_area_max = AreaIndexY(polygon->y_max());
  absl::flat_hash_set<int> area_ids;
  for (int x_area_index = x_area_min; x_area_index <= x_area_max;
       ++x_area_index) {
    for (int y_area_index = y_area_min; y_area_index <= y_area_max;
         ++y_area_index) {
      area_ids.insert(GetCollisionAreaId(x_area_index, y_area_index));
    }
  }
  return area_ids;
}

absl::Status CollisionManager::AddObject(ObjectInterface *object) {
  int x_area_min = AreaIndexX(object->polygon()->x_min());
  int x_area_max = AreaIndexX(object->polygon()->x_max());
  int y_area_min = AreaIndexY(object->polygon()->y_min());
  int y_area_max = AreaIndexY(object->polygon()->y_max());
  absl::flat_hash_set<int> &area_ids =
      object_id_to_area_ids_[object->object_id()];
  for (int x_area_index = x_area_min; x_area_index <= x_area_max;
       ++x_area_index) {
    for (int y_area_index = y_area_min; y_area_index <= y_area_max;
         ++y_area_index) {
      int area_id = GetCollisionAreaId(x_area_index, y_area_index);
      area_ids.insert(area_id);
      absl::flat_hash_set<uint64_t> &object_ids =
          area_id_to_object_ids_[area_id];
      object_ids.insert(object->object_id());
    }
  }
  objects_[object->object_id()] = object;
  return absl::OkStatus();
}

void CollisionManager::Update() {
  // Clear hashes before looking for new collisions.
  hashes_.clear();
  for (auto &[object_id, object_a] : objects_) {
    // If this object is not the type of object causes collisions, mostly
    // meaning objects that can move, then continue.
    if (!collsion_types_.contains(object_a->object_type()))
      continue;
    // Get all area ids object_a is within.
    absl::flat_hash_set<int> area_ids =
        GetCollisionAreaIds(object_a->polygon());
    // For all areas, get the objects within them.
    for (int area_id : area_ids) {
      UpdateArea(area_id, *object_a);
    }
  }
}

void CollisionManager::UpdateArea(int area_id, ObjectInterface &object_a) {
  for (uint64_t object_id : area_id_to_object_ids_[area_id]) {
    // We don't want to collide objects with themselves.
    if (object_a.object_id() == object_id)
      continue;
    // Get potential object to collide against.
    ObjectInterface &object_b = *objects_[object_id];
    // Check if this collision has already ocurred.
    int hash = CollisionHash(object_a.object_id(), object_b.object_id());
    if (hashes_.contains(hash))
      continue;
    // If object_a and object_b are not interactive, then continue.
    if (!object_a.IsInteractive(object_b.object_type()))
      continue;
    // Check if there was a collision and if so, how much overlap.
    PolygonOverlap overlap =
        object_a.polygon()->GetOverlap(*object_b.polygon());
    // If no collision, continue.
    if (!overlap.overlap)
      continue;
    // Add collision result to objects collision storage.
    absl::Status collision_result = object_a.HandleCollision(
        {.overlap = overlap, .object_type = object_b.object_type()});
    // Oh no, errors are bad, print message.
    if (!collision_result.ok()) {
      std::cout << collision_result.message() << std::endl;
      continue;
    }
    // Add collision hash to hashes so we don't calculate it again.
    hashes_.insert(hash);
    // Go ahead calculate the overlap from the perspective of object b, and
    // add the collision to it. Even if the object is not the type of object
    // that _causes_ collisions, all objects can react to a collision. Overlap
    // reaction_overlap = object_b.polygon()->GetOverlap(*object_a.polygon());
    // If there was an overlap with a to b, there must be an overlap with b to
    // a. if (!overlap.overlap) {
    //   continue;
    // }
    // Add collision result to object_b collision storage.
    // absl::Status reaction_result = object_b.HandleCollision(
    //   {.overlap = overlap, .object_type = object_a.object_type()});
    // if (!collision_result.ok()) {
    //   std::cout << collision_result.message() << std::endl;
    //   continue;
    // }
    // Calculate the hash, and add it to the list of collisions already
    // calculated. int reaction_hash = CollisionHash(object_a.object_id(),
    // object_b.object_id()); Add it to the list of hashes.
    // hashes_.insert(reaction_hash);
  }
}

void CollisionManager::CleanUp() {
  for (auto &[object_id, object] : objects_) {
    // If this object is not the type of object that causes collisions, mostly
    // meaning objects that can move, then continue.
    if (!collsion_types_.contains(object->object_type()))
      continue;
    // Get the objects new areas.
    absl::flat_hash_set<int> area_ids = GetCollisionAreaIds(object->polygon());
    // Get old area ids.
    absl::flat_hash_set<int> &old_area_ids = object_id_to_area_ids_[object_id];
    // If the areas are the same continue.
    if (area_ids == old_area_ids)
      continue;
    // Iterate over the new area ids.
    for (int area_id : area_ids) {
      // Erase all the old area ids.
      old_area_ids.erase(area_id);
      // Add object_id to new areas.
      area_id_to_object_ids_[area_id].insert(object_id);
    }
    // Iterate over any area ids left, and remove the object_id from their list.
    for (int area_id : old_area_ids) {
      area_id_to_object_ids_[area_id].erase(object_id);
    }
    // Finally, replace the old reference mapping this object to area ids.
    object_id_to_area_ids_[object_id] = area_ids;
  }
}

} // namespace zebes
