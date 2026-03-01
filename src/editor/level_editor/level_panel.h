#pragma once

#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "objects/level.h"

namespace zebes {

class LevelPanel : public LevelPanelInterface {
 public:
  struct Options {
    Api* api;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<LevelPanel>> Create(Options options);
  ~LevelPanel() = default;

  enum class Op { kLevelCreate, kLevelDelete, kLevelEdit, kLevelSave, kLevelBack };

  absl::Status HandleOp(std::optional<Level>& level, SelectionState& selection, Op op);

  void RefreshCache() override;
  absl::Status RenderList(std::optional<Level>& level, SelectionState& selection) override;
  absl::Status RenderDetails(std::optional<Level>& level, SelectionState& selection) override;

 private:
  friend class LevelPanelTestPeer;

  explicit LevelPanel(Options options);

  Api& api_;
  GuiInterface* gui_;

  int selected_index_ = -1;
  std::vector<Level> level_cache_;
};

}  // namespace zebes
