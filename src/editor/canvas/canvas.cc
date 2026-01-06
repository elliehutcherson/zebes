#include "editor/canvas/canvas.h"

#include <cmath>
#include <cstdio>

#include "imgui.h"

namespace zebes {

void Canvas::Begin(const char* id, const ImVec2& size) {
  // Prevent zero zoom
  if (zoom_ <= 0.001f) zoom_ = 1.0f;

  ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

  p0_ = ImGui::GetCursorScreenPos();
  draw_list_ = ImGui::GetWindowDrawList();

  // Draw Background
  ImU32 bg_color = IM_COL32(50, 50, 50, 255);
  draw_list_->AddRectFilled(p0_, ImVec2(p0_.x + size.x, p0_.y + size.y), bg_color);

  // Calculate Origin (Center of screen + Offset)
  ImVec2 center_offset(size.x * 0.5f, size.y * 0.5f);
  origin_ = ImVec2(p0_.x + offset_.x + center_offset.x, p0_.y + offset_.y + center_offset.y);
}

void Canvas::End() { ImGui::EndChild(); }

void Canvas::Reset() {
  zoom_ = 1.0f;
  offset_ = {0, 0};
}

void Canvas::HandleInput() {
  ImVec2 size = ImGui::GetContentRegionAvail();

  // Invisible button covers the canvas to capture mouse interaction
  ImGui::SetCursorPos(ImVec2(0, 0));
  ImGui::InvisibleButton("##CanvasInput", size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);

  bool is_hovered = ImGui::IsItemHovered();
  bool is_active = ImGui::IsItemActive();

  // Handle Zoom
  if (is_hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    float zoom_speed = 0.1f;
    zoom_ += ImGui::GetIO().MouseWheel * zoom_speed;
    // Clamp zoom
    if (zoom_ < 0.1f) zoom_ = 0.1f;
    if (zoom_ > 10.0f) zoom_ = 10.0f;
  }

  // Handle Panning (Middle Mouse or Space + Left Click)
  bool panned = false;
  if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    panned = true;
  } else if (is_active && ImGui::IsKeyDown(ImGuiKey_Space) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    panned = true;
  }

  if (panned) {
    offset_.x += ImGui::GetIO().MouseDelta.x;
    offset_.y += ImGui::GetIO().MouseDelta.y;
  }
}

ImVec2 Canvas::WorldToScreen(const Vec& v) const {
  return ImVec2(origin_.x + (float)v.x * zoom_, origin_.y + (float)v.y * zoom_);
}

Vec Canvas::ScreenToWorld(const ImVec2& p) const {
  return Vec{(p.x - origin_.x) / zoom_, (p.y - origin_.y) / zoom_};
}

void Canvas::DrawGrid() {
  if (!draw_list_) return;

  ImVec2 canvas_sz = ImGui::GetWindowSize();

  // 1. Draw Axis Lines
  ImU32 axis_color = IM_COL32(100, 100, 100, 100);
  // X Axis
  draw_list_->AddLine(ImVec2(p0_.x, origin_.y), ImVec2(p0_.x + canvas_sz.x, origin_.y), axis_color);
  // Y Axis
  draw_list_->AddLine(ImVec2(origin_.x, p0_.y), ImVec2(origin_.x, p0_.y + canvas_sz.y), axis_color);

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
  float step = 50.0f * zoom_;
  while (step < 50.0f) step *= 2.0f;
  while (step > 150.0f) step /= 2.0f;

  // Draw X Ticks (Top Ruler)
  float start_x = fmod(origin_.x - p0_.x, step);
  if (start_x < 0) start_x += step;

  for (float x = start_x; x < canvas_sz.x; x += step) {
    ImVec2 p1(p0_.x + x, p0_.y);
    ImVec2 p2(p0_.x + x, p0_.y + ruler_thickness * 0.5f);
    draw_list_->AddLine(p1, p2, ruler_tick_color);

    // Label
    double world_x = (x - (origin_.x - p0_.x)) / zoom_;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", world_x);
    draw_list_->AddText(ImVec2(p1.x + 2, p1.y + 2), ruler_tick_color, buf);
  }

  // Draw Y Ticks (Left Ruler)
  float start_y = fmod(origin_.y - p0_.y, step);
  if (start_y < 0) start_y += step;

  for (float y = start_y; y < canvas_sz.y; y += step) {
    ImVec2 p1(p0_.x, p0_.y + y);
    ImVec2 p2(p0_.x + ruler_thickness * 0.5f, p0_.y + y);
    draw_list_->AddLine(p1, p2, ruler_tick_color);

    // Label
    double world_y = (y - (origin_.y - p0_.y)) / zoom_;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", world_y);
    draw_list_->AddText(ImVec2(p1.x + 2, p1.y + 2), ruler_tick_color, buf);
  }

  // 3. Mouse Indicator (Red Line on Rulers)
  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool mouse_in_canvas = ImGui::IsWindowHovered();

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