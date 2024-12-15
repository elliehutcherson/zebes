#include "object.h"

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "camera.h"
#include "polygon.h"
#include "sprite_interface.h"

namespace zebes {
namespace {

absl::flat_hash_set<ObjectType> GetCompatibleCollisions(ObjectType type) {
  switch (type) {
    case ObjectType::kPlayer:
      return {ObjectType::kTile};
    case ObjectType::kTile:
      return {ObjectType::kPlayer};
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

uint64_t Object::object_id() const { return id_; }

ObjectType Object::object_type() const { return type_; }

const Polygon *Object::polygon() const { return &polygon_; }

bool Object::IsInteractive(ObjectType type) const {
  return compatible_collisions_.contains(type);
}

absl::Status Object::AddPrimaryAxisIndex(uint8_t index,
                                         AxisDirection axis_direction) {
  return polygon_.AddPrimaryAxisIndex(index, axis_direction);
}

void Object::Move(float x, float y) { polygon_.Move(x, y); }

void Object::RegisterCollisionCallback(CollisionCallback callback) {
  callbacks_.push_back(callback);
}

absl::Status Object::HandleCollision(Collision collision) {
  if (collision.overlap.overlap) collision_ = true;
  for (CollisionCallback &callback : callbacks_) {
    callback(collision);
  }
  return absl::OkStatus();
}

void Object::PreUpdate() { collision_ = false; }

void Object::Destroy() { is_destroyed_ = true; }

bool Object::IsDestroyed() { return is_destroyed_; }

absl::Status Object::Render(Camera *camera) const {
  DrawColor color =
      collision_ ? DrawColor::kColorCollide : DrawColor::kColorTile;
  return camera->RenderLines(*polygon_.vertices(), color,
                             /*static_position=*/false);
}

absl::StatusOr<std::unique_ptr<SpriteObject>> SpriteObject::Create(
    ObjectOptions &options) {
  if (options.vertices.empty())
    return absl::InvalidArgumentError("Vertices must not be empty.");

  return std::unique_ptr<SpriteObject>(new SpriteObject(options));
}

SpriteObject::SpriteObject(ObjectOptions &options) : Object(options) { return; }

absl::Status SpriteObject::AddSprite(const SpriteInterface *sprite) {
  const uint16_t sprite_id = sprite->GetId();
  if (sprites_.contains(sprite_id))
    return absl::AlreadyExistsError("Profile already exists.");

  sprites_[sprite_id] = sprite;
  active_sprite_id_ = sprite_id;
  return absl::OkStatus();
}

absl::Status SpriteObject::RemoveSprite(uint16_t sprite_id) {
  auto profile_iter = sprites_.find(sprite_id);
  if (profile_iter == sprites_.end())
    return absl::NotFoundError("Profile not found.");

  sprites_.erase(profile_iter);
  return absl::OkStatus();
}

absl::Status SpriteObject::RemoveSpriteByType(uint16_t sprite_type) {
  for (auto &it : sprites_) {
    if (it.second->GetType() != sprite_type) continue;
    sprites_.erase(it.first);
    return absl::OkStatus();
  }

  return absl::NotFoundError("Profile not found.");
}

absl::Status SpriteObject::SetActiveSprite(uint16_t sprite_id) {
  if (!sprites_.contains(sprite_id))
    return absl::NotFoundError("Profile not found.");

  active_sprite_id_ = sprite_id;
  active_sprite_ticks_ = 0;
  return absl::OkStatus();
}

absl::Status SpriteObject::SetActiveSpriteByType(uint16_t sprite_type) {
  const SpriteInterface *sprite = nullptr;
  for (const auto &it : sprites_) {
    if (it.second->GetType() != sprite_type) continue;
    sprite = it.second;
  }
  if (sprite == nullptr) return absl::NotFoundError("Profile not found.");

  active_sprite_id_ = sprite->GetId();
  active_sprite_ticks_ = 0;
  return absl::OkStatus();
}

const SpriteInterface *SpriteObject::GetActiveSprite() const {
  return sprites_.at(active_sprite_id_);
}

uint8_t SpriteObject::GetActiveSpriteTicks() const {
  return active_sprite_ticks_;
}

uint64_t SpriteObject::GetActiveSpriteCycles() const {
  return active_sprite_cycles_;
}

uint16_t SpriteObject::GetActiveSpriteIndex() const {
  if (active_sprite_id_ == 0) return 0;

  const SpriteInterface *sprite = sprites_.at(active_sprite_id_);
  int ticks_per_sprite = sprite->GetConfig()->ticks_per_sprite;
  if (ticks_per_sprite == 0) return 0;

  return GetActiveSpriteTicks() / ticks_per_sprite;
}

void SpriteObject::Update() {
  if (active_sprite_id_ == 0) return;

  const SpriteInterface *sprite = sprites_.at(active_sprite_id_);
  const SpriteConfig *sprite_config = sprite->GetConfig();

  int ticks_per_cycle = sprite_config->ticks_per_sprite * sprite_config->size();

  active_sprite_ticks_++;
  active_sprite_cycles_ += (active_sprite_ticks_ / ticks_per_cycle);
  active_sprite_ticks_ = active_sprite_ticks_ % ticks_per_cycle;
};

void SpriteObject::ResetSprite() {
  active_sprite_ticks_ = 0;
  active_sprite_cycles_ = 0;
};

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

float MobileObject::velocity_x() const { return velocity_x_; }

void MobileObject::set_velocity_x(float velocity) { velocity_x_ = velocity; }

float MobileObject::velocity_y() const { return velocity_y_; }

void MobileObject::set_velocity_y(float velocity) { velocity_y_ = velocity; }

bool MobileObject::is_grounded() const { return is_grounded_; }

void MobileObject::set_is_grounded(bool is_grounded) {
  is_grounded_ = is_grounded;
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

  // Now that all state has been updated, invoke all callbacks.
  for (CollisionCallback &callback : callbacks_) {
    callback(collision);
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
