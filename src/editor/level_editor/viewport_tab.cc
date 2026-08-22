#include "editor/level_editor/viewport_tab.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/camera_guide.h"
#include "editor/level_editor/viewport_model.h"
#include "editor/level_editor/viewport_scene.h"
#include "imgui.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {
namespace {

const char* ParallaxPreviewModeLabel(ParallaxPreviewMode mode) {
  switch (mode) {
    case ParallaxPreviewMode::kActiveZone:
      return "Active Zone";
    case ParallaxPreviewMode::kSelectedZone:
      return "Selected Zone";
  }
  return "Unknown";
}

absl::StatusOr<std::optional<BlueprintPlacementMode>> ResolveEntityPlacementMode(
    Api& api, const Entity& entity) {
  if (entity.blueprint_id.empty()) return std::nullopt;
  absl::StatusOr<Blueprint*> blueprint = api.GetBlueprint(entity.blueprint_id);
  if (!blueprint.ok() || *blueprint == nullptr) return std::nullopt;
  if (entity.blueprint_state_index < 0 ||
      entity.blueprint_state_index >= (*blueprint)->states.size()) {
    return absl::FailedPreconditionError("selected entity has an invalid blueprint state index");
  }
  const BlueprintPlacementMode mode =
      (*blueprint)->states[entity.blueprint_state_index].placement_mode;
  if (!IsValidBlueprintPlacementMode(mode)) {
    return absl::FailedPreconditionError("selected entity has an invalid placement mode");
  }
  return mode;
}

}  // namespace

absl::StatusOr<Vec> ViewportTab::SnapBlueprintToGrid(Vec mouse_world, const Blueprint& blueprint,
                                                     int tile_render_w, int tile_render_h) const {
  if (!canvas_.GetSnap()) return mouse_world;
  if (blueprint.states.empty()) {
    return absl::InvalidArgumentError("snapped blueprint placement requires a state");
  }
  return SnapBlueprintOriginToGrid(mouse_world, tile_render_w, tile_render_h,
                                   blueprint.states.front().placement_mode);
}

ViewportTab::ViewportTab(Api& api, GuiInterface* gui, PreviewTextureSink* terrain_ghost)
    : api_(api),
      gui_(gui),
      terrain_ghost_(terrain_ghost),
      canvas_(Canvas::Options{
          .gui = gui,
          .snap_grid = true,
          .grid_size = static_cast<float>(TileChunk::kSize),
      }),
      renderer_(canvas_) {
  camera_ = Camera{};
}

void ViewportTab::Reset() {
  camera_ = {};
  pending_camera_frame_.reset();
  interaction_.Reset();
  pending_entity_.reset();
  click_selected_entity_id_.reset();
  delete_requested_entity_id_.reset();
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

std::optional<uint64_t> ViewportTab::TakeDeleteRequest() {
  return std::exchange(delete_requested_entity_id_, std::nullopt);
}

absl::Status ViewportTab::ValidateRenderOptions(const ViewportRenderOptions& options) {
  if (options.level == nullptr) {
    return absl::InvalidArgumentError("viewport render requires a level");
  }
  if (options.placement_tile != nullptr && options.level->tileset_id.empty()) {
    return absl::InvalidArgumentError("tile placement requires the level to have a tileset");
  }
  if (options.paint_terrain_id.has_value() && options.terrain_index == nullptr) {
    return absl::InvalidArgumentError("terrain placement requires a terrain index");
  }

  const Level& level = *options.level;
  if (FindWorldLayer(level, options.active_world_layer_id) == nullptr) {
    return absl::InvalidArgumentError("viewport render requires a valid active world layer");
  }
  if (options.active_world_layer_editable && options.hidden_world_layer_ids != nullptr &&
      options.hidden_world_layer_ids->contains(options.active_world_layer_id)) {
    return absl::InvalidArgumentError("a hidden world layer cannot be viewport-editable");
  }
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width < 0.0 ||
      level.height < 0.0) {
    return absl::InvalidArgumentError("level world dimensions must be finite and non-negative");
  }
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("level tile render dimensions must be positive");
  }
  return absl::OkStatus();
}

