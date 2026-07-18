#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "objects/tileset.h"

namespace zebes {

// Renders a scrollable thumbnail grid of tiles from the selected tileset.
// Provides toggles for per-tile visual overlays in the viewport (frame borders
// and collision shapes). The selected tile is surfaced to the LevelEditor so
// the viewport can enter tile-painting mode.
class TilePalettePanel {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<TilePalettePanel>> Create(Options options);

  // Renders the tile palette. Must be called each frame.
  // tile_render_width/height are the level's configured world-space render dimensions,
  // used to scale thumbnails proportionally so the palette reflects the rendered tile shape.
  absl::Status Render(int tile_render_width, int tile_render_height);

  // Returns the currently selected tile, or nullptr if none.
  const Tile* GetSelectedTile() const { return selected_tile_; }

  // Returns the tileset owning the selected tile, or nullptr if none selected.
  const Tileset* GetSelectedTileset() const { return selected_tileset_; }

  // Returns whether a thin border should be drawn around each placed tile cell
  // in the viewport (useful for invisible tiles).
  bool GetShowTileFrame() const { return show_tile_frame_; }

  // Returns whether the collision-shape overlay should be drawn on each placed
  // tile in the viewport.
  bool GetShowTileCollision() const { return show_tile_collision_; }

  // Returns the blue overlay opacity [0,1] applied to tile cells in the
  // viewport and to tile thumbnails in the palette. 0 = off, 1 = fully blue.
  float GetTileOverlayOpacity() const { return tile_overlay_opacity_; }

  // Deselects the current tile (e.g. after pressing Escape).
  void ClearSelection() { selected_tile_ = nullptr; }

 private:
  friend class TilePalettePanelTestPeer;

  explicit TilePalettePanel(Options options);

  // Handles a click on a tile thumbnail: toggles selection or picks the tile
  // by obtaining a stable pointer from the Api.
  absl::Status HandleTileClick(int tile_id, bool is_selected);

  // Renders the scrollable thumbnail grid for the current tileset.
  // tile_render_w/h are forwarded from Render() to scale thumbnail proportions.
  absl::Status RenderTileGrid(void* texture_handle, int tex_w, int tex_h,
                              int tile_render_w, int tile_render_h, float overlay_opacity);

  Api& api_;
  GuiInterface* gui_;

  // Stable pointers into the Api's tileset storage; valid as long as the Api
  // is alive and the tileset has not been modified.
  const Tileset* selected_tileset_ = nullptr;
  const Tile* selected_tile_ = nullptr;

  bool show_tile_frame_ = true;
  bool show_tile_collision_ = false;
  float tile_overlay_opacity_ = 0.0f;
};

}  // namespace zebes
