#include "editor/canvas/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "absl/log/log.h"
#include "editor/gui_interface.h"

namespace zebes {

namespace {

// Hard zoom limits applied both at the start of each frame (ClampCamera) and
// immediately after the user scrolls (HandleInput). Keeping them in one place
// ensures the two sites never drift out of sync.
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 10.0;

}  // namespace

Canvas::Canvas(Options options)
    : gui_(options.gui), snap_grid_(options.snap_grid), grid_size_(options.grid_size) {}

void Canvas::SetWorldBounds(Vec min, Vec max) {
  world_min_ = min;
  world_max_ = max;
}

float Canvas::GetZoom() const { return camera_ ? camera_->zoom : 1.0f; }

void Canvas::Begin(const char* id, const ImVec2& size, Camera& camera) {
  camera_ = &camera;

  // Prevent zero zoom
  if (camera_->zoom <= 0.001f) camera_->zoom = 1.0f;

  // Camera clamping depends on the visible world dimensions, so install the
  // current viewport before clamping. On the first frame these values are
  // otherwise zero and the camera can be positioned outside the actual view.
  camera_->viewport_width = size.x;
  camera_->viewport_height = size.y;

  // Enforce bounds immediately before rendering anything.
  ClampCamera();

  gui_->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  gui_->BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
  gui_->PopStyleVar();

  p0_ = gui_->GetCursorScreenPos();
  draw_list_ = gui_->GetWindowDrawList();

  // Draw Background
  if (draw_list_ != nullptr) {
    ImU32 bg_color = IM_COL32(50, 50, 50, 255);
    draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + size.x, p0_.y + size.y), bg_color);
  }

}

void Canvas::End() {
  gui_->EndChild();
  camera_ = nullptr;
}

void Canvas::HandleInput() {
  if (!camera_) return;

  ImVec2 size = gui_->GetContentRegionAvail();

  // Invisible button covers the canvas to capture mouse interaction
  gui_->SetCursorPos(ImVec2(0, 0));
  gui_->InvisibleButton("##CanvasInput", size,
                        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                            ImGuiButtonFlags_MouseButtonMiddle);

  bool is_hovered = gui_->IsItemHovered();

  // The canvas consumes vertical wheel input for zoom. Claim it for the
  // invisible canvas item so ImGui does not also scroll a parent window.
  if (is_hovered) {
    gui_->SetItemKeyOwner(ImGuiKey_MouseWheelY);
  }

  // 1. Handle Zoom (Mouse Wheel)
  if (is_hovered && gui_->GetIO().MouseWheel != 0.0f) {
    float zoom_speed = 0.1f;
    camera_->zoom += gui_->GetIO().MouseWheel * zoom_speed;
    // Clamp immediately so DrawGrid never sees zoom <= 0, regardless of call order.
    camera_->zoom = std::clamp(camera_->zoom, kMinZoom, kMaxZoom);
  }

  // 2. Handle Panning (Keyboard: WASD / Arrows)
  if (gui_->IsWindowFocused()) {
    float base_speed = 500.0f;  // Pixels per second
    float dt = gui_->GetIO().DeltaTime;

    // Scale speed by zoom so visual speed remains constant
    float move_step = (base_speed * dt) / camera_->zoom;

    if (gui_->IsKeyDown(ImGuiKey_UpArrow) || gui_->IsKeyDown(ImGuiKey_W)) {
      LOG(INFO) << __func__ << ": up";
      camera_->position.y -= move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_DownArrow) || gui_->IsKeyDown(ImGuiKey_S)) {
      LOG(INFO) << __func__ << ": down";
      camera_->position.y += move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_LeftArrow) || gui_->IsKeyDown(ImGuiKey_A)) {
      LOG(INFO) << __func__ << ": left";
      camera_->position.x -= move_step;
    }
    if (gui_->IsKeyDown(ImGuiKey_RightArrow) || gui_->IsKeyDown(ImGuiKey_D)) {
      LOG(INFO) << __func__ << ": right";
      camera_->position.x += move_step;
    }
  }

  // Input changes apply to this frame's rendering. Keep the updated camera
  // within the same bounds enforced at Begin().
  ClampCamera();
}

