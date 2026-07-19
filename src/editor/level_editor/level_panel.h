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

  GuiInterface* gui_;
};

}  // namespace zebes