absl::StatusOr<ViewportTab::ActiveTileset> ViewportTab::ResolveActiveTileset(
    const Level& level, const ViewportRenderOptions& options) {
  ActiveTileset active;
  if (!level.tileset_id.empty()) {
    ASSIGN_OR_RETURN(Tileset * tileset, api_.GetTileset(level.tileset_id));
    if (tileset == nullptr) {
      return absl::FailedPreconditionError("level tileset resolved to null");
    }
    active.tileset = tileset;
  }
  if (active.tileset != nullptr) {
    ASSIGN_OR_RETURN(active.texture, ResolveTilesetTexture(*active.tileset));
  }
  return active;
}

void ViewportTab::HandleFrameZoneShortcut(const Level& level, const ViewportRenderOptions& options,
                                          bool canvas_hovered) {
  if (!options.selected_zone_id.has_value() || !canvas_hovered) return;
  if (!gui_->IsKeyPressed(ImGuiKey_F, false)) return;

  const ParallaxZone* zone = FindParallaxZoneById(level.zones, *options.selected_zone_id);
  if (zone == nullptr) return;
  FrameZone(*zone);
}

absl::StatusOr<ViewportTab::RenderedScene> ViewportTab::RenderScene(
    const Level& level, const ViewportRenderOptions& options, const ActiveTileset& active,
    Vec mouse_world, bool mouse_in_level) {
  RenderedScene rendered;
  rendered.placement.interaction_world = mouse_world;
  ASSIGN_OR_RETURN(rendered.scene.active_zone, RenderParallaxBackground(level, options));

  canvas_.DrawGrid();
  RenderLevelBounds(level);

  // Resolve shared sprite resources once, then preserve layer order by issuing
  // one tiles/entities pass per visible layer.
  ASSIGN_OR_RETURN(rendered.scene.entity_sprites,
                   ResolveEntitySprites(level, options.hidden_world_layer_ids));
  for (const WorldLayer& layer : level.layers) {
    if (options.hidden_world_layer_ids != nullptr &&
        options.hidden_world_layer_ids->contains(layer.id)) {
      continue;
    }

    if (active.tileset != nullptr) {
      ASSIGN_OR_RETURN(
          TileRenderBatch tile_batch,
          ComposeLevelTileRenderBatch(level, layer, *active.tileset, active.texture, camera_,
                                      {.overlay_opacity = options.tile_overlay_opacity,
                                       .show_frame = options.show_tile_frame,
                                       .show_collision = options.show_tile_collision}));
      RETURN_IF_ERROR(renderer_.RenderTiles(tile_batch));
    }

    std::optional<BlueprintPlacementMode> selected_placement_mode;
    auto selected_entity = layer.entities.find(options.selected_entity_id);
    if (selected_entity != layer.entities.end()) {
      ASSIGN_OR_RETURN(selected_placement_mode,
                       ResolveEntityPlacementMode(api_, selected_entity->second));
    }
    ASSIGN_OR_RETURN(
        std::vector<EntityRenderItem> entity_items,
        ComposeEntityRenderItems(layer.entities, rendered.scene.entity_sprites,
                                 {.selected_entity_id = options.selected_entity_id,
                                  .show_borders = options.show_entity_borders,
                                  .overlay_opacity = options.entity_overlay_opacity,
                                  .selected_placement_mode = selected_placement_mode}));
    for (EntityRenderItem& item : entity_items) {
      if (item.overlay_opacity > 0.0f || item.show_border || item.selected) {
        rendered.scene.entity_overlays.push_back(item);
      }
      item.overlay_opacity = 0.0f;
      item.show_border = false;
      item.selected = false;
    }
    RETURN_IF_ERROR(renderer_.RenderEntities(entity_items));

    if (layer.id == options.active_world_layer_id && options.active_world_layer_editable) {
      ASSIGN_OR_RETURN(rendered.placement, RenderPlacementPreview(level, layer, options, active,
                                                                  mouse_world, mouse_in_level));
    }
  }

  RETURN_IF_ERROR(renderer_.RenderEntityOverlays(rendered.scene.entity_overlays));

  ASSIGN_OR_RETURN(
      std::vector<ZoneGizmoItem> zone_items,
      ComposeZoneGizmoItems(level.zones, camera_, options.selected_zone_id,
                            rendered.scene.active_zone.has_value()
                                ? std::optional<int>(rendered.scene.active_zone->zone_id)
                                : std::nullopt));
  renderer_.RenderZoneGizmos(zone_items);
  return rendered;
}

