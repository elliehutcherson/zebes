#include "editor/sprite_editor/sprite_editor_model.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"

namespace zebes {

void SpriteEditorModel::SetSprites(std::vector<Sprite> sprites) {
  sprites_.clear();
  for (Sprite& sprite : sprites) {
    AssetCatalogKey key{.display_name = sprite.name, .id = sprite.id};
    sprites_.emplace(std::move(key), std::move(sprite));
  }
}

void SpriteEditorModel::SetTextures(std::vector<Texture> textures) {
  textures_.clear();
  for (Texture& texture : textures) {
    AssetCatalogKey key{.display_name = texture.name, .id = texture.id};
    textures_.emplace(std::move(key), std::move(texture));
  }
}

absl::Status SpriteEditorModel::SelectSprite(const std::string& id) {
  for (const auto& [key, sprite] : sprites_) {
    if (sprite.id != id) continue;
    sprite_ = sprite;
    is_new_sprite_ = false;
    SetEditName(sprite.name);
    original_frames_ = sprite.frames;
    active_frame_index_ = -1;
    return absl::OkStatus();
  }
  return absl::NotFoundError("Selected sprite was not found");
}

void SpriteEditorModel::BeginNewSprite() {
  sprite_ = {};
  is_new_sprite_ = true;
  SetEditName("");
  original_frames_.clear();
  active_frame_index_ = -1;
}

void SpriteEditorModel::ClearSelection() {
  sprite_ = {};
  is_new_sprite_ = false;
  edit_name_buffer_.clear();
  original_frames_.clear();
  active_frame_index_ = -1;
}

absl::Status SpriteEditorModel::SelectTexture(const std::string& texture_id, TextureHandle handle) {
  if (!has_selection()) return absl::FailedPreconditionError("No sprite is selected");
  sprite_.texture_id = texture_id;
  sprite_.texture_handle = handle;
  return absl::OkStatus();
}

absl::StatusOr<Sprite> SpriteEditorModel::BuildCreateRequest() const {
  if (!is_new_sprite_) return absl::FailedPreconditionError("Not creating a sprite");
  if (sprite_.texture_id.empty()) return absl::InvalidArgumentError("Texture must be selected");
  Sprite result = sprite_;
  result.name = edit_name_buffer_.c_str();
  return result;
}

absl::StatusOr<Sprite> SpriteEditorModel::BuildUpdateRequest() const {
  if (is_new_sprite_ || sprite_.id.empty()) {
    return absl::FailedPreconditionError("No existing sprite is selected");
  }
  Sprite result = sprite_;
  result.name = edit_name_buffer_.c_str();
  for (int i = 0; i < static_cast<int>(result.frames.size()); ++i) result.frames[i].index = i;
  return result;
}

void SpriteEditorModel::FinishSave() {
  sprite_.name = edit_name_buffer_.c_str();
  ReindexFrames();
  original_frames_ = sprite_.frames;
}

void SpriteEditorModel::ToggleActiveFrame(int index) {
  active_frame_index_ = active_frame_index_ == index ? -1 : index;
}

absl::Status SpriteEditorModel::AddFrame() {
  if (!has_selection()) return absl::FailedPreconditionError("No sprite is selected");
  sprite_.frames.push_back(
      SpriteFrame{.texture_w = 32, .texture_h = 32, .render_w = 32, .render_h = 32});
  active_frame_index_ = static_cast<int>(sprite_.frames.size()) - 1;
  ReindexFrames();
  return absl::OkStatus();
}

absl::Status SpriteEditorModel::DeleteFrame(int index) {
  if (!IsValidFrameIndex(index)) return absl::OutOfRangeError("Frame index is out of range");
  sprite_.frames.erase(sprite_.frames.begin() + index);
  if (active_frame_index_ == index) active_frame_index_ = -1;
  if (active_frame_index_ > index) --active_frame_index_;
  ReindexFrames();
  return absl::OkStatus();
}

absl::Status SpriteEditorModel::MoveFrameLeft(int index) {
  if (!IsValidFrameIndex(index) || index == 0)
    return absl::OutOfRangeError("Cannot move frame left");
  std::swap(sprite_.frames[index], sprite_.frames[index - 1]);
  if (active_frame_index_ == index)
    active_frame_index_ = index - 1;
  else if (active_frame_index_ == index - 1)
    active_frame_index_ = index;
  ReindexFrames();
  return absl::OkStatus();
}

absl::Status SpriteEditorModel::MoveFrameRight(int index) {
  if (!IsValidFrameIndex(index) || index + 1 >= static_cast<int>(sprite_.frames.size())) {
    return absl::OutOfRangeError("Cannot move frame right");
  }
  std::swap(sprite_.frames[index], sprite_.frames[index + 1]);
  if (active_frame_index_ == index)
    active_frame_index_ = index + 1;
  else if (active_frame_index_ == index + 1)
    active_frame_index_ = index;
  ReindexFrames();
  return absl::OkStatus();
}

void SpriteEditorModel::ResetFrames() {
  sprite_.frames = original_frames_;
  active_frame_index_ = -1;
}

absl::Status SpriteEditorModel::ApplyFrameScale(int index, int scale) {
  if (!IsValidFrameIndex(index)) return absl::OutOfRangeError("Frame index is out of range");
  if (scale < 1) return absl::InvalidArgumentError("Scale must be positive");
  SpriteFrame& frame = sprite_.frames[index];
  frame.render_w = frame.texture_w * scale;
  frame.render_h = frame.texture_h * scale;
  return absl::OkStatus();
}

absl::Status SpriteEditorModel::ClampFrameToTexture(int index, int texture_width,
                                                    int texture_height) {
  if (!IsValidFrameIndex(index)) return absl::OutOfRangeError("Frame index is out of range");
  if (texture_width < 0 || texture_height < 0) {
    return absl::InvalidArgumentError("Texture dimensions cannot be negative");
  }
  SpriteFrame& frame = sprite_.frames[index];
  frame.texture_x = std::clamp(frame.texture_x, 0, texture_width);
  frame.texture_y = std::clamp(frame.texture_y, 0, texture_height);
  frame.texture_w = std::clamp(frame.texture_w, 0, texture_width - frame.texture_x);
  frame.texture_h = std::clamp(frame.texture_h, 0, texture_height - frame.texture_y);
  return absl::OkStatus();
}

void SpriteEditorModel::ZoomTextureIn() { texture_zoom_ = std::min(5.0f, texture_zoom_ + 0.1f); }
void SpriteEditorModel::ZoomTextureOut() { texture_zoom_ = std::max(0.1f, texture_zoom_ - 0.1f); }

absl::StatusOr<AnimationPreviewLayout> SpriteEditorModel::CalculateAnimationPreviewLayout(
    const std::vector<SpriteFrame>& frames, int current_frame_index, float available_width,
    float maximum_height) {
  if (current_frame_index < 0 || current_frame_index >= static_cast<int>(frames.size())) {
    return absl::OutOfRangeError("Current animation frame is out of range");
  }
  if (available_width <= 0.0f || maximum_height <= 0.0f) {
    return absl::InvalidArgumentError("Preview constraints must be positive");
  }

  int64_t left = 0;
  int64_t top = 0;
  int64_t right = 0;
  int64_t bottom = 0;
  bool has_renderable_frame = false;
  for (const SpriteFrame& frame : frames) {
    if (frame.render_w <= 0 || frame.render_h <= 0) continue;
    has_renderable_frame = true;
    const int64_t frame_left = frame.offset_x;
    const int64_t frame_top = frame.offset_y;
    const int64_t frame_right = frame_left + frame.render_w;
    const int64_t frame_bottom = frame_top + frame.render_h;
    left = std::min(left, frame_left);
    top = std::min(top, frame_top);
    right = std::max(right, frame_right);
    bottom = std::max(bottom, frame_bottom);
  }

  const SpriteFrame& current_frame = frames[current_frame_index];
  if (!has_renderable_frame || current_frame.render_w <= 0 || current_frame.render_h <= 0) {
    return absl::InvalidArgumentError("Animation frame dimensions must be positive");
  }

  const int64_t bounds_width = right - left;
  const int64_t bounds_height = bottom - top;
  const float scale = std::min({1.0f, available_width / static_cast<float>(bounds_width),
                                maximum_height / static_cast<float>(bounds_height)});

  return AnimationPreviewLayout{
      .bounds_left = left,
      .bounds_top = top,
      .bounds_right = right,
      .bounds_bottom = bottom,
      .scale = scale,
      .canvas_width = static_cast<float>(bounds_width) * scale,
      .canvas_height = static_cast<float>(bounds_height) * scale,
      .frame_x = static_cast<float>(static_cast<int64_t>(current_frame.offset_x) - left) * scale,
      .frame_y = static_cast<float>(static_cast<int64_t>(current_frame.offset_y) - top) * scale,
      .frame_width = static_cast<float>(current_frame.render_w) * scale,
      .frame_height = static_cast<float>(current_frame.render_h) * scale,
  };
}

void SpriteEditorModel::SetEditName(const std::string& name) {
  edit_name_buffer_ = name;
  edit_name_buffer_.resize(256, '\0');
}

void SpriteEditorModel::ReindexFrames() {
  for (int i = 0; i < static_cast<int>(sprite_.frames.size()); ++i) sprite_.frames[i].index = i;
}

bool SpriteEditorModel::IsValidFrameIndex(int index) const {
  return index >= 0 && index < static_cast<int>(sprite_.frames.size());
}

}  // namespace zebes
