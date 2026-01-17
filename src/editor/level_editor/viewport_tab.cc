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

void ViewportTab::Reset() { camera_ = Camera{}; }

absl::Status ViewportTab::Render(Level& level) {
  // Use a child window to contain the canvas and handle input clipping
  if (auto child = ScopedChild(gui_, "ViewportCanvas", ImVec2(0, 0), false,
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    // Canvas fills the available space in the child
    ImVec2 canvas_size = gui_->GetContentRegionAvail();
    canvas_size.y -= 20;  // Leave room for status text or controls at bottom if needed

    canvas_.Begin("LevelCanvas", canvas_size, camera_);

    // 1. Render Background Elements
    canvas_.DrawGrid();
    RenderParallax(level);
    RenderLevelBounds(level);

    // TODO(ellie): Render Grid Tiles
    // TODO(ellie): Render Entities

    canvas_.End();
    canvas_.HandleInput();

    // Debug/Status Overlay
    gui_->Text("Zoom: %.2f | Offset: %.1f, %.1f | Mouse: %.1f, %.1f", canvas_.GetZoom(),
               gui_->GetCursorPos().x, gui_->GetCursorPos().y, gui_->GetMousePos().x,
               gui_->GetMousePos().y);
  }

  return absl::OkStatus();
}

void ViewportTab::RenderParallax(const Level& level) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  // We need to know the "camera" position in the canvas world.
  // Canvas does not explicitly expose a "Camera Position", but we can infer the content's
  // offset from the ScreenToWorld of the screen center, or just use `WorldToScreen` purely.

  // For parallax, we want to shift the texture based on where the "camera" is.
  // In `Canvas`, panning changes the `offset_` which acts as the camera position.

  // Since `Canvas::WorldToScreen` applies the transform:
  // ScreenPos = (WorldPos * Zoom) + Offset + ScreenCenter
  //
  // We want to draw the background image such that it appears to move slower.
  //
  // A standard parallax approach:
  // DrawPosition = LayerOrigin + (CameraPos * (1.0 - ScrollFactor))

  // However, `Canvas` handles the main "Camera" transform automatically when we call WorldToScreen.
  // So if we just draw at (0,0), it moves 1:1 with the camera.
  // To simulate parallax, we must counteract the camera movement for background layers.
  //
  // If the Canvas Pan (Offset) matches the Camera position:
  // ParallaxOffset = CanvasOffset * (1.0 - ScrollFactor)
  //
  // But `Canvas` internals for `offset_` might be screen-space relative.
  // Let's look at `Canvas::WorldToScreen(Vec(0,0))` -> This gives us the screen position of the
  // world origin.
  //
  // Let's stick to a simpler model first:
  // For each layer, we draw a texture covering the whole level (repeatedly if needed).
  Vec world_tl = canvas_.ScreenToWorld(gui_->GetCursorScreenPos());
  ImVec2 cursor = gui_->GetCursorScreenPos();
  ImVec2 avail = gui_->GetContentRegionAvail();
  Vec world_br = canvas_.ScreenToWorld(ImVec2(cursor.x + avail.x, cursor.y + avail.y));

  for (const auto& layer : level.parallax_layers) {
    if (layer.texture_id.empty()) continue;

    absl::StatusOr<Texture*> texture_or = api_.GetTexture(layer.texture_id);
    if (!texture_or.ok() || *texture_or == nullptr) continue;
    const Texture& texture = *(*texture_or);

    // Skip if invalid texture
    if (texture.sdl_texture == nullptr) continue;

    // Basic implementation: Draw a single instance of the texture at (0,0) for now.
    // TODO: Implement proper tiling/repeating based on `layer.repeat_x`.
    // TODO: Implement proper parallax scrolling factor.

    // For now, let's just draw it at the level bounds to visualize it exists.
    // We'll trust the plan's formula in the next iteration or refinement.
    //
    // Render at (0,0) world coordinates standard for now.
    Vec pos = {0, 0};

    // Get dimensions
    int w = 0, h = 0;
    SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture.sdl_texture), nullptr, nullptr, &w, &h);

    ImVec2 p_min = canvas_.WorldToScreen(pos);
    ImVec2 p_max = canvas_.WorldToScreen(Vec{static_cast<double>(w), static_cast<double>(h)});

    draw_list->AddImage(reinterpret_cast<ImTextureID>(texture.sdl_texture), p_min, p_max);
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
