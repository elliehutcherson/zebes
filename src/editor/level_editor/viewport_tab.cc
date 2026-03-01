#include "editor/level_editor/viewport_tab.h"

#include <cmath>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

ViewportTab::ViewportTab(Api& api, GuiInterface* gui)
    : api_(api),
      gui_(gui),
      canvas_(Canvas::Options{
          .gui = gui,
          .snap_grid = true,
          .grid_size = static_cast<float>(TileChunk::kSize),
      }) {
  camera_ = Camera{};
}

void ViewportTab::Reset() { camera_ = {}; }

std::optional<Entity> ViewportTab::TakeNewEntity() {
  return std::exchange(pending_entity_, std::nullopt);
}

std::optional<uint64_t> ViewportTab::TakeClickSelection() {
  return std::exchange(click_selected_entity_id_, std::nullopt);
}

bool ViewportTab::TakeEntityMoved() {
  bool moved = entity_moved_;
  entity_moved_ = false;
  return moved;
}

std::optional<uint64_t> ViewportTab::TakeDeleteRequest() {
  return std::exchange(delete_requested_entity_id_, std::nullopt);
}

absl::Status ViewportTab::Render(const ViewportRenderOptions& options) {
  Level& level = *options.level;

  // Ensure next_entity_id_ is always greater than every entity already in the
  // level. This guards against collisions when a saved level is loaded whose
  // existing IDs are higher than the counter's current value.
  uint64_t available = NextAvailableEntityId(level.entities);
  if (available > next_entity_id_) {
    next_entity_id_ = available;
  }

  auto child = ScopedChild(gui_, "ViewportCanvas", ImVec2(0, 0), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y -= 25;  // Leave room for the status bar

  canvas_.SetWorldBounds({0, 0}, {level.width, level.height});
  canvas_.SetSnap(options.snap_to_grid);
  canvas_.Begin("LevelCanvas", canvas_size, camera_);

  // 1. Render Scene Elements
  RenderZones(level);
  canvas_.DrawGrid();
  RenderLevelBounds(level);
  RenderEntities(level, options.selected_entity_id, options.show_entity_borders);

  // 2. Render placement ghost at mouse position when in placement mode.
  Vec mouse_world = canvas_.ScreenToWorld(gui_->GetMousePos());
  mouse_world = canvas_.SnapToGrid(mouse_world);
  if (options.placement_blueprint != nullptr) {
    RenderPlacementGhost(*options.placement_blueprint, mouse_world);
  }

  // 3. Handle input (pan/zoom from keyboard/mouse wheel, then entity interaction).
  canvas_.HandleInput();
  LOG_IF_ERROR(HandleEntityInput(level, options.placement_blueprint, mouse_world,
                                 options.selected_entity_id, options.delete_mode));

  // 4. Capture zoom before End() nullifies the camera pointer.
  float zoom = canvas_.GetZoom();

  canvas_.End();

  // 5. Status bar
  gui_->Text("Cam: (%.0f, %.0f) | Zoom: %.2f | Mouse World: (%.0f, %.0f)", camera_.position.x,
             camera_.position.y, zoom, mouse_world.x, mouse_world.y);

  gui_->SameLine();
  if (gui_->Button("Reset View")) {
    Reset();
  }

  return absl::OkStatus();
}

absl::Status ViewportTab::HandleEntityInput(Level& level, const Blueprint* placement_blueprint,
                                            Vec mouse_world, uint64_t selected_entity_id,
                                            bool delete_mode) {
  // --- Delete mode: a right-click removes the entity under the cursor ---
  if (delete_mode && gui_->IsItemClicked(1)) {
    uint64_t picked = PickEntity(level.entities, mouse_world);
    if (picked != Entity::kInvalidId) {
      delete_requested_entity_id_ = picked;
    }
    return absl::OkStatus();
  }

  // --- Placement mode: a left-click drops a new entity ---
  if (placement_blueprint != nullptr) {
    if (!gui_->IsItemClicked(0)) return absl::OkStatus();

    Entity entity =
        CreateEntityFromBlueprint(*placement_blueprint, 0, mouse_world, next_entity_id_++);

    // Resolve sprite pointer from the blueprint's first state.
    // Invisible blueprints have no sprite; leave entity.sprite null in that case.
    std::optional<std::string> sprite_id = placement_blueprint->sprite_id(0);
    if (sprite_id.has_value()) {
      ASSIGN_OR_RETURN(Sprite * sprite, api_.GetSprite(*sprite_id));
      entity.sprite = sprite;
    }
    pending_entity_ = entity;
    return absl::OkStatus();
  }

  // --- Drag update: runs every frame while a drag is in progress ---
  if (dragging_entity_) {
    if (gui_->IsItemActive()) {
      auto it = level.entities.find(drag_entity_id_);
      if (it != level.entities.end()) {
        it->second.transform.position = {mouse_world.x - drag_offset_.x,
                                         mouse_world.y - drag_offset_.y};
        entity_moved_ = true;
      }
      return absl::OkStatus();
    }

    // Mouse released — end drag.
    dragging_entity_ = false;
    drag_entity_id_ = Entity::kInvalidId;
    return absl::OkStatus();
  }

  // --- Click: select entity or deselect, and optionally start a drag ---
  if (gui_->IsItemClicked(0)) {
    uint64_t picked = PickEntity(level.entities, mouse_world);
    click_selected_entity_id_ = picked;

    // Begin dragging if the click landed on the already-selected entity.
    if (picked != Entity::kInvalidId && picked == selected_entity_id) {
      auto it = level.entities.find(picked);
      if (it != level.entities.end()) {
        dragging_entity_ = true;
        drag_entity_id_ = picked;
        drag_offset_ = {mouse_world.x - it->second.transform.position.x,
                        mouse_world.y - it->second.transform.position.y};
      }
    }
  }

  return absl::OkStatus();
}

void ViewportTab::RenderEntities(const Level& level, uint64_t selected_entity_id,
                                 bool show_borders) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  for (const auto& [id, entity] : level.entities) {
    if (!entity.active) continue;

    ImVec2 sp_min;
    ImVec2 sp_max;

    if (entity.sprite != nullptr && !entity.sprite->frames.empty() &&
        entity.sprite->sdl_texture != nullptr) {
      const SpriteFrame& frame = entity.sprite->frames[0];

      int tex_w = 0, tex_h = 0;
      SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(entity.sprite->sdl_texture), nullptr, nullptr,
                       &tex_w, &tex_h);

      sp_min = canvas_.WorldToScreen({entity.transform.position.x + frame.offset_x,
                                      entity.transform.position.y + frame.offset_y});
      sp_max = ImVec2(sp_min.x + frame.render_w * camera_.zoom,
                      sp_min.y + frame.render_h * camera_.zoom);

      if (tex_w > 0 && tex_h > 0) {
        float u0 = static_cast<float>(frame.texture_x) / tex_w;
        float v0 = static_cast<float>(frame.texture_y) / tex_h;
        float u1 = static_cast<float>(frame.texture_x + frame.texture_w) / tex_w;
        float v1 = static_cast<float>(frame.texture_y + frame.texture_h) / tex_h;

        draw_list->AddImage(reinterpret_cast<ImTextureID>(entity.sprite->sdl_texture), sp_min,
                            sp_max, ImVec2(u0, v0), ImVec2(u1, v1));
      }
    } else {
      // No sprite: draw a placeholder box.
      constexpr int kDefaultSize = 32;
      sp_min = canvas_.WorldToScreen(entity.transform.position);
      sp_max =
          ImVec2(sp_min.x + kDefaultSize * camera_.zoom, sp_min.y + kDefaultSize * camera_.zoom);
      draw_list->AddRectFilled(sp_min, sp_max, IM_COL32(100, 100, 200, 180));
    }

    // Draw a border around every entity when the toggle is on.
    if (show_borders) {
      draw_list->AddRect(sp_min, sp_max, IM_COL32(255, 255, 255, 180), 0.0f, 0, 1.0f);
    }
    // Highlight the selected entity with a thicker gold outline on top.
    if (id == selected_entity_id) {
      draw_list->AddRect(sp_min, sp_max, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
    }
  }
}

