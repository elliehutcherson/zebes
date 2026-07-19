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
};

}  // namespace zebes
