#include "editor/canvas/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "editor/gui_interface.h"

namespace zebes {

namespace {

// World units per second at zoom 1, deliberately looser than the gameplay
// camera.
constexpr double kEditorPanSpeed = 500.0;
constexpr double kEditorWheelZoomStep = 0.1;
constexpr float kRulerWidth = 56.0f;
constexpr float kRulerHeight = 20.0f;

}  // namespace

Canvas::Canvas(Options options)
    : gui_(options.gui),
      snap_grid_(options.snap_grid),
      grid_size_(options.grid_size),
      logical_viewport_(options.logical_viewport),
      show_rulers_(options.show_rulers) {}

void Canvas::SetWorldBounds(Vec min, Vec max) {
  world_min_ = min;
  world_max_ = max;
}

float Canvas::GetZoom() const { return camera_ ? camera_->zoom : 1.0f; }

void Canvas::Begin(const char* id, const ImVec2& size, Camera& camera) {
  camera_ = &camera;
  canvas_size_ = {std::max(0.0f, size.x), std::max(0.0f, size.y)};
  const float ruler_width = show_rulers_ ? kRulerWidth : 0.0f;
  const float ruler_height = show_rulers_ ? kRulerHeight : 0.0f;
  const ImVec2 available_content{
      std::max(0.0f, canvas_size_.x - ruler_width),
      std::max(0.0f, canvas_size_.y - ruler_height),
  };

  display_scale_ = 1.0;
  content_size_ = available_content;
  content_local_origin_ = {ruler_width, ruler_height};
  if (logical_viewport_.has_value() && logical_viewport_->IsValid() && available_content.x > 0.0f &&
      available_content.y > 0.0f) {
    display_scale_ = std::min(available_content.x / logical_viewport_->width,
                              available_content.y / logical_viewport_->height);
    content_size_ = {
        static_cast<float>(logical_viewport_->width * display_scale_),
        static_cast<float>(logical_viewport_->height * display_scale_),
    };
    content_local_origin_.x += (available_content.x - content_size_.x) / 2.0f;
    content_local_origin_.y += (available_content.y - content_size_.y) / 2.0f;
  }

  // Reset rather than clamped: every transform divides by zoom, and there is no
  // meaningful view here to preserve.
  if (camera_->zoom <= 0.001f) camera_->zoom = 1.0f;

  // Before ClampCamera, which needs to know how much world is visible. On the
  // first frame these are still zero, and the camera would settle outside the
  // view it is about to draw.
  if (logical_viewport_.has_value() && logical_viewport_->IsValid()) {
    camera_->viewport_width = logical_viewport_->width;
    camera_->viewport_height = logical_viewport_->height;
  } else {
    camera_->viewport_width = content_size_.x;
    camera_->viewport_height = content_size_.y;
  }

  ClampCamera();

  gui_->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  gui_->BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
  gui_->PopStyleVar();

  canvas_origin_ = gui_->GetCursorScreenPos();
  content_origin_ = {
      canvas_origin_.x + content_local_origin_.x,
      canvas_origin_.y + content_local_origin_.y,
  };
  draw_list_ = gui_->GetWindowDrawList();

  if (draw_list_ != nullptr) {
    ImU32 bg_color = IM_COL32(50, 50, 50, 255);
    draw_list_->AddRectFilled(
        canvas_origin_,
        ImVec2(canvas_origin_.x + canvas_size_.x, canvas_origin_.y + canvas_size_.y), bg_color);
    if (logical_viewport_.has_value() && logical_viewport_->IsValid()) {
      draw_list_->AddRectFilled(
          content_origin_,
          ImVec2(content_origin_.x + content_size_.x, content_origin_.y + content_size_.y),
          IM_COL32(20, 20, 24, 255));
      draw_list_->AddRect(
          content_origin_,
          ImVec2(content_origin_.x + content_size_.x, content_origin_.y + content_size_.y),
          IM_COL32(100, 100, 110, 255));
    }
  }
}

void Canvas::End() {
  gui_->EndChild();
  camera_ = nullptr;
}

void Canvas::HandleInput() {
  if (!camera_) return;
  if (content_size_.x <= 0.0f || content_size_.y <= 0.0f) return;

  // An invisible button is what claims the mouse for the canvas; without an
  // item there the parent window takes it.
  gui_->SetCursorPos(content_local_origin_);
  gui_->InvisibleButton("##CanvasInput", content_size_,
                        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                            ImGuiButtonFlags_MouseButtonMiddle);

  bool is_hovered = gui_->IsItemHovered();

  // The canvas consumes vertical wheel input for zoom. Claim it for the
  // invisible canvas item so ImGui does not also scroll a parent window.
  if (is_hovered) {
    gui_->SetItemKeyOwner(ImGuiKey_MouseWheelY);
  }

  if (is_hovered && gui_->GetIO().MouseWheel != 0.0f) {
    camera_->zoom += gui_->GetIO().MouseWheel * kEditorWheelZoomStep;
    camera_->zoom = NavigationZoomRange().Clamp(camera_->zoom);
  }

  if (gui_->IsWindowFocused()) {
    float dt = gui_->GetIO().DeltaTime;

    // Dividing by zoom holds the pan speed constant on screen.
    float move_step = (kEditorPanSpeed * dt) / (camera_->zoom * display_scale_);

    if (gui_->IsKeyDown(ImGuiKey_UpArrow) || gui_->IsKeyDown(ImGuiKey_W)) {
      camera_->position.y -= move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_DownArrow) || gui_->IsKeyDown(ImGuiKey_S)) {
      camera_->position.y += move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_LeftArrow) || gui_->IsKeyDown(ImGuiKey_A)) {
      camera_->position.x -= move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_RightArrow) || gui_->IsKeyDown(ImGuiKey_D)) {
      camera_->position.x += move_step;
    }
  }

  HandleMousePan(is_hovered);

  // This frame's input is drawn this frame, so re-clamp before anything reads
  // the camera back.
  ClampCamera();
}

