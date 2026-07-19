#include "editor/level_editor/viewport_tab.h"

#include <cmath>
#include <limits>

#include "SDL_render.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"
#include "editor/canvas/tile_draw.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/texture.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

namespace {

// Draws a single tile cell onto the draw list at the given world-tile coordinate.
// texture_handle may be null; overlays (frame, collision) are drawn regardless so
// that invisible tiles remain visible in the editor.
// tile_render_w/h control the world-space pixel size of the rendered cell;
// UV sampling always uses the source rect from tileset.tile_width/tile_height.
void DrawTileAt(ImDrawList* dl, const Canvas& canvas, const Camera& camera, const Tile& tile,
                const Tileset& tileset, void* texture_handle, int tex_w, int tex_h, int world_tile_x,
                int world_tile_y, int tile_render_w, int tile_render_h, bool show_frame,
                bool show_collision, float tile_overlay_opacity) {
  Vec world_pos = {world_tile_x * static_cast<double>(tile_render_w),
                   world_tile_y * static_cast<double>(tile_render_h)};
  ImVec2 sp_min = canvas.WorldToScreen(world_pos);
  ImVec2 sp_max =
      ImVec2(sp_min.x + tile_render_w * camera.zoom, sp_min.y + tile_render_h * camera.zoom);

  if (texture_handle != nullptr && tex_w > 0 && tex_h > 0) {
    float u0 = static_cast<float>(tile.source_x) / tex_w;
    float v0 = static_cast<float>(tile.source_y) / tex_h;
    float u1 = static_cast<float>(tile.source_x + tileset.tile_width) / tex_w;
    float v1 = static_cast<float>(tile.source_y + tileset.tile_height) / tex_h;
    dl->AddImage(reinterpret_cast<ImTextureID>(texture_handle), sp_min, sp_max, ImVec2(u0, v0),
                 ImVec2(u1, v1));
  }
  if (tile_overlay_opacity > 0.0f) {
    dl->AddRectFilled(sp_min, sp_max,
                      IM_COL32(50, 100, 255, static_cast<uint8_t>(tile_overlay_opacity * 255.0f)));
  }
  if (show_frame) {
    dl->AddRect(sp_min, sp_max, IM_COL32(200, 200, 200, 100), 0.0f, 0, 1.0f);
  }
  if (show_collision && tile.shape != TileShape::kNone) {
    DrawShapeOverlay(dl, sp_min, sp_max, tile.shape);
  }
}

}  // namespace

absl::StatusOr<Vec> SnapEntityToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                     const Collider* collider, const Sprite* sprite) {
  int tile_x = static_cast<int>(std::floor(mouse_world.x / tile_render_w));
  int tile_y = static_cast<int>(std::floor(mouse_world.y / tile_render_h));
  double cell_center_x = tile_x * tile_render_w + tile_render_w / 2.0;
  double cell_bottom_y = (tile_y + 1) * static_cast<double>(tile_render_h);

  if (collider != nullptr && !collider->polygons.empty()) {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    for (const Polygon& poly : collider->polygons) {
      for (const Vec& pt : poly) {
        min_x = std::min(min_x, pt.x);
        max_x = std::max(max_x, pt.x);
        max_y = std::max(max_y, pt.y);
      }
    }
    return Vec{cell_center_x - (min_x + max_x) / 2.0, cell_bottom_y - max_y};
  }

  if (sprite != nullptr && !sprite->frames.empty()) {
    const SpriteFrame& frame = sprite->frames[0];
    if (frame.render_w <= 0 || frame.render_h <= 0) {
      LOG(ERROR) << "SnapEntityToGrid: sprite frame has invalid render dimensions";
      return absl::InvalidArgumentError("sprite frame has invalid render dimensions");
    }
    return Vec{cell_center_x - (frame.offset_x + frame.render_w / 2.0),
               cell_bottom_y - (frame.offset_y + frame.render_h)};
  }

  LOG(ERROR) << "SnapEntityToGrid: blueprint has no valid collider or sprite";
  return absl::InvalidArgumentError("blueprint has no valid collider or sprite");
}

