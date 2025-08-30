#include "mobile_object.h"

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "objects/object.h"
#include "objects/object_interface.h"
#include "objects/polygon.h"
#include "objects/sprite_interface.h"
#include "objects/sprite_object.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<MobileObject>> MobileObject::Create(
    ObjectOptions &options) {
  if (options.vertices.empty())
    return absl::InvalidArgumentError("Vertices must not be empty.");
  return std::unique_ptr<MobileObject>(new MobileObject(options));
}

MobileObject::MobileObject(ObjectOptions &options) : SpriteObject(options) {}

const MobileProfile *MobileObject::profile(uint8_t id) const {
  auto profiles_iter = profiles_.find(id);
  if (profiles_iter == profiles_.end()) return nullptr;
  return &profiles_iter->second;
}

absl::Status MobileObject::AddProfile(MobileProfile profile) {
  profiles_[profile.id] = std::move(profile);
  return absl::OkStatus();
}

absl::Status MobileObject::RemoveProfile(uint8_t id) {
  auto profiles_iter = profiles_.find(id);
  if (profiles_iter == profiles_.end())
    return absl::NotFoundError("Mobile profile not found");

  profiles_.erase(profiles_iter);
  return absl::OkStatus();
}

absl::Status MobileObject::SetActiveProfile(uint8_t id) {
  if (!profiles_.contains(id))
    return absl::NotFoundError("Mobile profile not found");

  active_profile_id_ = id;
  return absl::OkStatus();
}

const MobileProfile *MobileObject::GetActiveProfile() {
  auto profiles_iter = profiles_.find(active_profile_id_);
  if (profiles_iter == profiles_.end()) return nullptr;
  return &profiles_iter->second;
}

void MobileObject::MoveWithProfile(float x, float y) {
  const MobileProfile *active_profile = GetActiveProfile();

  // Calculate acceleration based on input
  float delta_vx = active_profile->accelerate_x * x;
  float delta_vy = active_profile->accelerate_y * y;

  // Apply acceleration and deceleration
  velocity_x_ = Accelerate(velocity_x_, delta_vx, active_profile->decelerate_x,
                           active_profile->velocity_max_x);
  velocity_y_ = Accelerate(velocity_y_, delta_vy, active_profile->decelerate_y,
                           active_profile->velocity_max_y);

  // Update the object's position
  polygon_.Move(velocity_x_, velocity_y_);
}

float MobileObject::Accelerate(float velocity, float delta_v,
                               float deceleration, float max_velocity) const {
  if (delta_v != 0.0f) {
    return std::max(std::min(velocity + delta_v, max_velocity), -max_velocity);
  }

  if (velocity > 0.0f) {
    return std::max(velocity - deceleration, 0.0f);
  } else if (velocity < 0.0f) {
    return std::min(velocity + deceleration, 0.0f);
  }
  return 0.0f;
}

absl::Status MobileObject::HandleCollision(Collision collision) {
  // Set variable to indicate a collision on this frame.
  collision_ = true;

  // Set variable to indicate object is grounded on this frame if applicable.
  if (collision.hit_ground()) is_grounded_ = true;

  // Move the polygon's position such that there is no overlap between
  // this object and the other colliding object.
  if (collision.overlap.has_primary) {
    polygon_.Move(-collision.overlap.min_primary_overlap_x(),
                  -collision.overlap.min_primary_overlap_y());

    // Update velocity.
    // if (collision.overlap.overlap_x() != 0) velocity_x_ = 0;
    if (collision.overlap.min_primary_overlap_y() != 0) velocity_y_ = 0;

  } else {
    polygon_.Move(-collision.overlap.min_overlap_x(),
                  -collision.overlap.min_overlap_y());
    // Update velocity.
    // if (collision.overlap.overlap_x() != 0) velocity_x_ = 0;
    if (collision.overlap.min_overlap_y() != 0) velocity_y_ = 0;
  }

  return absl::OkStatus();
}

void MobileObject::Reset() {
  polygon_ = Polygon(start_verticies_);
  velocity_x_ = 0;
  velocity_y_ = 0;
  collision_ = false;
  is_grounded_ = false;
}

}  // namespace zebes