void Canvas::HandleMousePan(bool is_hovered) {
  if (!gui_->IsMouseDragging(ImGuiMouseButton_Middle)) {
    pan_cursor_.reset();
    return;
  }

  const ImVec2 cursor = gui_->GetMousePos();

  // A drag only claims this canvas if it started over it. Without that check
  // every canvas on screen would pan together, since drag state is global.
  if (!pan_cursor_.has_value()) {
    if (!is_hovered) return;
    pan_cursor_ = cursor;
    return;
  }

  // Dividing by zoom converts a screen-space delta into world units, which is
  // what keeps the grabbed point pinned to the cursor as the view scales.
  camera_->position.x -= (cursor.x - pan_cursor_->x) / (camera_->zoom * display_scale_);
  camera_->position.y -= (cursor.y - pan_cursor_->y) / (camera_->zoom * display_scale_);
  pan_cursor_ = cursor;
}

ImVec2 Canvas::WorldToScreen(const Vec& v) const {
  if (!camera_) return {0, 0};
  Vec local = camera_->WorldToScreen(v);
  return ImVec2(content_origin_.x + static_cast<float>(local.x * display_scale_),
                content_origin_.y + static_cast<float>(local.y * display_scale_));
}

Vec Canvas::ScreenToWorld(const ImVec2& p) const {
  if (!camera_) return {0, 0};
  Vec local_screen;
  local_screen.x = (p.x - content_origin_.x) / display_scale_;
  local_screen.y = (p.y - content_origin_.y) / display_scale_;
  return camera_->ScreenToWorld(local_screen);
}

Vec Canvas::SnapToGrid(Vec world_pos) const {
  if (!snap_grid_) return world_pos;
  return {std::round(world_pos.x / grid_size_) * grid_size_,
          std::round(world_pos.y / grid_size_) * grid_size_};
}

void Canvas::DrawGrid() {
  if (!draw_list_ || !camera_ || content_size_.x <= 0.0f || content_size_.y <= 0.0f) return;

  double world_step = grid_size_;

  // Snapped to the last grid line before the top-left corner, so lines stay on
  // world multiples as the view scrolls instead of sliding with it.
  Vec tl_world = ScreenToWorld(content_origin_);
  double start_x = floor(tl_world.x / world_step) * world_step;
  double start_y = floor(tl_world.y / world_step) * world_step;

  ImVec2 origin_screen = WorldToScreen({0, 0});
  ImU32 axis_color = IM_COL32(100, 100, 100, 255);

  if (origin_screen.y > content_origin_.y &&
      origin_screen.y < content_origin_.y + content_size_.y) {
    draw_list_->AddLine(ImVec2(content_origin_.x, origin_screen.y),
                        ImVec2(content_origin_.x + content_size_.x, origin_screen.y), axis_color,
                        2.0f);
  }
  if (origin_screen.x > content_origin_.x &&
      origin_screen.x < content_origin_.x + content_size_.x) {
    draw_list_->AddLine(ImVec2(origin_screen.x, content_origin_.y),
                        ImVec2(origin_screen.x, content_origin_.y + content_size_.y), axis_color,
                        2.0f);
  }

  if (!show_rulers_) return;

  ImU32 ruler_bg_color = IM_COL32(40, 40, 40, 255);
  draw_list_->AddRectFilled(
      canvas_origin_, ImVec2(canvas_origin_.x + canvas_size_.x, content_origin_.y), ruler_bg_color);
  draw_list_->AddRectFilled(
      canvas_origin_, ImVec2(content_origin_.x, canvas_origin_.y + canvas_size_.y), ruler_bg_color);

  DrawRulerAndGrid(start_x, world_step, content_size_.x, true);
  DrawRulerAndGrid(start_y, world_step, content_size_.y, false);

  // Marks the cursor on both rulers, so a coordinate can be read off directly.
  if (gui_->IsWindowHovered()) {
    ImVec2 mouse_pos = gui_->GetMousePos();
    ImU32 indicator_color = IM_COL32(255, 50, 50, 255);

    if (mouse_pos.x >= content_origin_.x && mouse_pos.x <= content_origin_.x + content_size_.x) {
      draw_list_->AddLine(ImVec2(mouse_pos.x, canvas_origin_.y),
                          ImVec2(mouse_pos.x, content_origin_.y), indicator_color, 2.0f);
    }
    if (mouse_pos.y >= content_origin_.y && mouse_pos.y <= content_origin_.y + content_size_.y) {
      draw_list_->AddLine(ImVec2(canvas_origin_.x, mouse_pos.y),
                          ImVec2(content_origin_.x, mouse_pos.y), indicator_color, 2.0f);
    }
  }
}