ImVec2 Canvas::WorldToScreen(const Vec& v) const {
  if (!camera_) return {0, 0};
  Vec local = camera_->WorldToScreen(v);
  return ImVec2(p0_.x + (float)local.x, p0_.y + (float)local.y);
}

Vec Canvas::ScreenToWorld(const ImVec2& p) const {
  if (!camera_) return {0, 0};
  Vec local_screen;
  local_screen.x = p.x - p0_.x;
  local_screen.y = p.y - p0_.y;
  return camera_->ScreenToWorld(local_screen);
}

Vec Canvas::SnapToGrid(Vec world_pos) const {
  if (!snap_grid_) return world_pos;
  return {std::round(world_pos.x / grid_size_) * grid_size_,
          std::round(world_pos.y / grid_size_) * grid_size_};
}

void Canvas::DrawGrid() {
  if (!draw_list_ || !camera_) return;

  ImVec2 canvas_sz = gui_->GetWindowSize();
  const float ruler_thickness = 20.0f;

  // 1. Calculate Grid Step based on Zoom
  // Using the new configurable grid_size.
  double world_step = grid_size_;
  float step = grid_size_ * camera_->zoom;

  // 2. Determine visible world range (top-left)
  Vec tl_world = ScreenToWorld(p0_);

  // Snap start positions to the nearest grid step
  double start_x = floor(tl_world.x / world_step) * world_step;
  double start_y = floor(tl_world.y / world_step) * world_step;

  // 3. Draw Axis/Origin Crosshair (Thicker lines at 0,0)
  Vec origin_screen_local = camera_->WorldToScreen({0, 0});
  ImVec2 origin_screen = ImVec2(p0_.x + origin_screen_local.x, p0_.y + origin_screen_local.y);
  ImU32 axis_color = IM_COL32(100, 100, 100, 255);

  // X Axis (Horizontal line where Y=0)
  if (origin_screen.y > p0_.y && origin_screen.y < p0_.y + canvas_sz.y) {
    draw_list_->AddLine(ImVec2(p0_.x, origin_screen.y),
                        ImVec2(p0_.x + canvas_sz.x, origin_screen.y), axis_color, 2.0f);
  }
  // Y Axis (Vertical line where X=0)
  if (origin_screen.x > p0_.x && origin_screen.x < p0_.x + canvas_sz.x) {
    draw_list_->AddLine(ImVec2(origin_screen.x, p0_.y),
                        ImVec2(origin_screen.x, p0_.y + canvas_sz.y), axis_color, 2.0f);
  }

  // 4. Draw Ruler Backgrounds
  ImU32 ruler_bg_color = IM_COL32(40, 40, 40, 255);
  // Top Ruler (X)
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + canvas_sz.x, p0_.y + ruler_thickness),
                            ruler_bg_color);
  // Left Ruler (Y)
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + ruler_thickness, p0_.y + canvas_sz.y),
                            ruler_bg_color);

  // 5. Draw Ticks & Grid Lines using Member Helper
  DrawRulerAndGrid(start_x, world_step, canvas_sz.x, true);
  DrawRulerAndGrid(start_y, world_step, canvas_sz.y, false);

  // 6. Mouse Indicators
  if (gui_->IsWindowHovered()) {
    ImVec2 mouse_pos = gui_->GetMousePos();
    ImU32 indicator_color = IM_COL32(255, 50, 50, 255);

    // Indicator on X Ruler
    draw_list_->AddLine(ImVec2(mouse_pos.x, p0_.y), ImVec2(mouse_pos.x, p0_.y + ruler_thickness),
                        indicator_color, 2.0f);
    // Indicator on Y Ruler
    draw_list_->AddLine(ImVec2(p0_.x, mouse_pos.y), ImVec2(p0_.x + ruler_thickness, mouse_pos.y),
                        indicator_color, 2.0f);
  }
}