absl::StatusOr<ViewportTab::PlacementFrame> ViewportTab::RenderPlacementPreview(
    const Level& level, const WorldLayer& layer, const ViewportRenderOptions& options,
    const ActiveTileset& active, Vec mouse_world, bool mouse_in_level) {
  // Tile mode uses the canvas grid snap; blueprint mode snaps to the
  // blueprint's own collider/sprite dimensions, so it reports back a different
  // interaction position.
  PlacementFrame placement{.interaction_world = mouse_world};
  if (!mouse_in_level) return placement;

  if (options.placement_tile != nullptr && active.tileset != nullptr) {
    ASSIGN_OR_RETURN(
        TileRenderBatch placement_batch,
        ComposeTilePlacementBatch(*options.placement_tile, *active.tileset, active.texture,
                                  mouse_world, level.tile_render_width, level.tile_render_height));
    RETURN_IF_ERROR(renderer_.RenderTiles(placement_batch));
    return placement;
  }

  if (options.paint_terrain_id.has_value()) {
    RETURN_IF_ERROR(
        RenderTerrainGhost(layer, options, active.tileset, active.texture, mouse_world));
    return placement;
  }

  if (options.placement_blueprint == nullptr) return placement;

  ASSIGN_OR_RETURN(placement.sprite, ResolveBlueprintSprite(*options.placement_blueprint));
  ASSIGN_OR_RETURN(placement.interaction_world,
                   SnapBlueprintToGrid(mouse_world, *options.placement_blueprint,
                                       level.tile_render_width, level.tile_render_height));
  if (options.placement_blueprint->states.empty()) {
    return absl::InvalidArgumentError("blueprint placement preview requires a state");
  }
  RETURN_IF_ERROR(RenderPlacementGhost(placement.interaction_world, placement.sprite,
                                       options.placement_blueprint->states.front().placement_mode));
  return placement;
}

absl::Status ViewportTab::UpdateInteraction(Level& level, const ViewportRenderOptions& options,
                                            const PlacementFrame& placement,
                                            const SceneFrame& scene, bool mouse_in_level) {
  const bool interaction_active = gui_->IsItemActive();
  const ImGuiIO& io = gui_->GetIO();
  const bool editing = options.active_world_layer_editable;

  WorldLayer* active_layer = FindWorldLayer(level, options.active_world_layer_id);
  if (active_layer == nullptr) {
    return absl::FailedPreconditionError("active world layer disappeared during viewport frame");
  }

  ASSIGN_OR_RETURN(
      ViewportInteractionResult result,
      interaction_.Update(
          level, *active_layer,
          {
              .world_position = placement.interaction_world,
              .pointer_in_level = mouse_in_level && editing,
              .primary_pressed = editing && gui_->IsItemClicked(ImGuiMouseButton_Left),
              .primary_down = editing && interaction_active && io.MouseDown[ImGuiMouseButton_Left],
              .secondary_pressed = editing && gui_->IsItemClicked(ImGuiMouseButton_Right),
              .secondary_down =
                  editing && interaction_active && io.MouseDown[ImGuiMouseButton_Right],
          },
          {
              .paint_terrain_id = options.paint_terrain_id,
              .paint_shape = options.paint_shape,
              .terrain_index = options.terrain_index,
              .terrain_provider = options.terrain_provider,
              .paint_tile_id = options.placement_tile != nullptr
                                   ? std::optional<int>(options.placement_tile->id)
                                   : std::nullopt,
              .placement_blueprint = options.placement_blueprint,
              .placement_sprite = placement.sprite.sprite,
              .selected_entity_id = options.selected_entity_id,
              .entity_sprites = &scene.entity_sprites,
              .delete_mode = options.delete_mode,
          }));

  if (result.placed_entity.has_value()) {
    pending_entity_ = std::move(*result.placed_entity);
  }
  if (result.selected_entity_id.has_value()) {
    click_selected_entity_id_ = result.selected_entity_id;
  }
  if (result.delete_entity_id.has_value()) {
    delete_requested_entity_id_ = result.delete_entity_id;
  }
  return absl::OkStatus();
}

