#include "editor/level_editor/parallax_zone_creation_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(ParallaxZoneCreationModelTest, DefaultsToTheCompleteLevelWithoutMutatingIt) {
  Level level{.name = "Cave", .width = 1920, .height = 1080};
  ParallaxZoneCreationModel model;

  ASSERT_OK(model.Begin(level));

  ASSERT_TRUE(model.active());
  EXPECT_TRUE(level.zones.empty());
  EXPECT_EQ(model.draft()->min_point, Vec(0, 0));
  EXPECT_EQ(model.draft()->max_point, Vec(1920, 1080));
}

TEST(ParallaxZoneCreationModelTest, CancelLeavesTheLevelUntouched) {
  Level level{.name = "Cave", .width = 1920, .height = 1080};
  ParallaxZoneCreationModel model;
  ASSERT_OK(model.Begin(level));

  model.Cancel();

  EXPECT_FALSE(model.active());
  EXPECT_TRUE(level.zones.empty());
}

TEST(ParallaxZoneCreationModelTest, CommitRequiresThemeAndValidBounds) {
  Level level{.name = "Cave", .width = 1920, .height = 1080};
  const std::vector<ParallaxTheme> themes = {{.id = "cave-theme", .name = "Cave"}};
  ParallaxZoneCreationModel model;
  ASSERT_OK(model.Begin(level));

  EXPECT_EQ(model.Commit(level, themes).status().code(), absl::StatusCode::kInvalidArgument);
  model.draft()->theme_id = "cave-theme";
  model.draft()->max_point.x = 0;
  EXPECT_EQ(model.Commit(level, themes).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(level.zones.empty());
}

TEST(ParallaxZoneCreationModelTest, CommitAppendsOneValidStableIdZone) {
  Level level{.name = "Cave", .width = 1920, .height = 1080};
  level.zones.push_back({.id = 7,
                         .name = "Existing",
                         .theme_id = "cave-theme",
                         .min_point = {0, 0},
                         .max_point = {100, 100}});
  const std::vector<ParallaxTheme> themes = {{.id = "cave-theme", .name = "Cave"}};
  ParallaxZoneCreationModel model;
  ASSERT_OK(model.Begin(level));
  model.draft()->theme_id = "cave-theme";

  ASSERT_OK_AND_ASSIGN(const int id, model.Commit(level, themes));

  EXPECT_EQ(id, 8);
  ASSERT_EQ(level.zones.size(), 2);
  EXPECT_EQ(level.zones.back().theme_id, "cave-theme");
  EXPECT_FALSE(model.active());
}

}  // namespace
}  // namespace zebes
