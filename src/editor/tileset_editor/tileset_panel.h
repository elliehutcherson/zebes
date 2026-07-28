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

  // Renders the terrain list plus the import and detect controls. Defining a
  // 47-tile terrain by hand is impractical, so these are the authoring entry
  // points for the terrain brush.
  absl::Status RenderTerrainList(TilesetEditorModel& model);

  // Assigns the selected tile to a terrain for neighbour-masking only. This is
  // the manual route for hand-drawn set-pieces; generated slope units arrive
  // already registered.
  absl::Status RenderTerrainMembership(TilesetEditorModel& model, const Tileset& tileset);

  // Path typed into the manifest import field, persisted across frames.
  std::string manifest_path_;
  // Outcome of the last import or detect, shown until the next attempt.
  std::string terrain_status_;

  GuiInterface* gui_;
};

}  // namespace zebes
