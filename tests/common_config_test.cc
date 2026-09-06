#include <filesystem>
#include <fstream>

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

TEST(WindowConfigTest, RefusesAMissingFieldRatherThanDefaultingIt) {
  nlohmann::json serialized = WindowConfig{};
  serialized.erase("high_dpi");

  EXPECT_THROW(serialized.get<WindowConfig>(), nlohmann::json::exception);
}

TEST(EngineConfigTest, RoundTripsConfiguredGameView) {
  EngineConfig source;
  source.game_view = {.width = 320, .height = 180};

  const nlohmann::json serialized = source;
  const EngineConfig parsed = serialized.get<EngineConfig>();

  EXPECT_EQ(parsed.game_view.width, 320);
  EXPECT_EQ(parsed.game_view.height, 180);
}

TEST(EngineConfigTest, RefusesAMissingGameViewRatherThanDefaultingIt) {
  nlohmann::json serialized = EngineConfig{};
  serialized.erase("game_view");

  EXPECT_THROW(serialized.get<EngineConfig>(), nlohmann::json::exception);
}

// A corrupt config must come back as a status. Load owns the nlohmann API, so
// it is the one place allowed to catch, and nothing above it sees an exception.
TEST(EngineConfigTest, LoadReportsMalformedJsonAsAStatus) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "zebes-malformed-config.json";
  std::ofstream out(path);
  out << "{ \"window\": ";
  out.close();

  const absl::StatusOr<EngineConfig> config = EngineConfig::Load(path.string());

  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  std::filesystem::remove(path);
}

TEST(EngineConfigTest, LoadReportsAMissingFieldAsAStatus) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "zebes-incomplete-config.json";
  nlohmann::json serialized = EngineConfig{};
  serialized.erase("fps");
  std::ofstream out(path);
  out << serialized.dump();
  out.close();

  const absl::StatusOr<EngineConfig> config = EngineConfig::Load(path.string());

  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  std::filesystem::remove(path);
}

TEST(EngineConfigTest, RejectsInvalidGameView) {
  EngineConfig config;
  config.game_view = {.width = 0, .height = 360};

  EXPECT_EQ(config.Validate().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
