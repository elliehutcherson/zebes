#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"

#include "absl/status/statusor.h"
#include "camera.h"
#include "config.h"
#include "polygon.h"

namespace zebes {

enum ObjectType : uint16_t {
  kInvalid = 0,
  kPlayer = 1,
  kTile = 2,
};

struct Collision {
  PolygonOverlap overlap;
  ObjectType object_type = kInvalid;
  bool hit_ground() const {
    return overlap.overlap && object_type == kTile &&
           overlap.min_overlap_x() == 0 && overlap.min_overlap_y() > 0;
  }
};

using CollisionCallback = std::function<void(Collision &collision)>;

struct ObjectOptions {
  const GameConfig *config;
  ObjectType object_type = kInvalid;
  std::vector<Point> vertices;
};

class ObjectInterface {
public:
  // Return id of the object.
  virtual uint64_t object_id() const = 0;
  // Return type of the object.
  virtual ObjectType object_type() const = 0;
  // Return pointer to polygon.
  virtual const Polygon *polygon() const = 0;
  // Check if object can collide with another object.
  virtual bool IsInteractive(ObjectType type) const = 0;
  // Register a callback for any collision events.
  virtual void RegisterCollisionCallback(CollisionCallback callback) = 0;
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

class Object : virtual public ObjectInterface {
public:
  static absl::StatusOr<std::unique_ptr<Object>> Create(ObjectOptions &options);
  
  ~Object() = default;
 
  uint64_t object_id() const override;
 
  ObjectType object_type() const override;
 
  const Polygon *polygon() const override;
 
  bool IsInteractive(ObjectType type) const override;
 
  void RegisterCollisionCallback(CollisionCallback callback) override;
 
  absl::Status HandleCollision(Collision collision) override;
 
  absl::Status AddPrimaryAxisIndex(
      uint8_t index,
      AxisDirection axis_direction = AxisDirection::axis_none) override;
 
  void Move(float x, float y) override;
  
  void PreUpdate() override;

  void Destroy() override;
  
  bool IsDestroyed() override;

  // Convert points to SDL objects, and render to window.
  absl::Status Render(Camera* camera) const;

protected:
  Object(ObjectOptions &options);

  const GameConfig *config_;
  uint64_t id_ = 0;
  ObjectType type_ = ObjectType::kInvalid;
  bool is_destroyed_ = false;
  bool collision_ = false;
  Polygon polygon_;
  absl::flat_hash_set<ObjectType> compatible_collisions_;
  std::vector<CollisionCallback> callbacks_;
};

struct SpriteProfile {
  // Type of sprite, the sprite manager contain the rest of
  // the information. Pass the sprite type to the sprite manager
  // and the current tick and the position and it will take care
  // of the rest.
  SpriteType type;
  // Max number of ticks per rotation.
  int ticks_per_sprite = 0;
  // Max number of ticks per rotation.
  int ticks_per_cycle = 0;
};

class SpriteObjectInterface : virtual public ObjectInterface {
public:
  // Return pointer to polygon.
  // virtual const Polygon* polygon() const = 0;
  // Get sprite profile.
  virtual const SpriteProfile *profile(SpriteType type) const = 0;
  // Add sprite profile.
  virtual absl::Status AddSpriteProfile(SpriteProfile profile) = 0;
  // Remove mobile profile.
  virtual absl::Status RemoveSpriteProfile(SpriteType type) = 0;
  // Set the active mobile profile.
  virtual absl::Status SetActiveSpriteProfile(SpriteType type) = 0;
  // Get the active mobile profile.
  virtual const SpriteProfile *GetActiveSpriteProfile() const = 0;
  // Get number of ticks for the active sprite.
  virtual uint8_t GetActiveSpriteTicks() const = 0;
  // Get number of cycles for the active sprite.
  virtual uint64_t GetActiveSpriteCycles() const = 0;
  // Get active sprite index depending on the number of ticks.
  virtual uint64_t GetActiveSpriteIndex() const = 0;
  // Update the tick counter.
  virtual void Update() = 0;
  // Reset sprite ticks.
  virtual void ResetSprite() = 0;
};

class SpriteObject : public Object, virtual public SpriteObjectInterface {
public:
  // Factory method for creating sprite object.
  static absl::StatusOr<std::unique_ptr<SpriteObject>>
  Create(ObjectOptions &options);
  // Default destructor.
  ~SpriteObject() = default;
  // Get mobile profile. Is no profile is found, nullptr will be returned.
  const SpriteProfile *profile(SpriteType type) const override;
  // Add mobile profile.
  absl::Status AddSpriteProfile(SpriteProfile profile) override;
  // Remove mobile profile.
  absl::Status RemoveSpriteProfile(SpriteType type) override;
  // Set the active mobile profile.
  absl::Status SetActiveSpriteProfile(SpriteType type) override;
  // Get the active mobile profile.
  const SpriteProfile *GetActiveSpriteProfile() const override;
  // Get number of ticks for the active sprite.
  uint8_t GetActiveSpriteTicks() const override;
  // Get number of cycles for the active sprite.
  uint64_t GetActiveSpriteCycles() const override;
  // Get active sprite index depending on the number of ticks.
  uint64_t GetActiveSpriteIndex() const override;
  // Update the tick counter.
  void Update() override;
  // Reset velocity and position.
  void ResetSprite() override;

protected:
  // Use the factory method instead.
  SpriteObject(ObjectOptions &options);

private:
  uint8_t active_sprite_ticks_ = 0;
  uint64_t active_sprite_cycles_ = 0;
  SpriteType active_sprite_type_ = SpriteType::Invalid;
  SpriteType default_sprite_type_ = SpriteType::Invalid;
  absl::flat_hash_map<SpriteType, SpriteProfile> profiles_;
};

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
  static absl::StatusOr<std::unique_ptr<MobileObject>>
  Create(ObjectOptions &options);
  // Default destructor.
  ~MobileObject() = default;
  // Get Velocity in the x direction.
  float velocity_x() const override;
  // Set Velocity in the x direction.
  void set_velocity_x(float velocity) override;
  // Get Velocity in the y direction.
  float velocity_y() const override;
  // Set Velocity in the y direction.
  void set_velocity_y(float velocity) override;
  // Check if the object is grounded.
  bool is_grounded() const override;
  // Set if the object is grounded.
  void set_is_grounded(bool is_grounded) override;
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

} // namespace zebes