absl::StatusOr<Vec> ViewportTab::SnapBlueprintToGrid(Vec mouse_world, const Blueprint& blueprint,
                                                     int tile_render_w, int tile_render_h) const {
  if (!canvas_.GetSnap()) return mouse_world;

  Collider* collider = nullptr;
  std::optional<std::string> collider_id = blueprint.collider_id(0);
  if (collider_id.has_value()) {
    ASSIGN_OR_RETURN(collider, api_.GetCollider(*collider_id));
  }

  Sprite* sprite = nullptr;
  std::optional<std::string> sprite_id = blueprint.sprite_id(0);
  if (sprite_id.has_value()) {
    ASSIGN_OR_RETURN(sprite, api_.GetSprite(*sprite_id));
  }

  return SnapEntityToGrid(mouse_world, tile_render_w, tile_render_h, collider, sprite);
}

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

void ViewportTab::Reset() {
  camera_ = {};
  pending_camera_frame_.reset();
}

void ViewportTab::FrameZone(const ParallaxZone& zone) {
  pending_camera_frame_ = VisibleWorldBounds{
      .min = zone.min_point,
      .max = zone.max_point,
  };
}

void ViewportTab::ApplyPendingCameraFrame(const ImVec2& viewport_size,
                                          VisibleWorldBounds world_bounds) {
  if (!pending_camera_frame_.has_value()) return;

  std::optional<CameraFrame> frame = CalculateConstrainedCameraFrame(
      *pending_camera_frame_, world_bounds, static_cast<int>(viewport_size.x),
      static_cast<int>(viewport_size.y));
  pending_camera_frame_.reset();
  if (!frame.has_value()) return;

  camera_.position = frame->position;
  camera_.zoom = frame->zoom;
}

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

  ApplyPendingCameraFrame(canvas_size,
                          {.min = {0, 0}, .max = {level.width, level.height}});

  canvas_.SetWorldBounds({0, 0}, {level.width, level.height});
  canvas_.SetSnap(options.snap_to_grid);
  canvas_.SetGridSize(static_cast<float>(level.tile_render_width));
  canvas_.Begin("LevelCanvas", canvas_size, camera_);

  // Capture canvas input before calculating any scene geometry so zoom and
  // camera movement are reflected immediately in this frame.
  canvas_.HandleInput();

  if (options.selected_zone_id.has_value() && gui_->IsItemHovered() &&
      gui_->IsKeyPressed(ImGuiKey_F, false)) {
    if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, *options.selected_zone_id);
        zone != nullptr) {
      FrameZone(*zone);
    }
  }

  // Determine the active tileset: from options, or from the level's stored id.
  const Tileset* active_tileset = options.placement_tileset;
  if (active_tileset == nullptr && !level.tileset_id.empty()) {
    absl::StatusOr<Tileset*> tileset = api_.GetTileset(level.tileset_id);
    if (tileset.ok() && *tileset != nullptr) active_tileset = *tileset;
  }

  // 1. Render scene elements.
  std::optional<ActiveParallaxZone> active_zone = RenderParallaxBackground(level);
  canvas_.DrawGrid();
  RenderLevelBounds(level);
  if (active_tileset != nullptr) {
    RenderTileChunks(level, *active_tileset, options.show_tile_frame, options.show_tile_collision,
                     options.tile_overlay_opacity);
  }
  RenderEntities(level, options.selected_entity_id, options.show_entity_borders,
                 options.entity_overlay_opacity);
  RenderZoneGizmos(level, options.selected_zone_id,
                   active_zone.has_value() ? std::optional<int>(active_zone->zone_id)
                                           : std::nullopt);

  // 2. Render placement ghost. Tile mode uses its own grid snap; blueprint mode
  //    snaps to the blueprint's own collider/sprite dimensions.
  Vec mouse_world = canvas_.ScreenToWorld(gui_->GetMousePos());
  if (options.placement_tile != nullptr && options.placement_tileset != nullptr) {
    RenderTilePlacementGhost(*options.placement_tile, *options.placement_tileset, mouse_world,
                             level.tile_render_width, level.tile_render_height);
  } else if (options.placement_blueprint != nullptr) {
    ASSIGN_OR_RETURN(Vec snapped,
                     SnapBlueprintToGrid(mouse_world, *options.placement_blueprint,
                                         level.tile_render_width, level.tile_render_height));
    RenderPlacementGhost(*options.placement_blueprint, snapped);
  }

  // 3. Handle mode-specific interaction. Canvas input above created the
  // invisible interaction surface, so its item state remains available here.
  if (options.placement_tile != nullptr && options.placement_tileset != nullptr) {
    LOG_IF_ERROR(HandleTileInput(level, *options.placement_tile, *options.placement_tileset,
                                 mouse_world, level.tile_render_width, level.tile_render_height));
  } else if (options.placement_blueprint != nullptr) {
    ASSIGN_OR_RETURN(Vec snapped,
                     SnapBlueprintToGrid(mouse_world, *options.placement_blueprint,
                                         level.tile_render_width, level.tile_render_height));
    LOG_IF_ERROR(HandleEntityInput(level, options.placement_blueprint, snapped,
                                   options.selected_entity_id, options.delete_mode));
  } else {
    LOG_IF_ERROR(HandleEntityInput(level, nullptr, mouse_world, options.selected_entity_id,
                                   options.delete_mode));
  }

  // 4. Capture zoom before End() nullifies the camera pointer.
  float zoom = canvas_.GetZoom();

  canvas_.End();

  // 5. Status bar
  const char* active_zone_name = "None";
  if (active_zone.has_value()) {
    if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, active_zone->zone_id);
        zone != nullptr) {
      active_zone_name = zone->name.c_str();
    }
  }
  gui_->Text("Cam: (%.0f, %.0f) | Zoom: %.2f | Mouse: (%.0f, %.0f) | Zone: %s",
             camera_.position.x, camera_.position.y, zoom, mouse_world.x, mouse_world.y,
             active_zone_name);

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
                                 bool show_borders, float entity_overlay_opacity) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  for (const auto& [id, entity] : level.entities) {
    if (!entity.active) continue;

    ImVec2 sp_min;
    ImVec2 sp_max;

    if (entity.sprite != nullptr && !entity.sprite->frames.empty() &&
        entity.sprite->texture_handle) {
      const SpriteFrame& frame = entity.sprite->frames[0];

      int tex_w = 0, tex_h = 0;
      SDL_Texture* native_texture =
          SdlTextureHandleAdapter::ToNative(entity.sprite->texture_handle);
      SDL_QueryTexture(native_texture, nullptr, nullptr, &tex_w, &tex_h);

      sp_min = canvas_.WorldToScreen({entity.transform.position.x + frame.offset_x,
                                      entity.transform.position.y + frame.offset_y});
      sp_max = ImVec2(sp_min.x + frame.render_w * camera_.zoom,
                      sp_min.y + frame.render_h * camera_.zoom);

      if (tex_w > 0 && tex_h > 0) {
        float u0 = static_cast<float>(frame.texture_x) / tex_w;
        float v0 = static_cast<float>(frame.texture_y) / tex_h;
        float u1 = static_cast<float>(frame.texture_x + frame.texture_w) / tex_w;
        float v1 = static_cast<float>(frame.texture_y + frame.texture_h) / tex_h;

        draw_list->AddImage(reinterpret_cast<ImTextureID>(native_texture), sp_min, sp_max,
                            ImVec2(u0, v0), ImVec2(u1, v1));
      }
    } else {
      // No sprite: draw a placeholder box.
      constexpr int kDefaultSize = 32;
      sp_min = canvas_.WorldToScreen(entity.transform.position);
      sp_max =
          ImVec2(sp_min.x + kDefaultSize * camera_.zoom, sp_min.y + kDefaultSize * camera_.zoom);
      draw_list->AddRectFilled(sp_min, sp_max, IM_COL32(100, 100, 200, 180));
    }

    // Yellow overlay to help identify invisible entities.
    if (entity_overlay_opacity > 0.0f) {
      draw_list->AddRectFilled(
          sp_min, sp_max,
          IM_COL32(255, 200, 0, static_cast<uint8_t>(entity_overlay_opacity * 255.0f)));
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
        (*sprite_or)->texture_handle)
      sprite = *sprite_or;
  }

  if (sprite != nullptr) {
    const SpriteFrame& frame = sprite->frames[0];

    int tex_w = 0, tex_h = 0;
    SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(sprite->texture_handle);
    SDL_QueryTexture(native_texture, nullptr, nullptr, &tex_w, &tex_h);

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
      draw_list->AddImage(reinterpret_cast<ImTextureID>(native_texture), ghost_min, ghost_max,
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

std::optional<ActiveParallaxZone> ViewportTab::RenderParallaxBackground(const Level& level) {
  std::optional<ActiveParallaxZone> active =
      ResolveActiveParallaxZone(level.zones, camera_.position);
  if (!active.has_value()) return std::nullopt;

  auto theme_it = level.themes.find(active->theme_id);
  if (theme_it == level.themes.end()) return active;

  RenderParallaxTheme(theme_it->second);
  return active;
}

void ViewportTab::RenderParallaxTheme(const ParallaxTheme& theme) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  for (const ParallaxLayer& layer : theme.layers) {
    if (layer.texture_id.empty()) continue;

    absl::StatusOr<Texture*> texture_or = api_.GetTexture(layer.texture_id);
    if (!texture_or.ok() || *texture_or == nullptr) continue;
    const Texture& texture = *(*texture_or);

    int w = 0, h = 0;
    SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(texture.texture_handle);
    SDL_QueryTexture(native_texture, nullptr, nullptr, &w, &h);
    if (w == 0 || h == 0) continue;

    std::optional<ParallaxLayout> layout = CalculateParallaxLayout(camera_, layer, w, h);
    if (!layout.has_value()) continue;

    for (int row = layout->first_row; row <= layout->last_row; ++row) {
      for (int col = layout->first_column; col <= layout->last_column; ++col) {
        Vec tile_world = {layout->origin.x + col * layout->tile_width,
                          layout->origin.y + row * layout->tile_height};

        ImVec2 sp_min = canvas_.WorldToScreen(tile_world);
        ImVec2 sp_max = ImVec2(sp_min.x + layout->tile_width * camera_.zoom,
                               sp_min.y + layout->tile_height * camera_.zoom);

        draw_list->AddImage(reinterpret_cast<ImTextureID>(native_texture), sp_min, sp_max,
                            ImVec2(0, 0), ImVec2(1, 1));
      }
    }
  }
}

void ViewportTab::RenderZoneGizmos(const Level& level, std::optional<int> selected_zone_id,
                                   std::optional<int> active_zone_id) {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera_);
  for (const ParallaxZone& zone : level.zones) {
    const bool is_visible = zone.max_point.x >= visible.min.x &&
                            zone.min_point.x <= visible.max.x &&
                            zone.max_point.y >= visible.min.y &&
                            zone.min_point.y <= visible.max.y;
    if (!is_visible) continue;

    ImU32 color = IM_COL32(255, 255, 0, 150);
    float thickness = 2.0f;
    if (active_zone_id == zone.id) {
      color = IM_COL32(80, 220, 120, 220);
    }
    if (selected_zone_id == zone.id) {
      color = IM_COL32(255, 200, 0, 255);
      thickness = 3.0f;
    }

    draw_list->AddRect(canvas_.WorldToScreen(zone.min_point),
                       canvas_.WorldToScreen(zone.max_point), color, 0.0f, 0, thickness);
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

void ViewportTab::RenderTileChunks(const Level& level, const Tileset& tileset, bool show_frame,
                                   bool show_collision, float tile_overlay_opacity) {
  ImDrawList* dl = canvas_.GetDrawList();
  if (!dl) return;

  // Resolve the atlas texture. A non-empty texture_id must resolve successfully.
  void* texture_handle = nullptr;
  int tex_w = 0, tex_h = 0;
  if (!tileset.texture_id.empty()) {
    absl::StatusOr<Texture*> texture = api_.GetTexture(tileset.texture_id);
    if (!texture.ok() || *texture == nullptr || !(*texture)->texture_handle) {
      LOG(ERROR) << "RenderTileChunks: failed to resolve texture '" << tileset.texture_id << "'";
      return;
    }
    texture_handle = SdlTextureHandleAdapter::ToNative((*texture)->texture_handle);
    SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture_handle), nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
      LOG(ERROR) << "RenderTileChunks: texture has invalid dimensions '" << tileset.texture_id
                 << "'";
      return;
    }
  }

  // Build an id→tile lookup once so each chunk cell lookup is O(1).
  absl::flat_hash_map<int, const Tile*> tile_map;
  for (const Tile& t : tileset.tiles) tile_map[t.id] = &t;

  for (const auto& [key, chunk] : level.tile_chunks) {
    int chunk_x = static_cast<int32_t>(key);        // low 32 bits
    int chunk_y = static_cast<int32_t>(key >> 32);  // high 32 bits

    for (int i = 0; i < TileChunk::kSize * TileChunk::kSize; ++i) {
      int tile_id = chunk.tiles[i];
      if (tile_id == 0) continue;

      auto it = tile_map.find(tile_id);
      if (it == tile_map.end()) continue;

      int local_x = i % TileChunk::kSize;
      int local_y = i / TileChunk::kSize;
      DrawTileAt(dl, canvas_, camera_, *it->second, tileset, texture_handle, tex_w, tex_h,
                 chunk_x * TileChunk::kSize + local_x, chunk_y * TileChunk::kSize + local_y,
                 level.tile_render_width, level.tile_render_height, show_frame, show_collision,
                 tile_overlay_opacity);
    }
  }
}

