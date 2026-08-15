#pragma once

#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "editor/confirm_prompt.h"
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
    // Deleting a tile is reported rather than applied, because a level may have
    // painted it and only the containing editor can ask the Api who has.
    kDeleteTile,
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
  absl::StatusOr<Action> RenderTileList(TilesetEditorModel& model);

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

  // One prompt per destructive action. Terrains share a single prompt because
  // only one row's question can be open at a time, and ConfirmPrompt drops it
  // as soon as it is rendered against a different terrain.
  ConfirmPrompt delete_tileset_prompt_;
  ConfirmPrompt delete_terrain_prompt_;
  ConfirmPrompt discard_edits_prompt_;

  GuiInterface* gui_;
};

}  // namespace zebes
