#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "editor/tileset_editor/tileset_editor_model.h"

namespace zebes {

// Renders the tileset navigator. Authoring state belongs to
// TilesetEditorModel, and persistence requests are returned to TilesetEditor.
class TilesetPanel {
 public:
  enum class Action {
    kNone,
    kSave,
    kDelete,
  };

  static absl::StatusOr<std::unique_ptr<TilesetPanel>> Create(GuiInterface* gui);

  ~TilesetPanel() = default;

  absl::StatusOr<Action> RenderList(TilesetEditorModel& model);
  absl::StatusOr<Action> RenderDetails(TilesetEditorModel& model);

 private:
  explicit TilesetPanel(GuiInterface* gui);

  absl::Status RenderTilesetFields(TilesetEditorModel& model);
  absl::Status RenderTileList(TilesetEditorModel& model);

  GuiInterface* gui_;
};

}  // namespace zebes