void ViewportTab::RenderTilePlacementGhost(const Tile& tile, const Tileset& tileset,
                                           Vec mouse_world, int tile_render_w, int tile_render_h) {
  ImDrawList* dl = canvas_.GetDrawList();
  if (!dl) return;

  // Snap to the tile render grid (not the source size).
  int tile_x = static_cast<int>(std::floor(mouse_world.x / tile_render_w));
  int tile_y = static_cast<int>(std::floor(mouse_world.y / tile_render_h));
  Vec snapped = {tile_x * static_cast<double>(tile_render_w),
                 tile_y * static_cast<double>(tile_render_h)};

  ImVec2 sp_min = canvas_.WorldToScreen(snapped);
  ImVec2 sp_max =
      ImVec2(sp_min.x + tile_render_w * camera_.zoom, sp_min.y + tile_render_h * camera_.zoom);

  if (!tileset.texture_id.empty()) {
    absl::StatusOr<Texture*> texture = api_.GetTexture(tileset.texture_id);
    if (!texture.ok() || *texture == nullptr || !(*texture)->texture_handle) {
      LOG(ERROR) << "RenderTilePlacementGhost: failed to resolve texture '" << tileset.texture_id
                 << "'";
      return;
    }
    void* texture_handle = SdlTextureHandleAdapter::ToNative((*texture)->texture_handle);
    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture_handle), nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
      LOG(ERROR) << "RenderTilePlacementGhost: texture has invalid dimensions '"
                 << tileset.texture_id << "'";
      return;
    }
    // UV sampling always uses the source rect from the tileset atlas.
    float u0 = static_cast<float>(tile.source_x) / tex_w;
    float v0 = static_cast<float>(tile.source_y) / tex_h;
    float u1 = static_cast<float>(tile.source_x + tileset.tile_width) / tex_w;
    float v1 = static_cast<float>(tile.source_y + tileset.tile_height) / tex_h;
    dl->AddImage(reinterpret_cast<ImTextureID>(texture_handle), sp_min, sp_max, ImVec2(u0, v0),
                 ImVec2(u1, v1), IM_COL32(255, 255, 255, 160));
  } else {
    // No texture: draw a colored placeholder so the user still sees a ghost.
    dl->AddRectFilled(sp_min, sp_max, IM_COL32(100, 200, 100, 100));
  }
  dl->AddRect(sp_min, sp_max, IM_COL32(100, 200, 255, 200), 0.0f, 0, 2.0f);
}

