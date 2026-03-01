#pragma once

#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "gmock/gmock.h"

namespace zebes {

class MockLevelPanel : public LevelPanelInterface {
 public:
  MOCK_METHOD(void, RefreshCache, (), (override));
  MOCK_METHOD(absl::Status, RenderList, (std::optional<Level> & level, SelectionState& selection),
              (override));
  MOCK_METHOD(absl::Status, RenderDetails,
              (std::optional<Level> & level, SelectionState& selection), (override));
};

}  // namespace zebes
