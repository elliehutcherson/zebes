#include "common/config.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

TEST(WindowConfigTest, RoundTripsPlatformNeutralFields) {
  WindowConfig source{
      .title = "Game",
      .centered = false,
      .x = 120,
      .y = 80,
      .width = 1920,
      .height = 1080,
      .fullscreen = true,
      .resizable = false,
      .high_dpi = false,
  };

  const nlohmann::json serialized = source;
  const WindowConfig parsed = serialized.get<WindowConfig>();

  EXPECT_EQ(parsed.title, source.title);
  EXPECT_EQ(parsed.centered, source.centered);
  EXPECT_EQ(parsed.x, source.x);
  EXPECT_EQ(parsed.y, source.y);
  EXPECT_EQ(parsed.width, source.width);
  EXPECT_EQ(parsed.height, source.height);
  EXPECT_EQ(parsed.fullscreen, source.fullscreen);
  EXPECT_EQ(parsed.resizable, source.resizable);
  EXPECT_EQ(parsed.high_dpi, source.high_dpi);
}

TEST(WindowConfigTest, MigratesLegacySdlValuesWithoutExposingSdlTypes) {
  const nlohmann::json legacy = {
      {"title", "Legacy"},
      {"xpos", 0x2FFF0000u},
      {"ypos", 0x2FFF0000u},
      {"width", 1400},
      {"height", 640},
      {"flags", 0x00002020u},
  };

  const WindowConfig parsed = legacy.get<WindowConfig>();

  EXPECT_TRUE(parsed.centered);
  EXPECT_FALSE(parsed.fullscreen);
  EXPECT_TRUE(parsed.resizable);
  EXPECT_TRUE(parsed.high_dpi);
}

TEST(EngineConfigTest, RoundTripsConfiguredGameView) {
  EngineConfig source;
  source.game_view = {.width = 320, .height = 180};

  const nlohmann::json serialized = source;
  const EngineConfig parsed = serialized.get<EngineConfig>();

  EXPECT_EQ(parsed.game_view.width, 320);
  EXPECT_EQ(parsed.game_view.height, 180);
}

TEST(EngineConfigTest, DefaultsGameViewWhenLoadingOlderConfig) {
  EngineConfig source;
  nlohmann::json serialized = source;
  serialized.erase("game_view");

  const EngineConfig parsed = serialized.get<EngineConfig>();

  EXPECT_EQ(parsed.game_view.width, 640);
  EXPECT_EQ(parsed.game_view.height, 360);
}

TEST(EngineConfigTest, RejectsInvalidGameView) {
  EngineConfig config;
  config.game_view = {.width = 0, .height = 360};

  EXPECT_EQ(config.Validate().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
