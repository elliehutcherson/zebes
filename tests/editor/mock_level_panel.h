#pragma once

#include "editor/level_editor/level_panel_interface.h"
#include "gmock/gmock.h"

namespace zebes {

class MockLevelPanel : public LevelPanelInterface {
 public:
  MOCK_METHOD(absl::StatusOr<LevelPanelEvent>, RenderList, (LevelPanelModel & model), (override));
  MOCK_METHOD(absl::StatusOr<LevelPanelEvent>, RenderDetails, (LevelPanelModel & model), (override));
};

}  // namespace zebes
