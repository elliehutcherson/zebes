#pragma once

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/parallax_layout.h"
#include "editor/level_editor/viewport_interaction.h"
#include "editor/level_editor/viewport_renderer.h"
#include "objects/blueprint.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

enum class ParallaxPreviewMode {
  kActiveZone,
  kSelectedTheme,
  kSelectedLayer,
};

// Per-frame inputs to ViewportTab::Render(). All fields are transient — they
// are not stored between frames.
struct ViewportRenderOptions {
  // Level to render and interact with. Must outlive the Render() call.
  Level* level = nullptr;
  // Terrain painted while dragging; empty = not in terrain-painting mode.
  std::optional<int> paint_terrain_id;
  // Terrain tables for the level's tileset. Must be non-null when
  // paint_terrain_id is set.
  const TerrainIndex* terrain_index = nullptr;
  // Blueprint to place on the next canvas click; nullptr = no placement mode.
  const Blueprint* placement_blueprint = nullptr;
  // Entity currently selected by the LevelEditor; kInvalidId = none.
  uint64_t selected_entity_id = Entity::kInvalidId;
  // Whether entity placement and movement should snap to the grid relative to origin.
  bool snap_to_grid = true;
  // Whether to draw bounding-box borders around all entities in the viewport.
  bool show_entity_borders = false;
  // When true, a right-click on the canvas deletes the entity under the cursor.
  bool delete_mode = false;
  // Tile to paint when non-null; nullptr = not in tile-painting mode. It must
  // belong to the level's own tileset, which is the only one a frame resolves:
  // levels store bare tile IDs, so a tile from elsewhere would be stored as
  // whatever the level's tileset numbers the same.
  const Tile* placement_tile = nullptr;
  // Whether to draw a thin border around every placed tile cell in the viewport.
  bool show_tile_frame = false;
  // Whether to draw the collision-shape overlay on every placed tile.
  bool show_tile_collision = false;
  // Blue overlay alpha [0,1] drawn on top of every tile cell. 0 = off.
  float tile_overlay_opacity = 0.0f;
  // Yellow overlay alpha [0,1] drawn on top of every entity. 0 = off.
  float entity_overlay_opacity = 0.0f;
  // Zone selected in the editor navigator. Used only for gizmo highlighting
  // and frame-selected behavior; it does not override runtime activation.
  std::optional<int> selected_zone_id;
  // Theme selected in the navigator and available for explicit preview.
  std::optional<int> selected_parallax_theme_id;
  // Layer selected within selected_parallax_theme_id and available for isolation.
  std::optional<int> selected_parallax_layer_index;
};

class ViewportTab {
 public:
  explicit ViewportTab(Api& api, GuiInterface* gui);

  // Main render loop for the viewport tab.
  absl::Status Render(const ViewportRenderOptions& options);

  // Resets the viewport camera and transient interaction state.
  void Reset();

  // Requests that the next viewport frame center and fit this zone.
  void FrameZone(const ParallaxZone& zone);

  // Returns the entity placed this frame (if any), then clears it.
  // The returned entity has its sprite pointer resolved via the Api.
  std::optional<Entity> TakeNewEntity();

  // Returns the result of a canvas click for selection, then clears it.
  // - nullopt: no click occurred this frame.
  // - Entity::kInvalidId: click on empty space (deselect).
  // - any other value: click on that entity id (select).
  std::optional<uint64_t> TakeClickSelection();

  // Returns the ID of an entity the user right-clicked to delete (if any), then clears it.
  // Only populated when delete_mode is true in the render options.
  std::optional<uint64_t> TakeDeleteRequest();

 private:
  friend class ViewportTabTestPeer;

  // The tileset a frame draws with, resolved once together with its atlas so
  // the scene and the placement preview cannot disagree about either.
  struct ActiveTileset {
    const Tileset* tileset = nullptr;
    TextureHandle texture;
  };

  // What composing the scene leaves behind for the later phases: the zone the
  // status bar names, and the sprite lookup interaction reuses for picking.
  struct SceneFrame {
    std::optional<ActiveParallaxZone> active_zone;
    SpriteLookup entity_sprites;
  };

  // What the placement preview settles for the interaction phase. The preview
  // owns the snapping, so the world position it commits to is the one clicks
  // must use.
  struct PlacementFrame {
    Vec interaction_world;
    ResolvedSprite sprite;
  };

  // Rejects option combinations that would render an undefined frame.
  static absl::Status ValidateRenderOptions(const ViewportRenderOptions& options);

  // Prefers the tileset being painted from, falling back to the level's own.
  absl::StatusOr<ActiveTileset> ResolveActiveTileset(const Level& level,
                                                     const ViewportRenderOptions& options);