void ViewportTab::RenderStatusBar(const Level& level, const ViewportRenderOptions& options,
                                  const SceneFrame& scene, Vec mouse_world, float zoom) {
  const char* active_zone_name = "None";
  if (scene.active_zone.has_value()) {
    if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, scene.active_zone->zone_id);
        zone != nullptr) {
      active_zone_name = zone->name.c_str();
    }
  }

  gui_->Text("Cam: (%.0f, %.0f) | Zoom: %.2f | Mouse: (%.0f, %.0f) | Zone: %s", camera_.position.x,
             camera_.position.y, zoom, mouse_world.x, mouse_world.y, active_zone_name);

  gui_->SameLine();
  if (gui_->Button("Reset View")) {
    Reset();
  }
  gui_->SameLine();
  gui_->Checkbox("Camera Guide", &show_camera_guide_);
  RenderParallaxPreviewControls(options);
}

absl::Status ViewportTab::Render(const ViewportRenderOptions& options) {
  RETURN_IF_ERROR(ValidateRenderOptions(options));
  Level& level = *options.level;
  ReconcileParallaxPreviewMode(options);

  auto child = ScopedChild(gui_, "ViewportCanvas", ImVec2(0, 0), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y -= 25;  // Leave room for the status bar

  ApplyPendingCameraFrame(canvas_size, {.min = {0, 0}, .max = {level.width, level.height}});

  canvas_.SetWorldBounds({0, 0}, {level.width, level.height});
  canvas_.SetSnap(options.snap_to_grid);
  canvas_.SetGridSize(static_cast<float>(level.tile_render_width));
  canvas_.Begin("LevelCanvas", canvas_size, camera_);
  auto canvas_end = absl::MakeCleanup([this] { canvas_.End(); });

  // Capture canvas input before calculating any scene geometry so zoom and
  // camera movement are reflected immediately in this frame.
  canvas_.HandleInput();
  const bool canvas_hovered = gui_->IsItemHovered();
  HandleFrameZoneShortcut(level, options, canvas_hovered);

  const Vec mouse_world = canvas_.ScreenToWorld(gui_->GetMousePos());
  const bool mouse_in_level = canvas_hovered && mouse_world.x >= 0.0 && mouse_world.y >= 0.0 &&
                              mouse_world.x < level.width && mouse_world.y < level.height;
  ASSIGN_OR_RETURN(const ActiveTileset active, ResolveActiveTileset(level, options));
  ASSIGN_OR_RETURN(const RenderedScene rendered,
                   RenderScene(level, options, active, mouse_world, mouse_in_level));

  // Camera guides are editor overlays and remain visible above scene and
  // placement rendering.
  if (show_camera_guide_) {
    RenderCameraGuide();
  }

  RETURN_IF_ERROR(
      UpdateInteraction(level, options, rendered.placement, rendered.scene, mouse_in_level));

  // Zoom must be captured before End() nullifies the camera pointer.
  const float zoom = canvas_.GetZoom();
  std::move(canvas_end).Invoke();

  RenderStatusBar(level, options, rendered.scene, mouse_world, zoom);
  return absl::OkStatus();
}

absl::Status ViewportTab::RenderTerrainGhost(const WorldLayer& layer,
                                             const ViewportRenderOptions& options,
                                             const Tileset* tileset, TextureHandle texture,
                                             Vec world_pos) {
  if (tileset == nullptr) return absl::OkStatus();

  const Level& level = *options.level;
  const Terrain* terrain = options.terrain_index->FindById(*options.paint_terrain_id);
  if (terrain == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown terrain ID ", *options.paint_terrain_id));
  }

  ASSIGN_OR_RETURN(
      TileCoordinate coordinate,
      WorldToTileCoordinate(world_pos, level.tile_render_width, level.tile_render_height));
  ASSIGN_OR_RETURN(const TerrainCellKey key,
                   ComputeTerrainCellKey(level, layer, *options.terrain_index, *terrain,
                                         options.paint_shape, coordinate.x, coordinate.y));

  // The ghost resolves through the same provider the paint will, so what is
  // previewed and what lands cannot disagree. A hand-edited terrain can be
  // missing a mask; previewing nothing is better than failing the frame on
  // mouse movement, and the paint itself still reports it.
  //
  // Previewing, not resolving: hovering is a read. Asking for the tile would
  // append artwork and grow the atlas on mouse movement alone, leaving pictures
  // behind that no cell references -- and, because the grown atlas is not
  // uploaded until the frame ends, drawing one against a texture that is still
  // the old size.
  if (options.terrain_provider == nullptr) return absl::OkStatus();
  const absl::StatusOr<TerrainPreview> preview =
      options.terrain_provider->PreviewForKey(*terrain, key, coordinate.x, coordinate.y);
  if (!preview.ok()) return absl::OkStatus();

  // No tile holds this picture yet, so there is no atlas rectangle to draw. The
  // pixels are drawn as themselves instead.
  if (!preview->tile_id.has_value()) {
    if (!preview->artwork.has_value()) return absl::OkStatus();
    return RenderLooseTerrainGhost(*preview->artwork, level, world_pos);
  }

  const int tile_id = *preview->tile_id;
  const Tile* tile = nullptr;
  for (const Tile& candidate : tileset->tiles) {
    if (candidate.id == tile_id) tile = &candidate;
  }
  if (tile == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("terrain '", terrain->name, "' references missing tile ", tile_id));
  }

  ASSIGN_OR_RETURN(TileRenderBatch batch,
                   ComposeTilePlacementBatch(*tile, *tileset, texture, world_pos,
                                             level.tile_render_width, level.tile_render_height));
  return renderer_.RenderTiles(batch);
}

