#pragma once

#include "editor/level_editor/level_panel.h"
#include "gmock/gmock.h"

namespace zebes {

class MockLevelPanel : public ILevelPanel {
 public:
  MOCK_METHOD(absl::StatusOr<LevelResult>, Render, (), (override));
  MOCK_METHOD(absl::Status, Attach, (const std::string& id), (override));
  MOCK_METHOD(void, Detach, (), (override));
  MOCK_METHOD(const LevelCounters&, GetCounters, (), (const, override));
};

}  // namespace zebes
