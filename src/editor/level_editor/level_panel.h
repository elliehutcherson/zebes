#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/level_editor/level_panel_interface.h"
#include "objects/level.h"

namespace zebes {

struct LevelCounters {
  int render_list = 0;
  int render_details = 0;
  int create = 0;
  int edit = 0;
  int del = 0;
  int save = 0;
  int back = 0;
};

class LevelPanel : public LevelPanelInterface {
 public:
  struct Options {
    Api* api;
  };

  static absl::StatusOr<std::unique_ptr<LevelPanel>> Create(Options options);
  ~LevelPanel() = default;

  enum Op : uint8_t { kLevelCreate, kLevelEdit, kLevelSave, kLevelDelete, kLevelBack };

  // Renders the level panel UI.
  // Returns kChanged if the level was modified.
  absl::StatusOr<LevelResult> Render(std::optional<Level>& level) override;

  absl::StatusOr<LevelResult> HandleOp(std::optional<Level>& level, Op op);

  const LevelCounters& GetCounters() const { return counters_; }

  void TestOnlySetSelectedIndex(int index) { selected_index_ = index; }
  int TestOnly_GetSelectedIndex() const { return selected_index_; }

 private:
  explicit LevelPanel(Options options);

  // Renders the list of level layers and CRUD buttons.
  absl::StatusOr<LevelResult> RenderList(std::optional<Level>& level);

  // Renders the details view for creating or editing a layer.
  absl::StatusOr<LevelResult> RenderDetails(std::optional<Level>& level);

  void RefreshLevelCache();

  Api& api_;

  int selected_index_ = 0;
  std::vector<Level> level_cache_;
  LevelCounters counters_;
};

}  // namespace zebes
