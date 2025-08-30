#pragma once

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/object.h"
#include "objects/object_interface.h"
#include "objects/sprite_interface.h"

namespace zebes {

class SpriteObjectInterface : virtual public ObjectInterface {
 public:
  virtual ~SpriteObjectInterface() = default;
  // Add sprite.
  virtual absl::Status AddSprite(const SpriteInterface *sprite) = 0;

  virtual absl::Status RemoveSprite(uint16_t sprite_id) = 0;
  virtual absl::Status RemoveSpriteByType(uint16_t sprite_type) = 0;

  virtual absl::Status SetActiveSprite(uint16_t sprite_id) = 0;
  virtual absl::Status SetActiveSpriteByType(uint16_t sprite_type) = 0;

  virtual const SpriteInterface *GetActiveSprite() const = 0;

  // Get number of ticks for the active sprite.
  virtual uint8_t GetActiveSpriteTicks() const = 0;

  // Get number of cycles for the active sprite.
  virtual uint64_t GetActiveSpriteCycles() const = 0;

  // Get active sprite index depending on the number of ticks.
  virtual uint16_t GetActiveSpriteIndex() const = 0;

  // Update the tick counter.
  virtual void Update() = 0;

  // Reset sprite ticks.
  virtual void ResetSprite() = 0;
};

class SpriteObject : public Object, virtual public SpriteObjectInterface {
 public:
  // Factory method for creating sprite object.
  static absl::StatusOr<std::unique_ptr<SpriteObject>> Create(
      ObjectOptions &options);

  // Default destructor.
  ~SpriteObject() = default;

  absl::Status AddSprite(const SpriteInterface *profile) override;

  absl::Status RemoveSprite(uint16_t sprite_id) override;
  absl::Status RemoveSpriteByType(uint16_t sprite_type) override;

  // Set the active sprite.
  absl::Status SetActiveSprite(uint16_t sprite_id) override;
  absl::Status SetActiveSpriteByType(uint16_t sprite_type) override;

  // Get the active sprite.
  const SpriteInterface *GetActiveSprite() const override;

  // Get number of ticks for the active sprite.
  uint8_t GetActiveSpriteTicks() const override;

  // Get number of cycles for the active sprite.
  uint64_t GetActiveSpriteCycles() const override;

  // Get active sprite index depending on the number of ticks.
  uint16_t GetActiveSpriteIndex() const override;

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
  uint16_t active_sprite_id_ = 0;
  uint16_t default_sprite_id_ = 0;
  absl::flat_hash_map<uint16_t, const SpriteInterface *> sprites_;
};

}  // namespace zebes
