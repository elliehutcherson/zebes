#include "player.h"

#include <iostream>
#include <memory>

#include "absl/status/status.h" 

#include "engine/sprite_manager.h"
#include "object.h"
#include "config.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Player>> Player::Create(
  const GameConfig* config, const SpriteManager* sprite_manager) {
  auto player = std::unique_ptr<Player>(new Player(config));
  absl::Status result = player->Init(sprite_manager);
  if (!result.ok()) return result;
  return player;
}

Player::Player(const GameConfig* config) : 
config_(config) {};

absl::Status Player::Init(const SpriteManager* sprite_manager) {
  ObjectOptions object_options = {
    .config = config_,
    .object_type = ObjectType::kPlayer,
    .vertices = {
      { .x = config_->player_starting_x, .y = config_->player_starting_y},
      { .x = config_->player_starting_x + 32.0f, .y = config_->player_starting_y},
      { .x = config_->player_starting_x + 32.0f, .y = config_->player_starting_y + 32.0f},
      { .x = config_->player_starting_x, .y = config_->player_starting_y + 32.0f},
    }
  };

  MobileProfile mobile_profile = {
    .id = 0,
    .accelerate_x = 0.5f,
    .accelerate_y = 0.5f,
    .decelerate_x = 1.8f,
    .decelerate_y = 1.8f,
    .velocity_max_x = 10,
    .velocity_max_y = 15,
    .velocity_start_x = 0.0f,
    .velocity_start_y = 0.0f,
    .gravity = true
  };

  mobile_object_ = std::make_unique<MobileObject>(object_options);
  absl::Status result = mobile_object_->AddProfile(mobile_profile);
  if (!result.ok()) return result;

  std::vector<SpriteType> sprite_types = {SpriteType::SamusTurningLeft, 
    SpriteType::SamusTurningRight, SpriteType::SamusRunningLeft, 
    SpriteType::SamusRunningRight, SpriteType::SamusIdleLeft, 
    SpriteType::SamusIdleRight};


  for (SpriteType sprite_type : sprite_types) {
    absl::StatusOr<const Sprite*> sprite = sprite_manager->GetSprite(sprite_type);
    if (!sprite.ok()) return sprite.status();

    SpriteProfile profile = {
      .type = sprite_type, 
      .ticks_per_sprite = (*sprite)->GetConfig()->ticks_per_sprite, 
      .ticks_per_cycle = (*sprite)->GetConfig()->size() * profile.ticks_per_sprite
    };
    result = mobile_object_->AddSpriteProfile(profile);
    if (!result.ok()) return result;
  }

  return absl::OkStatus();
}

void Player::Update(const ControllerState* state) {
  if (state->player_reset) {
    Reset();
  } else {
    UpdateSprite(state);
    UpdatePosition(state);
  }
}

void Player::UpdatePosition(const ControllerState* state) {
  int x = 0, y = 0;
  if (state->b == KeyState::down && mobile_object_->is_grounded()) {
    float velocity = -15.0f;
    mobile_object_->set_velocity_y(velocity);
  } else if (state->b == KeyState::none && mobile_object_->velocity_y() < 0) {
    mobile_object_->set_velocity_y(0);
  } else {
    y++;
  }
  if (state->left != KeyState::none) x--;
  if (state->right != KeyState::none) x++; 
  mobile_object_->MoveWithProfile(x, y);
}

