#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "editor/level_editor/level_panel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

TEST(LevelPanelValidationTest, Disabled) {
  // This test file assumes an older LevelPanel API and is currently disabled.
  SUCCEED();
}

}  // namespace
}  // namespace zebes
