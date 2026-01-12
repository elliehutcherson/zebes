#pragma once

#include "editor/level_editor/parallax_panel.h"
#include "gmock/gmock.h"

namespace zebes {

class MockParallaxPanel : public IParallaxPanel {
 public:
  MOCK_METHOD(absl::StatusOr<ParallaxResult>, Render, (Level&), (override));
  MOCK_METHOD(std::optional<std::string>, GetTexture, (), (const, override));
};

}  // namespace zebes
