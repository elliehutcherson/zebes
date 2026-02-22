#include "editor/level_editor/viewport_tab.h"

#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/viewport_tab.h"
#include "imgui.h"

namespace zebes {
namespace {

struct Rect {
  ImVec2 min;
  ImVec2 max;
};

std::vector<Rect> CalculateParallaxTiles(Vec camera_p, Vec viewport_size, Vec image_size,
                                         float zoom, Vec scroll_factor) {
  // 1. Calculate the view size in world units
  float view_w = viewport_size.x / zoom;
  float view_h = viewport_size.y / zoom;

  LOG(INFO) << "camera_p: " << camera_p;
  LOG(INFO) << "viewport_size: " << viewport_size;
  LOG(INFO) << "image_size: " << image_size;
  LOG(INFO) << "zoom: " << zoom;
  LOG(INFO) << "scroll_factor: " << scroll_factor;

  float left = camera_p.x - (view_w / 2.0f);
  float left_adj = left * scroll_factor.x;
  float right_adj = left_adj + view_w;

  float top = camera_p.y - (view_h / 2.0f);
  float top_adj = top * scroll_factor.y;
  float bottom_adj = top + view_w;

  // float right = camera_p.x + (view_w / 2.0f);
  // float bottom = camera_p.y + (view_h / 2.0f);

  // 2. Calculate effective camera position
  // float cam_eff_x = camera_p.x * scroll_factor.x;
  // float cam_eff_y = camera_p.y * scroll_factor.y;

  // 3. Determine the visible boundaries in the layer's space
  LOG(INFO) << "left = " << left_adj;
  LOG(INFO) << "view_w = " << view_w;

  // 4. Calculate starting and ending grid indices
  // Note: std::floor is critical here to handle negative coordinates correctly
  // int start_col = static_cast<int>(std::floor(left / image_size.x));
  // int end_col = static_cast<int>(std::floor(right / image_size.x));
  // int start_row = static_cast<int>(std::floor(top / image_size.y));
  // int end_row = static_cast<int>(std::floor(bottom / image_size.y));

  // 5. Calculate world offset for the parallax layer
  // float offset_x = -(camera_p.x * (1.0f - scroll_factor.x));
  // float offset_y = -(camera_p.y * (1.0f - scroll_factor.y));

  // 6. Generate bounding boxes for all visible tiles
  std::vector<Rect> tiles;
  Rect tile;
  tile.min.x = left_adj;
  tile.min.y = top_adj;
  tile.max.x = right_adj;
  tile.max.y = bottom_adj;
  tiles.push_back(tile);
  LOG(INFO) << left_adj << "->" << right_adj << ", " << top_adj << "->" << bottom_adj;
  //     tile.min.x = offset_x + (col * image_size.x);
  //     tile.min.y = offset_y + (row * image_size.y);

  //     tile.max.x = tile.min.x + image_size.x;
  //     tile.max.y = tile.min.y + image_size.y;

  // for (int row = start_row; row <= end_row; ++row) {
  //   for (int col = start_col; col <= end_col; ++col) {
  //     Rect tile;
  //     tile.min.x = offset_x + (col * image_size.x);
  //     tile.min.y = offset_y + (row * image_size.y);

  //     tile.max.x = tile.min.x + image_size.x;
  //     tile.max.y = tile.min.y + image_size.y;

  //     tiles.push_back(tile);
  //   }
  // }

  return tiles;
}

}  // namespace

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
  RenderZones(level);
  canvas_.DrawGrid();
  RenderLevelBounds(level);

  // TODO: Render Entities, Tiles, etc.

  // 2. Handle Input (Keyboard Panning)
  canvas_.HandleInput();

  // 3. Capture values before End() nullifies the internal camera pointer.
  Vec mouse_world = canvas_.ScreenToWorld(gui_->GetMousePos());
  float zoom = canvas_.GetZoom();

  canvas_.End();

  // 4. Status Bar / Debug Overlay
  gui_->Text("Cam: (%.0f, %.0f) | Zoom: %.2f | Mouse World: (%.0f, %.0f)", camera_.position.x,
             camera_.position.y, zoom, mouse_world.x, mouse_world.y);

  gui_->SameLine();
  if (gui_->Button("Reset View")) {
    Reset();
  }

  return absl::OkStatus();
}

void ViewportTab::RenderZones(const Level& level) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  for (const ParallaxZone& zone : level.zones) {
    ImVec2 p_min = canvas_.WorldToScreen(zone.min_point);
    ImVec2 p_max = canvas_.WorldToScreen(zone.max_point);

    // Frustum Culling
    ImVec2 canvas_p_min = ImGui::GetWindowPos();
    ImVec2 canvas_p_max = ImVec2(canvas_p_min.x + ImGui::GetWindowSize().x,
                                 canvas_p_min.y + ImGui::GetWindowSize().y);
    if (p_max.x < canvas_p_min.x || p_min.x > canvas_p_max.x || p_max.y < canvas_p_min.y ||
        p_min.y > canvas_p_max.y) {
      continue;
    }

    auto it = level.themes.find(zone.theme_id);
    if (it == level.themes.end()) continue;
    const ParallaxTheme& theme = it->second;

    for (const ParallaxLayer& layer : theme.layers) {
      if (layer.texture_id.empty()) continue;

      absl::StatusOr<Texture*> texture_or = api_.GetTexture(layer.texture_id);
      if (!texture_or.ok() || *texture_or == nullptr) continue;
      const Texture& texture = *(*texture_or);

      int w = 0, h = 0;
      SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture.sdl_texture), nullptr, nullptr, &w,
                       &h);
      if (w == 0 || h == 0) continue;

      float image_w = static_cast<float>(w) * layer.base_scale;
      float image_h = static_cast<float>(h) * layer.base_scale;

      // Where image (0,0) goes in world space
      Vec origin = camera_.ParallaxWorldOrigin(layer.scroll_factor, layer.offset);

      // Visible world area
      float view_w = camera_.viewport_width / camera_.zoom;
      float view_h = camera_.viewport_height / camera_.zoom;
      float world_left = camera_.position.x - view_w / 2.0f;
      float world_top = camera_.position.y - view_h / 2.0f;

      // Which tiles are visible
      int start_col = (int)std::floor((world_left - origin.x) / image_w);
      int end_col = (int)std::floor((world_left + view_w - origin.x) / image_w);
      int start_row = (int)std::floor((world_top - origin.y) / image_h);
      int end_row = (int)std::floor((world_top + view_h - origin.y) / image_h);

      if (!layer.repeat_x) {
        start_col = 0;
        end_col = 0;
      }
      if (!layer.repeat_y) {
        start_row = 0;
        end_row = 0;
      }

      for (int row = start_row; row <= end_row; ++row) {
        for (int col = start_col; col <= end_col; ++col) {
          Vec tile_world = {origin.x + col * (double)image_w, origin.y + row * (double)image_h};

          ImVec2 sp_min = canvas_.WorldToScreen(tile_world);
          ImVec2 sp_max =
              ImVec2(sp_min.x + image_w * camera_.zoom, sp_min.y + image_h * camera_.zoom);

          draw_list->AddImage(reinterpret_cast<ImTextureID>(texture.sdl_texture), sp_min, sp_max,
                              ImVec2(0, 0), ImVec2(1, 1));
        }
      }
    }

    draw_list->AddRect(p_min, p_max, IM_COL32(255, 255, 0, 150), 0.0f, 0, 2.0f);
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
