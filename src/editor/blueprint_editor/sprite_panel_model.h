#pragma once

#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/sprite.h"

namespace zebes {

struct SpriteFramePreview {
  float uv0_x = 0.0f;
  float uv0_y = 0.0f;
  float uv1_x = 0.0f;
  float uv1_y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Owns SpritePanel's editable state and calculations without depending on
// ImGui, SDL, or the application API.
class SpritePanelModel {
 public:
  void SetSprites(std::vector<Sprite> sprites);

  const std::vector<Sprite>& sprites() const { return sprites_; }
  int selected_sprite_index() const { return selected_sprite_index_; }
  void SelectSpriteIndex(int index);

  absl::Status AttachSelectedSprite();
  void AttachSprite(Sprite sprite);
  void DetachSprite();

  bool has_editing_sprite() const { return editing_sprite_.has_value(); }
  Sprite* editing_sprite();
  const Sprite* editing_sprite() const;

  int frame_index() const { return frame_index_; }
  absl::Status SetFrameIndex(int index);
  SpriteFrame* current_frame();

  static absl::StatusOr<SpriteFramePreview> CalculateFramePreview(const SpriteFrame& frame,
                                                                  int texture_width,
                                                                  int texture_height,
                                                                  float available_width);

 private:
  int selected_sprite_index_ = -1;
  int frame_index_ = 0;
  std::vector<Sprite> sprites_;
  std::optional<Sprite> editing_sprite_;
};

}  // namespace zebes
