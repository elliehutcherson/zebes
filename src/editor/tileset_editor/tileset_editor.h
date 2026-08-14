#pragma once

#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/tileset_editor/tile_panel.h"
#include "editor/tileset_editor/tileset_editor_model.h"
#include "editor/tileset_editor/tileset_panel.h"
#include "objects/camera.h"
#include "objects/tileset.h"

namespace zebes {

class TilesetEditor {
 public:
  static absl::StatusOr<std::unique_ptr<TilesetEditor>> Create(Api* api, GuiInterface* gui);

  ~TilesetEditor() = default;

  absl::Status Render();

 private:
  TilesetEditor(Api* api, GuiInterface* gui);

  absl::Status Init();
  void RefreshCatalogs();
  absl::Status SaveActiveTileset();
  absl::Status DeleteSelectedTileset();
  absl::Status HandlePanelAction(TilesetPanel::Action action);

  // Renders the tileset list and management controls (left column).
  absl::Status RenderNavigator();

  // Renders the main editing viewport (middle column).
  absl::Status RenderViewport();

  // Turns clicks and drags over the atlas into tile edits, and draws the
  // highlight for whichever gesture is in progress. Must be called after
  // Canvas::HandleInput, which leaves the canvas as the current ImGui item.
  //
  // A release covering one cell re-points the selected tile, exactly as
  // clicking always has. A release covering more adds a tile per cell. Acting
  // on release rather than on press is what lets the two share one gesture:
  // there is no way to tell them apart before the button comes up.
  absl::Status HandleAtlasInteraction(ImDrawList* draw_list, int texture_width,
                                      int texture_height);

  // Applies a finished gesture: one cell re-points the selected tile, more than
  // one adds a tile per cell.
  absl::Status CommitAtlasGesture(AtlasCell anchor, AtlasCell end);

  // Outlines an inclusive rectangle of atlas cells on the canvas.
  void DrawCellRegion(ImDrawList* draw_list, AtlasCell first, AtlasCell last, ImU32 fill,
                      ImU32 border) const;

  // Renders the selected tile's properties in the right column.
  absl::Status RenderInspector();

  Api* api_;
  GuiInterface* gui_;

  Canvas canvas_;
  Camera camera_;

  std::unique_ptr<TilesetPanel> tileset_panel_;
  std::unique_ptr<TilePanel> tile_panel_;
  TilesetEditorModel model_;
  std::optional<std::string> error_message_;

  // The cell a drag began on and the cell it currently covers. Both are set
  // only while the mouse is held over the atlas.
  std::optional<AtlasCell> drag_anchor_;
  std::optional<AtlasCell> drag_current_;

  // What the last atlas gesture did, shown in the status bar under the canvas.
  std::string viewport_status_;
};

}  // namespace zebes
