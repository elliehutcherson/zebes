#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/level_panel_model.h"

namespace zebes {

// Renders level catalog and detail state, reporting persistence intents to the
// containing editor. It does not access the application API.
class LevelPanel : public LevelPanelInterface {
 public:
  static absl::StatusOr<std::unique_ptr<LevelPanel>> Create(GuiInterface* gui);
  ~LevelPanel() override = default;

  absl::StatusOr<LevelPanelEvent> RenderList(LevelPanelModel& model) override;
  absl::StatusOr<LevelPanelEvent> RenderDetails(LevelPanelModel& model) override;

 private:
  explicit LevelPanel(GuiInterface* gui);

  // Renders the tileset the level's tile IDs are resolved against. Changing it
  // is the only supported way to repoint a level, which is why it lives beside
  // the level's other properties rather than in a palette.
  absl::Status RenderTilesetField(LevelPanelModel& model, const Level& level);

  // Renders the confirmation shown when the switch would strand placed tiles.
  absl::Status RenderTilesetChangeConfirmation(LevelPanelModel& model);

  GuiInterface* gui_;
};

}  // namespace zebes
