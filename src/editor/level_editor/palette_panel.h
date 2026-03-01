#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/blueprint_palette_panel.h"
#include "editor/level_editor/tile_palette_panel.h"
#include "objects/blueprint.h"
#include "objects/tileset.h"

namespace zebes {

// Top-level palette panel that hosts both the blueprint palette and the tile
// palette, with a mode-toggle to switch between them. LevelEditor owns one
// PalettePanel and uses its pass-through getters to build ViewportRenderOptions
// each frame.
class PalettePanel {
 public:
  enum class Mode { kBlueprints, kTiles };

  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
    // Optional pre-constructed sub-panels (used in tests to inject mocks).
    std::unique_ptr<BlueprintPalettePanel> blueprint_panel;
    std::unique_ptr<TilePalettePanel> tile_panel;
  };

  static absl::StatusOr<std::unique_ptr<PalettePanel>> Create(Options options);

  // Renders the mode-toggle buttons followed by the active sub-panel.
  // tile_render_width/height are the level's configured render dimensions, forwarded to the tile
  // palette so thumbnails reflect the tile's rendered shape in the world.
  absl::Status Render(int tile_render_width, int tile_render_height);

  // --- Blueprint-mode getters ---
  // Returns the selected blueprint, or nullptr when not in blueprint mode.
  const Blueprint* GetSelectedBlueprint() const;
  bool GetSnapToGrid() const;
  bool GetShowEntityBorders() const;

  // --- Tile-mode getters ---
  // Returns the selected tile, or nullptr when not in tile mode.
  const Tile* GetSelectedTile() const;
  // Returns the tileset owning the selected tile, or nullptr when not in tile mode.
  const Tileset* GetSelectedTileset() const;
  bool GetShowTileFrame() const;
  bool GetShowTileCollision() const;
  float GetTileOverlayOpacity() const;
  float GetEntityOverlayOpacity() const;

  // Returns true when delete mode is active: right-clicking an entity in the
  // viewport will delete it instead of deselecting.
  bool GetDeleteMode() const { return delete_mode_; }

  Mode GetMode() const { return mode_; }

 private:
  friend class PalettePanelTestPeer;

  PalettePanel() = default;

  Mode mode_ = Mode::kBlueprints;
  bool delete_mode_ = false;
  std::unique_ptr<BlueprintPalettePanel> blueprint_panel_;
  std::unique_ptr<TilePalettePanel> tile_panel_;
  GuiInterface* gui_ = nullptr;
};

}  // namespace zebes