void Canvas::DrawRulerAndGrid(double start_val, double step, double max_dim, bool is_x_axis) {
  // A zero or negative step would cause an infinite loop; this guards against
  // broken state from bad zoom values or an unset grid_size.
  if (step <= 0) return;

  ImU32 ruler_tick_color = IM_COL32(180, 180, 180, 255);
  ImU32 grid_color = IM_COL32(60, 60, 60, 100);

  const float content_main = is_x_axis ? content_origin_.x : content_origin_.y;
  const float ruler_cross = is_x_axis ? canvas_origin_.y : canvas_origin_.x;

  for (double val = start_val;; val += step) {
    // Only one axis of the result is used; the other coordinate is filler.
    Vec world_pt = is_x_axis ? Vec{val, 0} : Vec{0, val};
    ImVec2 screen_pos = WorldToScreen(world_pt);

    float pos_main = is_x_axis ? screen_pos.x : screen_pos.y;

    // Lines are generated in increasing world order, so the first one past the
    // far edge ends the loop.
    if (pos_main > content_main + max_dim) break;
    if (pos_main < content_main) continue;

    ImVec2 tick_start, tick_end;
    if (is_x_axis) {
      tick_start = ImVec2(pos_main, ruler_cross);
      tick_end = ImVec2(pos_main, ruler_cross + kRulerHeight * 0.5f);
    } else {
      tick_start = ImVec2(ruler_cross, pos_main);
      tick_end = ImVec2(ruler_cross + kRulerWidth * 0.25f, pos_main);
    }
    draw_list_->AddLine(tick_start, tick_end, ruler_tick_color);

    ImVec2 grid_start;
    ImVec2 grid_end;
    if (is_x_axis) {
      grid_start = ImVec2(pos_main, content_origin_.y);
      grid_end = ImVec2(pos_main, content_origin_.y + content_size_.y);
    } else {
      grid_start = ImVec2(content_origin_.x, pos_main);
      grid_end = ImVec2(content_origin_.x + content_size_.x, pos_main);
    }
    draw_list_->AddLine(grid_start, grid_end, grid_color);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", val);
    draw_list_->AddText(ImVec2(tick_start.x + 3, tick_start.y + 2), ruler_tick_color, buf);
  }
}

void Canvas::ClampCamera() {
  if (!camera_) return;

  // Applied before the early return, so an unbounded canvas still cannot reach
  // a degenerate zoom.
  camera_->zoom = NavigationZoomRange().Clamp(camera_->zoom);

  if (!world_min_.has_value() || !world_max_.has_value()) return;

  double world_w = world_max_->x - world_min_->x;
  double world_h = world_max_->y - world_min_->y;

  // Zoom out only until the world fills the viewport. Past that the position
  // clamp below has no valid box, since the view is wider than the world. A
  // world too small to measure is skipped rather than forced to an absurd zoom.
  if (world_w > 1.0 && world_h > 1.0) {
    float min_zoom_x = static_cast<float>(camera_->viewport_width / world_w);
    float min_zoom_y = static_cast<float>(camera_->viewport_height / world_h);
    float min_required_zoom = std::max(min_zoom_x, min_zoom_y);

    if (camera_->zoom < min_required_zoom) {
      camera_->zoom = min_required_zoom;
    }
  }

  // The box where a half-viewport in each direction still lands inside the
  // world. Depends on zoom, which is why it is computed after the clamp above.
  double view_half_w = (camera_->viewport_width / 2.0) / camera_->zoom;
  double view_half_h = (camera_->viewport_height / 2.0) / camera_->zoom;

  double min_x = world_min_->x + view_half_w;
  double max_x = world_max_->x - view_half_w;
  double min_y = world_min_->y + view_half_h;
  double max_y = world_max_->y - view_half_h;

  // Ordered so max wins if the box has inverted, which a world smaller than the
  // guard above allows. The view then rests against one edge rather than
  // jittering between two impossible limits.
  if (camera_->position.x < min_x) camera_->position.x = min_x;
  if (camera_->position.x > max_x) camera_->position.x = max_x;

  if (camera_->position.y < min_y) camera_->position.y = min_y;
  if (camera_->position.y > max_y) camera_->position.y = max_y;
}

}  // namespace zebes
