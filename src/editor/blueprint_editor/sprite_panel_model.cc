#include "editor/blueprint_editor/sprite_panel_model.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"

namespace zebes {

void SpritePanelModel::SetSprites(std::vector<Sprite> sprites) {
  std::sort(sprites.begin(), sprites.end(),
            [](const Sprite& a, const Sprite& b) { return a.name < b.name; });
  sprites_ = std::move(sprites);

  if (selected_sprite_index_ >= static_cast<int>(sprites_.size())) {
    selected_sprite_index_ = -1;
  }
}

void SpritePanelModel::SelectSpriteIndex(int index) {
  if (index < 0 || index >= static_cast<int>(sprites_.size())) {
    selected_sprite_index_ = -1;
    return;
  }
  selected_sprite_index_ = index;
}

absl::Status SpritePanelModel::AttachSelectedSprite() {
  if (selected_sprite_index_ < 0 || selected_sprite_index_ >= static_cast<int>(sprites_.size())) {
    return absl::OutOfRangeError("Cannot attach sprite: selection is out of range");
  }
  AttachSprite(sprites_[selected_sprite_index_]);
  return absl::OkStatus();
}

void SpritePanelModel::AttachSprite(Sprite sprite) {
  editing_sprite_ = std::move(sprite);
  frame_index_ = 0;
}

void SpritePanelModel::DetachSprite() {
  frame_index_ = 0;
  selected_sprite_index_ = -1;
  editing_sprite_.reset();
}

Sprite* SpritePanelModel::editing_sprite() {
  return editing_sprite_.has_value() ? &*editing_sprite_ : nullptr;
}

const Sprite* SpritePanelModel::editing_sprite() const {
  return editing_sprite_.has_value() ? &*editing_sprite_ : nullptr;
}

absl::Status SpritePanelModel::SetFrameIndex(int index) {
  if (!editing_sprite_.has_value()) {
    return absl::FailedPreconditionError("Cannot select a frame without an attached sprite");
  }
  if (index < 0 || index >= static_cast<int>(editing_sprite_->frames.size())) {
    return absl::OutOfRangeError("Cannot select frame: index is out of range");
  }
  frame_index_ = index;
  return absl::OkStatus();
}

SpriteFrame* SpritePanelModel::current_frame() {
  if (!editing_sprite_.has_value() || frame_index_ < 0 ||
      frame_index_ >= static_cast<int>(editing_sprite_->frames.size())) {
    return nullptr;
  }
  return &editing_sprite_->frames[frame_index_];
}

absl::StatusOr<SpriteFramePreview> SpritePanelModel::CalculateFramePreview(const SpriteFrame& frame,
                                                                           int texture_width,
                                                                           int texture_height,
                                                                           float available_width) {
  if (texture_width <= 0 || texture_height <= 0) {
    return absl::InvalidArgumentError("Texture dimensions must be positive");
  }
  if (frame.texture_w <= 0 || frame.texture_h <= 0) {
    return absl::InvalidArgumentError("Sprite frame dimensions must be positive");
  }

  float scale = 1.0f;
  if (available_width > 0.0f && frame.texture_w > available_width) {
    scale = available_width / static_cast<float>(frame.texture_w);
  }

  return SpriteFramePreview{
      .uv0_x = static_cast<float>(frame.texture_x) / texture_width,
      .uv0_y = static_cast<float>(frame.texture_y) / texture_height,
      .uv1_x = static_cast<float>(frame.texture_x + frame.texture_w) / texture_width,
      .uv1_y = static_cast<float>(frame.texture_y + frame.texture_h) / texture_height,
      .width = static_cast<float>(frame.texture_w) * scale,
      .height = static_cast<float>(frame.texture_h) * scale,
  };
}

}  // namespace zebes
