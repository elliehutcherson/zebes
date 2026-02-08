#include "editor/level_editor/viewport_tab.h"

#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/viewport_tab.h"
#include "imgui.h"

namespace zebes {

ViewportTab::ViewportTab(Api& api, GuiInterface* gui)
    : api_(api),
      gui_(gui),
      canvas_(Canvas::Options{
          .gui = gui,
          .snap_grid = true,
      }) {
  camera_ = Camera{};
}

void ViewportTab::Reset() { camera_ = {}; }

absl::Status ViewportTab::Render(Level& level) {
  // Use a child window to contain the canvas and handle input clipping
  auto child = ScopedChild(gui_, "ViewportCanvas", ImVec2(0, 0), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y -= 25;  // Leave slightly more room for the status bar

  canvas_.SetWorldBounds({0, 0}, {level.width, level.height});
  canvas_.Begin("LevelCanvas", canvas_size, camera_);

  // 1. Render Scene Elements
  RenderParallax(level);
  canvas_.DrawGrid();
  RenderLevelBounds(level);

  // TODO: Render Entities, Tiles, etc.

  // 2. Handle Input (Keyboard Panning)
  canvas_.HandleInput();
  canvas_.End();

  // 3. Status Bar / Debug Overlay
  Vec mouse_world = canvas_.ScreenToWorld(gui_->GetMousePos());
  gui_->Text("Cam: (%.0f, %.0f) | Zoom: %.2f | Mouse World: (%.0f, %.0f)", camera_.position.x,
             camera_.position.y, canvas_.GetZoom(), mouse_world.x, mouse_world.y);

  gui_->SameLine();
  if (gui_->Button("Reset View")) {
    Reset();
  }

  return absl::OkStatus();
}

void ViewportTab::RenderParallax(const Level& level) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  ImVec2 p_min = ImGui::GetWindowPos();
  ImVec2 view_size = ImGui::GetWindowSize();
  ImVec2 p_max = ImVec2(p_min.x + view_size.x, p_min.y + view_size.y);

  // Center offset (half viewport size)
  // This matches the logic in Camera::WorldToScreen so (0,0) is centered
  double center_off_x = view_size.x / 2.0;
  double center_off_y = view_size.y / 2.0;

  for (const ParallaxLayer& layer : level.parallax_layers) {
    if (layer.texture_id.empty()) continue;

    absl::StatusOr<Texture*> texture_or = api_.GetTexture(layer.texture_id);
    if (!texture_or.ok() || *texture_or == nullptr) continue;
    const Texture& texture = *(*texture_or);

    int w = 0, h = 0;
    SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture.sdl_texture), nullptr, nullptr, &w, &h);
    if (w == 0 || h == 0) continue;

    // 1. Calculate Scaled Image Size
    float img_w = w * canvas_.GetZoom();
    float img_h = h * canvas_.GetZoom();

    // 2. Calculate the "Anchor" Position on Screen
    // This is where the top-left of the image would be if it were at World(0,0)
    // Formula: (WorldPos - ParallaxCamPos) * Zoom + CenterOffset

    // How much the camera effectively moves for this layer (0.0 = full movement, 1.0 = none)
    double effective_cam_x = camera_.position.x * (1.0 - layer.scroll_factor.x);
    double effective_cam_y = camera_.position.y * (1.0 - layer.scroll_factor.y);

    double anchor_x = p_min.x + center_off_x - (effective_cam_x * canvas_.GetZoom());
    double anchor_y = p_min.y + center_off_y - (effective_cam_y * canvas_.GetZoom());

    // 3. Determine Start and End Points for Loops
    float start_x, end_x;
    float start_y, end_y;

    // --- X AXIS LOGIC ---
    if (layer.repeat_x) {
      // If repeating, we find the "phase" relative to the anchor
      // We want to start drawing just to the left of the viewport (p_min.x)
      float phase_x = fmod(anchor_x - p_min.x, img_w);
      if (phase_x > 0) phase_x -= img_w;  // Ensure we start behind the left edge

      start_x = p_min.x + phase_x;
      end_x = p_max.x;  // Fill the whole screen
    } else {
      // If NOT repeating, we just draw once at the anchor
      start_x = (float)anchor_x;
      end_x = start_x + img_w;
      // Optimization: Skip if completely off screen
      if (end_x < p_min.x || start_x > p_max.x) continue;
    }

    // --- Y AXIS LOGIC (Assuming repeat_y doesn't exist in struct yet, or defaulting false?) ---
    // If you want Y repetition, copy the logic above.
    // Usually parallax backgrounds don't repeat Y unless it's a pattern.
    // For now, let's assume NO Y Repeat (Standard for side-scrollers) unless you add the flag.

    // Simple Vertical Centering or World Positioning:
    start_y = (float)anchor_y;
    end_y = start_y + img_h;

    // 4. Draw Loop
    for (float y = start_y; y < end_y; y += img_h) {
      for (float x = start_x; x < end_x; x += img_w) {
        // Culling: Don't issue draw commands for tiles completely outside viewport
        if (x + img_w < p_min.x || x > p_max.x) continue;
        if (y + img_h < p_min.y || y > p_max.y) continue;

        draw_list->AddImage(reinterpret_cast<ImTextureID>(texture.sdl_texture), ImVec2(x, y),
                            ImVec2(x + img_w, y + img_h));
      }
    }
  }
}

void ViewportTab::RenderLevelBounds(const Level& level) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  // Level bounds rectangle
  Vec tl = {0, 0};
  Vec br = {level.width, level.height};

  ImVec2 p_min = canvas_.WorldToScreen(tl);
  ImVec2 p_max = canvas_.WorldToScreen(br);

  // Draw border
  draw_list->AddRect(p_min, p_max, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);

  // Draw text label
  draw_list->AddText(p_min, IM_COL32(255, 0, 0, 255), "Level Bounds");
}

}  // namespace zebes
