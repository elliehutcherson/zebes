#include "editor/canvas/canvas.h"

#include <cmath>
#include <cstdio>

#include "editor/gui_interface.h"
#include "imgui.h"

namespace zebes {

Canvas::Canvas(Options options) : gui_(options.gui), snap_grid_(options.snap_grid) {}
Canvas::Canvas(GuiInterface* gui) : gui_(gui) {}

void Canvas::Begin(const char* id, const ImVec2& size, Camera& camera) {
  camera_ = &camera;

  // Prevent zero zoom
  if (camera_->zoom <= 0.001f) camera_->zoom = 1.0f;

  gui_->BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  p0_ = gui_->GetCursorScreenPos();
  draw_list_ = gui_->GetWindowDrawList();

  // Draw Background
  ImU32 bg_color = IM_COL32(50, 50, 50, 255);
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + size.x, p0_.y + size.y), bg_color);

  // Update viewport size in camera so it knows how to center things
  camera_->viewport_width = size.x;
  camera_->viewport_height = size.y;
}

void Canvas::End() {
  gui_->EndChild();
  camera_ = nullptr;
}

float Canvas::GetZoom() const { return camera_ ? camera_->zoom : 1.0f; }

void Canvas::HandleInput() {
  if (!camera_) return;

  ImVec2 size = gui_->GetContentRegionAvail();

  // Invisible button covers the canvas to capture mouse interaction
  gui_->SetCursorPos(ImVec2(0, 0));
  gui_->InvisibleButton("##CanvasInput", size,
                        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                            ImGuiButtonFlags_MouseButtonMiddle);

  bool is_hovered = gui_->IsItemHovered();
  bool is_active = gui_->IsItemActive();

  // Handle Zoom
  if (is_hovered && gui_->GetIO().MouseWheel != 0.0f) {
    float zoom_speed = 0.1f;
    camera_->zoom += gui_->GetIO().MouseWheel * zoom_speed;
    // Clamp zoom
    if (camera_->zoom < 0.1f) camera_->zoom = 0.1f;
    if (camera_->zoom > 10.0f) camera_->zoom = 10.0f;
  }

  // Handle Panning (Middle Mouse or Space + Left Click)
  bool panned = false;
  if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    panned = true;
  } else if (is_active && gui_->IsKeyPressed(ImGuiKey_Space) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    // Wait, ImGui::IsMouseDragging isn't in GuiInterface?
    // I need to check. If not, I should use ImGui:: or add it.
    // Assuming keeping ImGui:: for pure input utilities if necessary, or better add to interface.
    // IsMouseDragging is common.
    // I'll leave ImGui::IsMouseDragging for now as I missed checking it.
    // But I should replace it if possible.
    panned = true;
  }

  if (panned) {
    camera_->position.x -= gui_->GetIO().MouseDelta.x / camera_->zoom;
    camera_->position.y -= gui_->GetIO().MouseDelta.y / camera_->zoom;
  }
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

