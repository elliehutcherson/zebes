#pragma once

#include <memory>
#include <optional>
#include <string>

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

  // Renders either the Delete button or the confirmation that replaces it.
  // Returns kDelete only once the user has confirmed.
  absl::StatusOr<Action> RenderDeleteTilesetControl(TilesetEditorModel& model);

  absl::Status RenderTilesetFields(TilesetEditorModel& model);
  absl::Status RenderTileList(TilesetEditorModel& model);

  // Renders the terrains this tileset carries, plus Detect. Authoring a terrain
  // happens in the Terrain tab, which has room for the preview that tuning one
  // requires; what belongs here is only what needs the tiles beside it.
  absl::Status RenderTerrainList(TilesetEditorModel& model);

  // Assigns the selected tile to a terrain for neighbour-masking only. This is
  // the manual route for hand-drawn set-pieces; generated slope units arrive
  // already registered.
  absl::Status RenderTerrainMembership(TilesetEditorModel& model, const Tileset& tileset);

  // Clears every pending confirmation. Called whenever the user does something
  // other than answer the question in front of them, so a Confirm click can
  // never land on a target the user has since moved away from.
  void CancelPendingConfirmations();

  // Outcome of the last detect, shown until the next attempt.
  std::string terrain_status_;

  // Destructive actions confirm in place rather than through a modal: the
  // button is replaced by a question and a Confirm/Cancel pair until answered.
  // Each field holds the target awaiting confirmation, so a stale confirmation
  // cannot be applied to whatever happens to be selected later.
  std::optional<std::string> confirm_delete_tileset_;
  std::optional<int> confirm_delete_terrain_;
  bool confirm_discard_ = false;

  GuiInterface* gui_;
};

}  // namespace zebes