absl::Status ViewportTab::RenderLooseTerrainGhost(const RgbaImage& artwork, const Level& level,
                                                  Vec world_pos) {
  // Without a sink there is nowhere to put pixels the GPU has never seen, so
  // the cell previews as nothing. Appending them to the atlas to get a handle
  // is the thing this function exists to avoid.
  if (terrain_ghost_ == nullptr) return absl::OkStatus();

  ImDrawList* draw_list = canvas_.GetDrawList();
  if (draw_list == nullptr) return absl::OkStatus();

  ASSIGN_OR_RETURN(
      const TileCoordinate coordinate,
      WorldToTileCoordinate(world_pos, level.tile_render_width, level.tile_render_height));
  ASSIGN_OR_RETURN(const ImTextureID texture, terrain_ghost_->Upload(artwork));

  const ImVec2 min =
      canvas_.WorldToScreen({static_cast<double>(coordinate.x) * level.tile_render_width,
                             static_cast<double>(coordinate.y) * level.tile_render_height});
  const ImVec2 max =
      canvas_.WorldToScreen({static_cast<double>(coordinate.x + 1) * level.tile_render_width,
                             static_cast<double>(coordinate.y + 1) * level.tile_render_height});

  // Matched to the placement ghost the renderer draws for an existing tile, so
  // a cell does not change appearance the moment its artwork earns a tile.
  draw_list->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
  draw_list->AddRect(min, max, IM_COL32(100, 200, 255, 200), 0.0f, 0, 2.0f);
  return absl::OkStatus();
}