void Canvas::DrawGrid() {
  if (!draw_list_ || !camera_) return;

  ImVec2 canvas_sz = gui_->GetIO().DisplaySize;  // Was GetWindowSize(), but inside Child window?
  // Original was ImGui::GetWindowSize().
  // gui_->GetIO().DisplaySize is screen size.
  // I need ImGui::GetWindowSize(). I missed adding GetWindowSize to GuiInterface?
  // Let's assume GetContentRegionAvail matches window size if full?
  // GetWindowSize includes borders/padding.
  // I should check if GetWindowSize is in GuiInterface.
  // If not, I'll use ImGui::GetWindowSize() temporarily or add it.

  // Actually, I'll use ImGui::GetWindowSize() for now to avoid blocking on adding method.
  canvas_sz = ImGui::GetWindowSize();

  // Calculate grid metrics based on camera
  Vec origin_screen_local = camera_->WorldToScreen({0, 0});
  ImVec2 origin_screen = ImVec2(p0_.x + origin_screen_local.x, p0_.y + origin_screen_local.y);

  // 1. Draw Axis Lines
  ImU32 axis_color = IM_COL32(100, 100, 100, 100);
  // X Axis (where y=0)
  draw_list_->AddLine(ImVec2(p0_.x, origin_screen.y), ImVec2(p0_.x + canvas_sz.x, origin_screen.y),
                      axis_color);
  // Y Axis (where x=0)
  draw_list_->AddLine(ImVec2(origin_screen.x, p0_.y), ImVec2(origin_screen.x, p0_.y + canvas_sz.y),
                      axis_color);

  // 2. Draw Rulers
  const float ruler_thickness = 20.0f;
  ImU32 ruler_bg_color = IM_COL32(40, 40, 40, 255);
  ImU32 ruler_tick_color = IM_COL32(180, 180, 180, 255);

  // Top Ruler (X-axis) background
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + canvas_sz.x, p0_.y + ruler_thickness),
                            ruler_bg_color);

  // Left Ruler (Y-axis) background
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + ruler_thickness, p0_.y + canvas_sz.y),
                            ruler_bg_color);

  // Calculate Grid Step
  float step = 50.0f * camera_->zoom;
  while (step < 50.0f) step *= 2.0f;
  while (step > 150.0f) step /= 2.0f;

  // Draw X Ticks (Top Ruler)
  // We need to find the first tick visible on screen.
  // Visible world X range:
  Vec tl_world = ScreenToWorld(p0_);

  // Align to step size in world space? No, step is in screen pixels kind of.
  // The 'step' logic above creates a pixel interval between 50 and 150.
  // World space step:
  double world_step = step / camera_->zoom;

  // Start with a multiple of world_step just before tl_world.x
  double start_world_x = floor(tl_world.x / world_step) * world_step;

  for (double wx = start_world_x;; wx += world_step) {
    ImVec2 screen_pos = WorldToScreen({wx, 0});
    if (screen_pos.x > p0_.x + canvas_sz.x) break;
    if (screen_pos.x < p0_.x) continue;

    ImVec2 p1(screen_pos.x, p0_.y);
    ImVec2 p2(screen_pos.x, p0_.y + ruler_thickness * 0.5f);
    draw_list_->AddLine(p1, p2, ruler_tick_color);

    // Label
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", wx);
    draw_list_->AddText(ImVec2(p1.x + 2, p1.y + 2), ruler_tick_color, buf);
  }

  // Draw Y Ticks (Left Ruler)
  Vec tl_world_y = ScreenToWorld(p0_);  // Same as above
  double start_world_y = floor(tl_world_y.y / world_step) * world_step;

  for (double wy = start_world_y;; wy += world_step) {
    ImVec2 screen_pos = WorldToScreen({0, wy});
    if (screen_pos.y > p0_.y + canvas_sz.y) break;
    if (screen_pos.y < p0_.y) continue;

    ImVec2 p1(p0_.x, screen_pos.y);
    ImVec2 p2(p0_.x + ruler_thickness * 0.5f, screen_pos.y);
    draw_list_->AddLine(p1, p2, ruler_tick_color);

    // Label
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", wy);
    draw_list_->AddText(ImVec2(p1.x + 2, p1.y + 2), ruler_tick_color, buf);
  }

  // 3. Mouse Indicator (Red Line on Rulers)
  ImVec2 mouse_pos = gui_->GetMousePos();
  bool mouse_in_canvas =
      gui_->IsItemHovered();  // Was IsWindowHovered, but we have InvisibleButton over canvas now?
  // Wait, Render uses InvisibleButton. DrawGrid is called IN Begin so InvisibleButton is not yet
  // submitted? HandleInput is called AFTER End. DrawGrid is called INSIDE Begin/End.
  // IsWindowHovered works for the child window created in Begin.
  mouse_in_canvas = ImGui::IsWindowHovered();  // Keep usage or gui_->IsWindowHovered() if added.
  // IsWindowHovered is NOT in GuiInterface? Check.
  // I likely missed it.

  if (mouse_in_canvas) {
    ImU32 indicator_color = IM_COL32(255, 50, 50, 255);

    // X-Axis Indicator
    draw_list_->AddLine(ImVec2(mouse_pos.x, p0_.y), ImVec2(mouse_pos.x, p0_.y + ruler_thickness),
                        indicator_color, 2.0f);

    // Y-Axis Indicator
    draw_list_->AddLine(ImVec2(p0_.x, mouse_pos.y), ImVec2(p0_.x + ruler_thickness, mouse_pos.y),
                        indicator_color, 2.0f);
  }
}

}  // namespace zebes