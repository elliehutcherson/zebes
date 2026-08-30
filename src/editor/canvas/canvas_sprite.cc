#include "editor/canvas/canvas_sprite.h"

#include <cmath>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

absl::StatusOr<bool> CanvasSprite::Render(Canvas& canvas, int frame_index, bool input_allowed) {
  if (frame_index < 0 || frame_index >= sprite_.frames.size()) {
    return absl::InvalidArgumentError("Index out of range.");
  }
  if (!texture_) {
    return absl::InternalError("SDL_Texture must not be null!");
  }

  SpriteFrame local_frame_copy;
  SpriteFrame* frame_ptr = nullptr;

  if (is_animating_) {
    // Editing is off while playing: the frame shown is a copy owned by the
    // animator, so a drag would write to something discarded next tick.
    input_allowed = false;
    ASSIGN_OR_RETURN(local_frame_copy, animator_.GetCurrentFrame(sprite_.frames));
    frame_ptr = &local_frame_copy;
  } else {
    frame_ptr = &sprite_.frames[frame_index];
  }

  const SpriteFrameRenderBounds frame_bounds = CalculateSpriteFrameRenderBounds(*frame_ptr);
  if (!frame_bounds.IsValid()) {
    return absl::InvalidArgumentError("Sprite frame must have positive render dimensions.");
  }
  ImVec2 p1 = canvas.WorldToScreen(
      {static_cast<double>(frame_bounds.left), static_cast<double>(frame_bounds.top)});
  ImVec2 p2 = canvas.WorldToScreen(
      {static_cast<double>(frame_bounds.right), static_cast<double>(frame_bounds.bottom)});

  ImDrawList* draw_list = canvas.GetDrawList();

  int tex_w = 0, tex_h = 0;
  SDL_Texture* texture = SdlTextureHandleAdapter::ToNative(texture_);
  SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);

  if (tex_w > 0 && tex_h > 0) {
    ImVec2 uv0(static_cast<float>(frame_ptr->texture_x) / tex_w,
               static_cast<float>(frame_ptr->texture_y) / tex_h);
    ImVec2 uv1(static_cast<float>(frame_ptr->texture_x + frame_ptr->texture_w) / tex_w,
               static_cast<float>(frame_ptr->texture_y + frame_ptr->texture_h) / tex_h);

    draw_list->AddImage(reinterpret_cast<ImTextureID>(texture), p1, p2, uv0, uv1);
  }

  draw_list->AddRect(p1, p2, IM_COL32(100, 200, 100, 255));

  if (!input_allowed) {
    is_dragging_ = false;
    return false;
  }

  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered =
      mouse_pos.x >= p1.x && mouse_pos.x <= p2.x && mouse_pos.y >= p1.y && mouse_pos.y <= p2.y;

  if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    is_dragging_ = true;
    drag_acc_x_ = 0;
    drag_acc_y_ = 0;
  }

  // Writes the new offsets and reports whether they differ, so the caller only
  // marks the sprite dirty when a drag actually moved it.
  auto is_modified = [this](SpriteFrame& frame, int x, int y) {
    if (is_animating_) {
      return false;
    }
    bool modified = false;
    if (x != frame.offset_x) {
      frame.offset_x = x;
      modified = true;
    }
    if (y != frame.offset_y) {
      frame.offset_y = y;
      modified = true;
    }
    return modified;
  };

  bool modified = false;
  if (is_dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    double dx = ImGui::GetIO().MouseDelta.x / canvas.GetZoom();
    double dy = ImGui::GetIO().MouseDelta.y / canvas.GetZoom();

    // Accumulated in double and only then rounded, so a slow drag at high zoom
    // does not lose sub-pixel motion to the integer offsets.
    double x = static_cast<double>(frame_ptr->offset_x);
    double y = static_cast<double>(frame_ptr->offset_y);

    ApplyDrag(x, drag_acc_x_, dx, true);  // Always snap sprites
    ApplyDrag(y, drag_acc_y_, dy, true);

    modified = is_modified(*frame_ptr, static_cast<int>(x), static_cast<int>(y));
  }

  if (is_dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    is_dragging_ = false;
  }

  UpdateAnimation();

  return modified;
}

void CanvasSprite::ApplyDrag(double& val, double& accumulator, double delta, bool snap) {
  static constexpr double kDragThreshold = 1e-4;
  accumulator += delta;

  if (!snap) {
    val += accumulator;
    accumulator = 0;
    return;
  }

  double target = std::round(val + accumulator);
  double diff = target - val;
  if (std::abs(diff) > kDragThreshold) {
    val += diff;
    accumulator -= diff;
  }
}

void CanvasSprite::UpdateAnimation() {
  if (!is_animating_) return;

  constexpr int kTargetFps = 60;
  constexpr double kTickDuration = 1.0 / kTargetFps;
  animation_timer_ += ImGui::GetIO().DeltaTime;
  while (animation_timer_ >= kTickDuration) {
    animator_.Update(sprite_.frames, sprite_.playback_mode);
    animation_timer_ -= kTickDuration;
  }
}

void CanvasSprite::SetIsAnimating(bool is_animating) {
  if (is_animating && !is_animating_) animator_.Reset();
  is_animating_ = is_animating;
}

}  // namespace zebes
