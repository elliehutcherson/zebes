#pragma once

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "config.h"
#include "object.h"
#include "polygon.h"

namespace zebes {

struct CollisionArea {
  int id = -1;
  float x_min = 0;
  float x_max = 0;
  float y_min = 0;
  float y_max = 0;
  std::string DebugString() {
    return absl::StrFormat("id: %d, x_min: %f, x_max: %f, y_min: %f, y_max: %f",
                           id, x_min, x_max, y_min, y_max);
  }
};

class CollisionManager {
public:
  CollisionManager(const GameConfig *config);
  ~CollisionManager() = default;
  // Initialize collision manager, including areas.
  absl::Status Init();
  // Get the number of areas in the x dimension.
  int NumberAreasX() const;
  // Get the number of areas in the y dimension.
  int NumberAreasY() const;
  // Get the area index for the absolute position in the x dimension.
  int AreaIndexX(float x) const;
  // Get the area index for the absolute position in the y dimension.
  int AreaIndexY(float y) const;
  // Get the id corresponding to the x and y area indexes.
  int GetCollisionAreaId(int x, int y) const;
  // Get the all area ids for a given polygons position.
  absl::flat_hash_set<int> GetCollisionAreaIds(const Polygon *polygon) const;
  // Add object to collision manager.
  absl::Status AddObject(ObjectInterface *object);
  // Check for collisions between all objects.
  void Update();
  // After update pipeline, collisions could have affected positions.
  // Check for new positions, and update areas.
  void CleanUp();

private:
  // Check for collisions between all objects within the specified area.
  void UpdateArea(int area_id, ObjectInterface &object_a);

  const GameConfig *config_;
  absl::flat_hash_set<ObjectType> collsion_types_ = {ObjectType::kPlayer};
  absl::flat_hash_map<uint64_t, ObjectInterface *> objects_;
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<int>>
      object_id_to_area_ids_;
  absl::flat_hash_map<int, CollisionArea> areas_;
  absl::flat_hash_map<int, absl::flat_hash_set<uint64_t>>
      area_id_to_object_ids_;
  absl::flat_hash_set<int> hashes_;
};

} // namespace zebes