  // Frames the selected zone when the user presses F over the canvas.
  void HandleFrameZoneShortcut(const Level& level, const ViewportRenderOptions& options,
                               bool canvas_hovered);

  // Draws background, grid, bounds, tiles, entities, and zone gizmos.
  absl::StatusOr<SceneFrame> RenderScene(const Level& level,
                                         const ViewportRenderOptions& options,
                                         const ActiveTileset& active);

  // Draws whichever preview the current mode calls for, and settles where a
  // click would land.
  absl::StatusOr<PlacementFrame> RenderPlacementPreview(const Level& level,
                                                        const ViewportRenderOptions& options,
                                                        const ActiveTileset& active,
                                                        Vec mouse_world, bool mouse_in_level);

  // Translates ImGui canvas state into a platform-neutral interaction frame and
  // records whatever the controller reports back.
  absl::Status UpdateInteraction(Level& level, const ViewportRenderOptions& options,
                                 const PlacementFrame& placement, const SceneFrame& scene,
                                 bool mouse_in_level);

  // Renders the camera/zoom/zone readout and its inline controls. Runs after the
  // canvas closes, so it takes the zoom captured beforehand.
  void RenderStatusBar(const Level& level, const ViewportRenderOptions& options,
                       const SceneFrame& scene, Vec mouse_world, float zoom);

  // Composes and renders a semi-transparent placement preview at world_pos.
  absl::Status RenderPlacementGhost(Vec world_pos, const ResolvedSprite& resolved);

  // Draws the tile the hovered cell would actually receive, so the brush shows
  // its resolved edge or corner artwork rather than a generic swatch.
  absl::Status RenderTerrainGhost(const ViewportRenderOptions& options, const Tileset* tileset,
                                  TextureHandle texture, Vec world_pos);

  // Resolves and renders the requested environment, returning the actual active zone.
  absl::StatusOr<std::optional<ActiveParallaxZone>> RenderParallaxBackground(
      const Level& level, const ViewportRenderOptions& options);

  // Falls back to active-zone rendering when the requested selection no longer exists.
  void ReconcileParallaxPreviewMode(const ViewportRenderOptions& options);

  // Renders explicit active-zone, selected-theme, and selected-layer controls.
  void RenderParallaxPreviewControls(const ViewportRenderOptions& options);

  // Applies a queued frame request once the current viewport and world
  // dimensions are known.
  void ApplyPendingCameraFrame(const ImVec2& viewport_size, VisibleWorldBounds world_bounds);

  // Renders the boundaries of the level and potentially the camera start box.
  void RenderLevelBounds(const Level& level);

  // Draws the logical game view centered on the editor camera, plus a
  // constant-screen-size reticle at the camera position.
  void RenderCameraGuide();

  // Resolves the blueprint collider and delegates platform-neutral grid snapping.
  absl::StatusOr<Vec> SnapBlueprintToGrid(Vec mouse_world, const Blueprint& blueprint,
                                          const Sprite* sprite, int tile_render_w,
                                          int tile_render_h) const;

  // Resolves the blueprint's optional managed sprite for preview and placement.
  absl::StatusOr<ResolvedSprite> ResolveBlueprintSprite(const Blueprint& blueprint) const;

  // Resolves a sprite's atlas handle. An unset or unloaded texture yields an
  // invalid handle so the caller draws a placeholder instead of failing.
  absl::StatusOr<TextureHandle> ResolveSpriteTexture(const Sprite& sprite) const;

  // Resolves every referenced entity sprite once for the current frame.
  // Entities store only IDs, so rendering and picking share this lookup rather
  // than reading pointers off the level definition.
  absl::StatusOr<SpriteLookup> ResolveEntitySprites(
      const std::map<uint64_t, Entity>& entities) const;

  // Resolves the tileset's platform-neutral atlas handle. An empty texture ID
  // deliberately produces an invalid handle for placeholder-only rendering.
  absl::StatusOr<TextureHandle> ResolveTilesetTexture(const Tileset& tileset) const;

  Api& api_;
  GuiInterface* gui_;
  Canvas canvas_;
  Camera camera_;
  ViewportRenderer renderer_;
  ViewportInteractionController interaction_;
  bool show_camera_guide_ = true;
  ParallaxPreviewMode parallax_preview_mode_ = ParallaxPreviewMode::kActiveZone;
  std::optional<VisibleWorldBounds> pending_camera_frame_;

  std::optional<Entity> pending_entity_;
  std::optional<uint64_t> click_selected_entity_id_;
  std::optional<uint64_t> delete_requested_entity_id_;
};

}  // namespace zebes
