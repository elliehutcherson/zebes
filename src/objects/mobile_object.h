#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "objects/object_interface.h"
#include "objects/sprite_object.h"
#include "common/vector.h"

namespace zebes {

struct MobileProfile {
  uint8_t id = 0;
  float accelerate_x = 0.0f;
  float accelerate_y = 0.0f;
  float decelerate_x = 0.0f;
  float decelerate_y = 0.0f;
  float velocity_max_x = 0.0f;
  float velocity_max_y = 0.0f;
  float velocity_start_x = 0.0f;
  float velocity_start_y = 0.0f;
  bool gravity = true;
};

class MobileInterface : virtual public SpriteObjectInterface {
 public:
  // Get Velocity in the x direction.
  virtual float velocity_x() const = 0;

  // Set Velocity in the x direction.
  virtual void set_velocity_x(float velocity) = 0;

  // Get Velocity in the y direction.
  virtual float velocity_y() const = 0;

  // Set Velocity in the y direction.
  virtual void set_velocity_y(float velocity) = 0;

  // Check if the object is grounded.
  virtual bool is_grounded() const = 0;

  // Set if the object is grounded.
  virtual void set_is_grounded(bool is_grounded) = 0;

  // Get mobile profile.
  virtual const MobileProfile *profile(uint8_t id) const = 0;

  // Add mobile profile.
  virtual absl::Status AddProfile(MobileProfile profile) = 0;

  // Remove mobile profile.
  virtual absl::Status RemoveProfile(uint8_t id) = 0;

  // Set the active mobile profile.
  virtual absl::Status SetActiveProfile(uint8_t id) = 0;

  // Get the active mobile profile.
  virtual const MobileProfile *GetActiveProfile() = 0;

  // Mobiles have the ability to move accodring to their mobile profile.
  virtual void MoveWithProfile(float x, float y) = 0;

  // Reset velocity and position.
  virtual void Reset() = 0;
};

class MobileObject : public SpriteObject, virtual public MobileInterface {
 public:
  // Factory method for creating sprite object.
  static absl::StatusOr<std::unique_ptr<MobileObject>> Create(
      ObjectOptions &options);
  // Default destructor.
  ~MobileObject() = default;

  // Get Velocity in the x direction.
  float velocity_x() const override { return velocity_x_; }

  // Set Velocity in the x direction.
  void set_velocity_x(float velocity) override { velocity_x_ = velocity; }

  // Get Velocity in the y direction.
  float velocity_y() const override { return velocity_y_; }

  // Set Velocity in the y direction.
  void set_velocity_y(float velocity) override { velocity_y_ = velocity; }

  // Check if the object is grounded.
  bool is_grounded() const override { return is_grounded_; }

  // Set if the object is grounded.
  void set_is_grounded(bool is_grounded) override {
    is_grounded_ = is_grounded;
  }

  // Get mobile profile. Is no profile is found, nullptr will be returned.
  const MobileProfile *profile(uint8_t id) const override;

  // Add mobile profile.
  absl::Status AddProfile(MobileProfile profile) override;

  // Remove mobile profile.
  absl::Status RemoveProfile(uint8_t id) override;

  // Set the active mobile profile.
  absl::Status SetActiveProfile(uint8_t id) override;

  // Get the active mobile profile.
  const MobileProfile *GetActiveProfile() override;

  // Mobiles have the ability to move.
  void MoveWithProfile(float x, float y) override;

  // After moving, followed by collision, resolve any potential collisions.
  absl::Status HandleCollision(Collision collision) override;

  // Reset velocity and position.
  void Reset() override;

 private:
  // Use the factory method instead.
  MobileObject(ObjectOptions &options);

  // Accelerate or decelerate velocity in a particular dimension. Returns the
  // new velocity.
  float Accelerate(float velocity, float delta_v, float deceleration,
                   float max_velocity) const;

  bool is_grounded_ = false;
  float velocity_x_ = 0;
  float velocity_y_ = 0;
  uint8_t active_profile_id_ = 0;
  absl::flat_hash_map<uint8_t, MobileProfile> profiles_;
  std::vector<Point> start_verticies_;
};

}  // namespace zebes