void Player::UpdateSprite(const ControllerState* state) {
  mobile_object_->Update();
  uint64_t cycles = mobile_object_->GetActiveSpriteCycles();
  SpriteType current_type = mobile_object_->GetActiveSpriteProfile()->type;
  SpriteType new_type = SpriteType::Invalid;
  if (current_type == SpriteType::SamusTurningLeft) { 
    if (cycles > 0) new_type = SpriteType::SamusIdleLeft;
  } else if (current_type == SpriteType::SamusTurningRight) {
    if (cycles > 0) new_type = SpriteType::SamusIdleRight;
  } else if (state->left != KeyState::none) {
    if (!FacingLeft()) {
      new_type = SpriteType::SamusTurningLeft;
    } else if (current_type != SpriteType::SamusRunningLeft) {
      new_type = SpriteType::SamusRunningLeft;
    }
  } else if (state->right != KeyState::none) {
    if (!FacingRight()) {
      new_type = SpriteType::SamusTurningRight;
    } else if (current_type != SpriteType::SamusRunningRight) {
      new_type = SpriteType::SamusRunningRight;
    }
  } else if (FacingLeft()) {
    if (current_type != SpriteType::SamusIdleLeft) {
      new_type = SpriteType::SamusIdleLeft;
    }
  } else if (FacingRight()) {
    if (current_type != SpriteType::SamusIdleRight) {
      new_type = SpriteType::SamusIdleRight;
    }
  }
  if (new_type != SpriteType::Invalid) {
    mobile_object_->ResetSprite();
    absl::Status result = mobile_object_->SetActiveSpriteProfile(new_type);
    if (!result.ok()) {
      // We should probably crash in this case. 
      std::cout << result.message() << std::endl;
    }
  }
}

void Player::Reset() {
  mobile_object_->Reset();
  absl::Status result = mobile_object_->SetActiveSpriteProfile(SpriteType::SamusRunningRight);
  if (!result.ok()) {
    // We should probably crash in this case. 
    std::cout << result.message() << std::endl;
  }
}

MobileObject* Player::GetObject() const {
  return mobile_object_.get();
}

void Player::Jump() {
  return;
}

bool Player::FacingLeft() const {
  switch (mobile_object_->GetActiveSpriteProfile()->type) {
    case SpriteType::SamusIdleLeft:
    case SpriteType::SamusTurningLeft:
    case SpriteType::SamusRunningLeft:
      return true;
    default:
      break;
  }
  return false;
}

bool Player::FacingRight() const {
  switch (mobile_object_->GetActiveSpriteProfile()->type) {
    case SpriteType::SamusIdleRight:
    case SpriteType::SamusTurningRight:
    case SpriteType::SamusRunningRight:
      return true;
    default:
      break;
  }
  return false;
}

bool Player::is_grounded() const {
  return mobile_object_->is_grounded();
}

float Player::velocity_x() const {
  return mobile_object_->velocity_x();
}

float Player::velocity_y() const {
  return mobile_object_->velocity_y();
}

float Player::x_center() const {
  return mobile_object_->polygon()->x_min() + (mobile_object_->polygon()->x_max() - mobile_object_->polygon()->x_min());
}

float Player::y_center() const {
  return mobile_object_->polygon()->y_min() + (mobile_object_->polygon()->y_max() - mobile_object_->polygon()->y_min());
}

std::string Player::to_string() const {
  const Polygon* polygon = mobile_object_->polygon();
  std::string message = absl::StrFormat("[x1, x2]: %d, %d\n", polygon->x_min_floor(), polygon->x_max_floor());
  absl::StrAppend(&message, absl::StrFormat("[y1, y2]: %d, %d\n", polygon->y_min_floor(), polygon->y_max_floor()));
  absl::StrAppend(&message, absl::StrFormat("stype: %d\n", mobile_object_->GetActiveSpriteProfile()->type)); 
  absl::StrAppend(&message, absl::StrFormat("sticks: %d\n", mobile_object_->GetActiveSpriteTicks())); 
  absl::StrAppend(&message, absl::StrFormat("scycle: %d\n", mobile_object_->GetActiveSpriteCycles())); 
  absl::StrAppend(&message, absl::StrFormat("is_grounded: %d\n", mobile_object_->is_grounded())); 
  absl::StrAppend(&message, absl::StrFormat("velocity_x: %f\n", mobile_object_->velocity_x())); 
  absl::StrAppend(&message, absl::StrFormat("velocity_y: %f\n", mobile_object_->velocity_x()));
  return message;
}

}  // namespace zebes