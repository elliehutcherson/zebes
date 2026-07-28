#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/texture_preview.h"
#include "objects/tileset.h"

namespace zebes {

// Lists the terrain brushes defined on a tileset and lets the user pick one.
//
// Terrains resolve their artwork from a painted cell's neighbourhood, so the
// swatch shows the fully surrounded tile: the piece a large filled region is
// mostly made of, and the most recognisable single image of a material.
class TerrainPalettePanel {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<TerrainPalettePanel>> Create(Options options);

  // Renders the tileset selector and terrain swatch list. Must be called each
  // frame.
  absl::Status Render();

  // Returns the selected terrain's ID, or empty when none is selected.
  std::optional<int> GetSelectedTerrainId() const;

  // Returns the tileset owning the selected terrain, or nullptr.
  const Tileset* GetSelectedTileset() const { return selected_tileset_; }

  void ClearSelection() { selected_terrain_id_.reset(); }

 private:
  friend class TerrainPalettePanelTestPeer;

  explicit TerrainPalettePanel(Options options);

  // Renders one selectable swatch per terrain in the active tileset.
  absl::Status RenderTerrainList(const AtlasBinding& atlas);

  Api& api_;
  GuiInterface* gui_;
  TexturePreviewRenderer texture_preview_;

  // Stable pointer into the Api's tileset storage, matching how the tile
  // palette holds its selection.
  const Tileset* selected_tileset_ = nullptr;
  std::optional<int> selected_terrain_id_;
};

}  // namespace zebes
