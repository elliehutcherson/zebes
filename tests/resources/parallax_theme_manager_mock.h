#pragma once

#include "gmock/gmock.h"
#include "resources/parallax_theme_manager.h"

namespace zebes {

class ParallaxThemeManagerMock : public ParallaxThemeManager {
 public:
  ParallaxThemeManagerMock() : ParallaxThemeManager() {}

  MOCK_METHOD(absl::Status, LoadAllThemes, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTheme, (ParallaxTheme), (override));
  MOCK_METHOD(absl::Status, SaveTheme, (const ParallaxTheme&), (override));
  MOCK_METHOD(absl::StatusOr<ParallaxTheme*>, GetTheme, (const std::string&), (override));
  MOCK_METHOD(std::vector<ParallaxTheme>, GetAllThemes, (), (const, override));
  MOCK_METHOD(absl::Status, DeleteTheme, (const std::string&), (override));
};

}  // namespace zebes