absl::Status ViewportTab::HandleTileInput(Level& level, const Tile& tile, const Tileset& tileset,
                                          Vec mouse_world, int tile_render_w, int tile_render_h) {
  // Snap to the tile render grid (independent of the chunk snap grid).
  int tile_x = static_cast<int>(std::floor(mouse_world.x / tile_render_w));
  int tile_y = static_cast<int>(std::floor(mouse_world.y / tile_render_h));

  // Left mouse held: paint. Guard against right-click activating the canvas
  // InvisibleButton (which captures all three buttons), which would otherwise
  // make IsItemActive() true on right-click and paint instead of erase.
  if (gui_->IsItemActive() && gui_->GetIO().MouseDown[ImGuiMouseButton_Left]) {
    SetTileAt(level, tile_x, tile_y, tile.id);
    return absl::OkStatus();
  }

  // Right click or right mouse held: erase. A single right-click is the primary
  // erase gesture; the IsItemActive + MouseDown check allows dragging to erase
  // across multiple tiles without re-clicking each one.
  if (gui_->IsItemClicked(ImGuiMouseButton_Right) ||
      (gui_->IsItemActive() && gui_->GetIO().MouseDown[ImGuiMouseButton_Right])) {
    SetTileAt(level, tile_x, tile_y, 0);
  }

  return absl::OkStatus();
}

}  // namespace zebes