void ViewportTab::RenderPlacementGhost(const Blueprint& blueprint, Vec world_pos) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  Sprite* sprite = nullptr;
  std::optional<std::string> sprite_id_opt = blueprint.sprite_id(0);
  if (sprite_id_opt.has_value()) {
    absl::StatusOr<Sprite*> sprite_or = api_.GetSprite(*sprite_id_opt);
    if (sprite_or.ok() && *sprite_or != nullptr && !(*sprite_or)->frames.empty() &&
        (*sprite_or)->sdl_texture != nullptr)
      sprite = *sprite_or;
  }

  if (sprite != nullptr) {
    const SpriteFrame& frame = sprite->frames[0];

    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(sprite->sdl_texture), nullptr, nullptr, &tex_w,
                     &tex_h);

    ImVec2 ghost_min =
        canvas_.WorldToScreen({world_pos.x + frame.offset_x, world_pos.y + frame.offset_y});
    ImVec2 ghost_max = ImVec2(ghost_min.x + frame.render_w * camera_.zoom,
                              ghost_min.y + frame.render_h * camera_.zoom);

    if (tex_w > 0 && tex_h > 0) {
      float u0 = static_cast<float>(frame.texture_x) / tex_w;
      float v0 = static_cast<float>(frame.texture_y) / tex_h;
      float u1 = static_cast<float>(frame.texture_x + frame.texture_w) / tex_w;
      float v1 = static_cast<float>(frame.texture_y + frame.texture_h) / tex_h;

      // Semi-transparent tint to indicate ghost/preview.
      draw_list->AddImage(reinterpret_cast<ImTextureID>(sprite->sdl_texture), ghost_min, ghost_max,
                          ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32(255, 255, 255, 160));
      return;
    }
  }

  // Fallback: draw a simple colored box.
  constexpr int kDefaultSize = 32;
  ImVec2 ghost_min = canvas_.WorldToScreen(world_pos);
  ImVec2 ghost_max =
      ImVec2(ghost_min.x + kDefaultSize * camera_.zoom, ghost_min.y + kDefaultSize * camera_.zoom);
  draw_list->AddRectFilled(ghost_min, ghost_max, IM_COL32(100, 200, 100, 100));
  draw_list->AddRect(ghost_min, ghost_max, IM_COL32(100, 200, 100, 220), 0.0f, 0, 2.0f);
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

  Vec tl = {0, 0};
  Vec br = {level.width, level.height};

  ImVec2 p_min = canvas_.WorldToScreen(tl);
  ImVec2 p_max = canvas_.WorldToScreen(br);

  draw_list->AddRect(p_min, p_max, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
  draw_list->AddText(p_min, IM_COL32(255, 0, 0, 255), "Level Bounds");
}

}  // namespace zebes