void Canvas::DrawRulerAndGrid(double start_val, double step, double max_dim, bool is_x_axis) {
  // A zero or negative step would cause an infinite loop; this guards against
  // broken state from bad zoom values or an unset grid_size.
  if (step <= 0) return;

  const float ruler_thickness = 20.0f;
  ImU32 ruler_tick_color = IM_COL32(180, 180, 180, 255);
  ImU32 grid_color = IM_COL32(60, 60, 60, 100);

  // Determine main axis start and cross axis start from class members
  float p0_main = is_x_axis ? p0_.x : p0_.y;
  float p0_cross = is_x_axis ? p0_.y : p0_.x;

  for (double val = start_val;; val += step) {
    // We only need the screen coordinate for the current axis.
    // Construct a temporary world vec to pass to the transform.
    Vec world_pt = is_x_axis ? Vec{val, 0} : Vec{0, val};
    ImVec2 screen_pos = WorldToScreen(world_pt);

    float pos_main = is_x_axis ? screen_pos.x : screen_pos.y;

    // Check bounds
    if (pos_main > p0_main + max_dim) break;
    if (pos_main < p0_main) continue;

    // 1. Draw Ruler Tick
    ImVec2 tick_start, tick_end;
    if (is_x_axis) {
      tick_start = ImVec2(pos_main, p0_cross);
      tick_end = ImVec2(pos_main, p0_cross + ruler_thickness * 0.5f);
    } else {
      tick_start = ImVec2(p0_cross, pos_main);
      tick_end = ImVec2(p0_cross + ruler_thickness * 0.5f, pos_main);
    }
    draw_list_->AddLine(tick_start, tick_end, ruler_tick_color);

    // 2. Draw Grid Line
    ImVec2 grid_start = tick_start;
    ImVec2 grid_end;
    if (is_x_axis) {
      grid_start.y += ruler_thickness;
      grid_end = ImVec2(pos_main, p0_cross + 20000.0f);
    } else {
      grid_start.x += ruler_thickness;
      grid_end = ImVec2(p0_cross + 20000.0f, pos_main);
    }
    draw_list_->AddLine(grid_start, grid_end, grid_color);

    // 3. Draw Label
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", val);
    draw_list_->AddText(ImVec2(tick_start.x + 3, tick_start.y + 2), ruler_tick_color, buf);
  }
}

void Canvas::ClampCamera() {
  if (!camera_) return;

  // 1. Apply Hard Limits (Sanity Check)
  // We always want reasonable limits regardless of world size
  camera_->zoom = std::clamp(camera_->zoom, kMinZoom, kMaxZoom);

  if (!world_min_.has_value() || !world_max_.has_value()) return;

  // 2. Apply World Boundary Constraints
  double world_w = world_max_->x - world_min_->x;
  double world_h = world_max_->y - world_min_->y;

  // A. CLAMP ZOOM
  // Ensure the viewport is never larger than the world.
  if (world_w > 1.0 && world_h > 1.0) {
    // Calculate the minimum zoom required to fill the screen
    float min_zoom_x = (float)(camera_->viewport_width / world_w);
    float min_zoom_y = (float)(camera_->viewport_height / world_h);

    // We must satisfy the stricter of the two constraints
    float min_required_zoom = std::max(min_zoom_x, min_zoom_y);

    // If current zoom is too far out, snap it back in
    if (camera_->zoom < min_required_zoom) {
      camera_->zoom = min_required_zoom;
    }
  }

  // B. CLAMP POSITION
  // Now that zoom is valid/finalized, calculate the view radius
  double view_half_w = (camera_->viewport_width / 2.0) / camera_->zoom;
  double view_half_h = (camera_->viewport_height / 2.0) / camera_->zoom;

  // Define the safe box for the center point
  double min_x = world_min_->x + view_half_w;
  double max_x = world_max_->x - view_half_w;
  double min_y = world_min_->y + view_half_h;
  double max_y = world_max_->y - view_half_h;

  // Apply clamps (handling the case where min > max slightly gracefully)
  if (camera_->position.x < min_x) camera_->position.x = min_x;
  if (camera_->position.x > max_x) camera_->position.x = max_x;

  if (camera_->position.y < min_y) camera_->position.y = min_y;
  if (camera_->position.y > max_y) camera_->position.y = max_y;
}

}  // namespace zebes
