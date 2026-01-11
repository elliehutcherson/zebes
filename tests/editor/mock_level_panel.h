#pragma once

#include "editor/level_editor/level_panel_interface.h"
#include "gmock/gmock.h"

namespace zebes {

class MockLevelPanel : public LevelPanelInterface {
 public:
  MOCK_METHOD(absl::StatusOr<LevelResult>, Render, (std::optional<Level> & level), (override));
};

}  // namespace zebes