absl::Status ViewportTab::RenderPlacementGhost(Vec world_pos, const ResolvedSprite& resolved,
                                               BlueprintPlacementMode placement_mode) {
  ASSIGN_OR_RETURN(EntityRenderItem item,
                   ComposeEntityPlacementItem(world_pos, resolved, placement_mode));
  return renderer_.RenderEntities(std::span<const EntityRenderItem>(&item, 1));
}

absl::StatusOr<std::optional<ActiveParallaxZone>> ViewportTab::RenderParallaxBackground(
    const Level& level, const ViewportRenderOptions& options) {
  std::optional<ActiveParallaxZone> active =
      ResolveActiveParallaxZone(level.zones, camera_.position);

  std::optional<std::string> theme_id;
  switch (parallax_preview_mode_) {
    case ParallaxPreviewMode::kActiveZone:
      if (active.has_value()) theme_id = active->theme_id;
      break;
    case ParallaxPreviewMode::kSelectedZone:
      if (options.selected_zone_id.has_value()) {
        const ParallaxZone* selected = FindParallaxZoneById(level.zones, *options.selected_zone_id);
        if (selected != nullptr) theme_id = selected->theme_id;
      }
      break;
  }
  if (!theme_id.has_value()) return active;
  if (theme_id->empty()) return active;

  if (options.parallax_themes == nullptr) {
    return absl::FailedPreconditionError("parallax theme catalog is unavailable");
  }
  auto theme_it = std::find_if(options.parallax_themes->begin(), options.parallax_themes->end(),
                               [&](const ParallaxTheme& theme) { return theme.id == *theme_id; });
  if (theme_it == options.parallax_themes->end()) {
    return absl::FailedPreconditionError("parallax preview references a missing theme");
  }

  std::map<std::string, TextureHandle> textures;
  for (const ParallaxLayer& layer : theme_it->layers) {
    if (layer.texture_id.empty()) continue;
    if (textures.contains(layer.texture_id)) continue;

    ASSIGN_OR_RETURN(TextureHandle handle, api_.GetTextureHandle(layer.texture_id));
    if (!handle) {
      return absl::FailedPreconditionError("parallax layer texture is unavailable");
    }
    textures.emplace(layer.texture_id, handle);
  }

  ASSIGN_OR_RETURN(ParallaxRenderBatch batch,
                   ComposeParallaxRenderBatch(*theme_it, camera_, textures));
  RETURN_IF_ERROR(renderer_.RenderParallax(batch));
  return active;
}

void ViewportTab::ReconcileParallaxPreviewMode(const ViewportRenderOptions& options) {
  if (parallax_preview_mode_ == ParallaxPreviewMode::kSelectedZone &&
      !options.selected_zone_id.has_value()) {
    parallax_preview_mode_ = ParallaxPreviewMode::kActiveZone;
  }
}

