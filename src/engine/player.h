#pragma once

#include <memory>

#include "absl/status/status.h"
#include "config.h"
#include "controller.h"
#include "engine/sprite_manager.h"
#include "focus.h"
#include "object.h"
#include "sprite.h"

namespace zebes {

class Player : public Focus {
 public:
  static absl::StatusOr<std::unique_ptr<Player>> Create(
      const GameConfig *config, const SpriteManager *sprite_manager);
  ~Player() = default;
  // All updates per tick for player.
  void Update(const ControllerState *state) override;
  // Reset the player position, velocity, and sprite.
  void Reset();
  // Get the player's object.
  MobileObject *GetObject() const;
  // Get the player's current sprite.
  const Sprite *GetSprite() const;
  // Check if the current sprite if facing left.
  bool FacingLeft() const;
  // Check if the current sprite if facing right.
  bool FacingRight() const;
  // Check is player is against ground.
  bool is_grounded() const;
  // Get velocity in x direction.
  float velocity_x() const;
  // Get velocity in y direction.
  float velocity_y() const;
  // Get the player's x position.
  float x_center() const override;
  // Get the player's y position.
  float y_center() const override;
  // Get the player's string representation.
  std::string to_string() const override;

 private:
  Player(const GameConfig *config);
  // Initialize Player
  absl::Status Init(const SpriteManager *sprite_manager);
  // Get the active sprite type for the mobile object.
  SpriteType GetActiveSpriteType() const;
  // Jump player.
  void Jump();
  // Update the current sprite.
  void UpdateSprite(const ControllerState *state);
  // Move player.
  void UpdatePosition(const ControllerState *state);
  // Unowned objects for the player.
  const GameConfig *config_;
  // Objects owned by the player.
  std::unique_ptr<MobileObject> mobile_object_;
  SpriteType current_sprite_type_ = SpriteType::SamusIdleRight;
};

}  // namespace zebes