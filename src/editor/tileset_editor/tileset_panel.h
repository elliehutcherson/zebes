#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "objects/texture.h"
#include "objects/tileset.h"

namespace zebes {

// Manages the navigator column for the TilesetEditor. Owns the tileset/texture
// caches and tile selection state.
//
// RenderList    - shows the sorted tileset list with Create / Edit / Delete.
//                 Used when no tileset is active.
// RenderDetails - shows tileset fields + tile list. Used when a tileset is
//                 active (replaces the list in the navigator column).
// GetSelectedTile - returns a pointer to the currently selected tile for the
//                   inspector column to render, or nullptr if none selected.
class TilesetPanel {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  // Factory. Returns an error if api or gui is null.
  static absl::StatusOr<std::unique_ptr<TilesetPanel>> Create(Options options);

  ~TilesetPanel() = default;

  // Operations that mutate the active tileset or trigger API calls.
  enum class Op {
    kCreate,
    kEdit,
    kSave,
    kDelete,
    kBack,
    kTileAdd,
    kTileDelete,
  };

  // Dispatches the requested operation. Caller passes the active tileset by
  // reference so the panel can set, reset, or update it as needed.
  absl::Status HandleOp(std::optional<Tileset>& tileset, Op op);

  // Renders the tileset list with Create / Edit / Delete buttons.
  absl::Status RenderList(std::optional<Tileset>& tileset);

  // Renders tileset fields + tile list for the active tileset.
  // Caller must ensure tileset.has_value() before calling.
  absl::Status RenderDetails(std::optional<Tileset>& tileset);

  // Returns a mutable pointer to the selected tile within tileset, or nullptr
  // if no tile is selected or the index is out of range.
  Tile* GetSelectedTile(Tileset& tileset);

  // Exposes the texture cache so TilesetEditor::RenderViewport can resolve
  // SDL textures without re-querying the API each frame.
  const std::vector<Texture>& GetTextures() const { return texture_cache_; }

 private:
  explicit TilesetPanel(Options options);

  absl::Status Init();

  void RefreshTilesetCache();
  void RefreshTextureCache();

  // Renders the top-level tileset fields: name, texture picker, dimensions.
  absl::Status RenderTilesetFields(Tileset& ts);

  // Renders the tile list sub-section: Add/Delete buttons + selectable rows.
  absl::Status RenderTileList(std::optional<Tileset>& tileset);

  Api& api_;
  GuiInterface* gui_;

  int selected_index_ = -1;       // Index into tileset_cache_.
  int selected_tile_index_ = -1;  // Index into the active tileset's tiles.

  std::vector<Tileset> tileset_cache_;  // Sorted by name.
  std::vector<Texture> texture_cache_;
};

}  // namespace zebes