void ViewportTab::RenderParallaxPreviewControls(const ViewportRenderOptions& options) {
  gui_->SameLine();
  ScopedCombo combo =
      gui_->CreateScopedCombo("Parallax View", ParallaxPreviewModeLabel(parallax_preview_mode_));
  if (!combo) return;

  if (gui_->Selectable("Active Zone", parallax_preview_mode_ == ParallaxPreviewMode::kActiveZone)) {
    parallax_preview_mode_ = ParallaxPreviewMode::kActiveZone;
  }
  if (options.selected_zone_id.has_value() &&
      gui_->Selectable("Selected Zone",
                       parallax_preview_mode_ == ParallaxPreviewMode::kSelectedZone)) {
    parallax_preview_mode_ = ParallaxPreviewMode::kSelectedZone;
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

void ViewportTab::RenderCameraGuide() {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (!draw_list) return;

  constexpr ImU32 kGuideColor = IM_COL32(80, 200, 255, 230);
  constexpr float kReticleArm = 11.0f;

  const ImVec2 center = canvas_.WorldToScreen(camera_.position);
  draw_list->AddLine(ImVec2(center.x - kReticleArm, center.y),
                     ImVec2(center.x + kReticleArm, center.y), kGuideColor, 2.0f);
  draw_list->AddLine(ImVec2(center.x, center.y - kReticleArm),
                     ImVec2(center.x, center.y + kReticleArm), kGuideColor, 2.0f);

  const GameViewSize& game_view = api_.GetConfig()->game_view;
  const std::optional<CameraGuide> guide = CalculateCameraGuide(camera_.position, game_view);
  if (!guide.has_value()) return;

  const ImVec2 guide_min = canvas_.WorldToScreen(guide->min);
  const ImVec2 guide_max = canvas_.WorldToScreen(guide->max);
  draw_list->AddRect(guide_min, guide_max, kGuideColor, 0.0f, 0, 2.0f);

  const std::string label = absl::StrFormat("Game View %dx%d", game_view.width, game_view.height);
  draw_list->AddText(ImVec2(guide_min.x + 5.0f, guide_min.y + 5.0f), kGuideColor, label.c_str());
}

absl::StatusOr<TextureHandle> ViewportTab::ResolveTilesetTexture(const Tileset& tileset) const {
  if (tileset.texture_id.empty()) return TextureHandle{};

  ASSIGN_OR_RETURN(TextureHandle handle, api_.GetTextureHandle(tileset.texture_id));
  if (!handle) {
    return absl::FailedPreconditionError("tileset texture has no renderer resource");
  }
  return handle;
}

absl::StatusOr<TextureHandle> ViewportTab::ResolveSpriteTexture(const Sprite& sprite) const {
  if (sprite.texture_id.empty()) return TextureHandle{};

  // An unloaded texture is an authoring state, not a render failure: the sprite
  // falls back to its placeholder bounds.
  absl::StatusOr<TextureHandle> handle = api_.GetTextureHandle(sprite.texture_id);
  if (!handle.ok()) return TextureHandle{};
  return *handle;
}

absl::StatusOr<SpriteLookup> ViewportTab::ResolveEntitySprites(
    const Level& level, const absl::flat_hash_set<int>* hidden_layer_ids) const {
  SpriteLookup sprites;
  for (const WorldLayer& layer : level.layers) {
    if (hidden_layer_ids != nullptr && hidden_layer_ids->contains(layer.id)) continue;
    for (const auto& [id, entity] : layer.entities) {
      if (entity.sprite_id.empty() || sprites.contains(entity.sprite_id)) continue;

      // A missing sprite is an authoring state, not a render failure: the
      // entity falls back to its placeholder bounds.
      absl::StatusOr<Sprite*> sprite = api_.GetSprite(entity.sprite_id);
      if (!sprite.ok() || *sprite == nullptr) {
        sprites.emplace(entity.sprite_id, ResolvedSprite{});
        continue;
      }
      ASSIGN_OR_RETURN(const TextureHandle texture, ResolveSpriteTexture(**sprite));
      sprites.emplace(entity.sprite_id, ResolvedSprite{.sprite = *sprite, .texture = texture});
    }
  }
  return sprites;
}

absl::StatusOr<ResolvedSprite> ViewportTab::ResolveBlueprintSprite(
    const Blueprint& blueprint) const {
  std::optional<std::string> sprite_id = blueprint.sprite_id(0);
  if (!sprite_id.has_value()) return ResolvedSprite{};

  ASSIGN_OR_RETURN(Sprite * sprite, api_.GetSprite(*sprite_id));
  if (sprite == nullptr) {
    return absl::FailedPreconditionError("placement blueprint sprite resolved to null");
  }
  ASSIGN_OR_RETURN(const TextureHandle texture, ResolveSpriteTexture(*sprite));
  return ResolvedSprite{.sprite = sprite, .texture = texture};
}

}  // namespace zebes
