#include "editor/canvas/canvas_sprite.h"

#include <cmath>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<bool> CanvasSprite::Render(Canvas& canvas, int frame_index, bool input_allowed) {
  if (frame_index < 0 || frame_index >= sprite_.frames.size()) {
    return absl::InvalidArgumentError("Index out of range.");
  }
  if (sprite_.sdl_texture == nullptr) {
    return absl::InternalError("SDL_Texture must not be null!");
  }

  // 1. Determine which frame data to visualize
  SpriteFrame local_frame_copy;
  SpriteFrame* frame_ptr = nullptr;

  if (is_animating_) {
    // Input is strictly disabled during animation to prevent modifying
    // transient interpolated frames.
    input_allowed = false;
    ASSIGN_OR_RETURN(local_frame_copy, animator_.GetCurrentFrame());
    frame_ptr = &local_frame_copy;
  } else {
    frame_ptr = &sprite_.frames[frame_index];
  }

  // 2. Coordinate Conversion (World -> Screen)
  ImVec2 p1 = canvas.WorldToScreen({(double)frame_ptr->offset_x, (double)frame_ptr->offset_y});
  ImVec2 p2 = canvas.WorldToScreen({(double)frame_ptr->offset_x + frame_ptr->render_w,
                                    (double)frame_ptr->offset_y + frame_ptr->render_h});

  ImDrawList* draw_list = canvas.GetDrawList();

  // 3. Render Texture
  int tex_w = 0, tex_h = 0;
  SDL_QueryTexture((SDL_Texture*)sprite_.sdl_texture, nullptr, nullptr, &tex_w, &tex_h);

  if (tex_w > 0 && tex_h > 0) {
    ImVec2 uv0((float)frame_ptr->texture_x / tex_w, (float)frame_ptr->texture_y / tex_h);
    ImVec2 uv1((float)(frame_ptr->texture_x + frame_ptr->texture_w) / tex_w,
               (float)(frame_ptr->texture_y + frame_ptr->texture_h) / tex_h);

    draw_list->AddImage((ImTextureID)sprite_.sdl_texture, p1, p2, uv0, uv1);
  }

  // 4. Draw Selection Box
  // (Green if selected/editing, maybe gray if just viewing)
  draw_list->AddRect(p1, p2, IM_COL32(100, 200, 100, 255));

  // If input is not allowed (e.g., hovering another window or animating), we skip interaction
  if (!input_allowed) {
    is_dragging_ = false;
    return false;
  }

  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered =
      mouse_pos.x >= p1.x && mouse_pos.x <= p2.x && mouse_pos.y >= p1.y && mouse_pos.y <= p2.y;

  // Start Drag
  if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    is_dragging_ = true;
    drag_acc_x_ = 0;
    drag_acc_y_ = 0;
  }

  // Check if value actually changed to return 'modified'
  auto is_modified = [this](SpriteFrame& frame, int x, int y) {
    // Apply changes if we aren't animating (modifying the source frame)
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
  // During Drag
  if (is_dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    // Convert mouse delta to world space delta
    double dx = ImGui::GetIO().MouseDelta.x / canvas.GetZoom();
    double dy = ImGui::GetIO().MouseDelta.y / canvas.GetZoom();

    // Use a temp copy to calculate new positions using double precision
    double x = (double)frame_ptr->offset_x;
    double y = (double)frame_ptr->offset_y;

    ApplyDrag(x, drag_acc_x_, dx, true);  // Always snap sprites
    ApplyDrag(y, drag_acc_y_, dy, true);

    modified = is_modified(*frame_ptr, static_cast<int>(x), static_cast<int>(y));
  }

  // End Drag
  if (is_dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    is_dragging_ = false;
  }

  // Finally update the animation ticks
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
    animator_.Update();
    animation_timer_ -= kTickDuration;
  }
}

void CanvasSprite::SetIsAnimating(bool is_animating) { is_animating_ = is_animating; }

}  // namespace zebes