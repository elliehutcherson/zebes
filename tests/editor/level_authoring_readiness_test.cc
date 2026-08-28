#include "editor/level_editor/level_authoring_readiness.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(LevelAuthoringReadinessTest, SeparatesSavePlacementAndParallaxRequirements) {
  const Level level{.name = "Cave", .width = 1920, .height = 1088};

  const LevelAuthoringReadiness readiness =
      EvaluateLevelAuthoringReadiness(level, false, true, false, true);

  EXPECT_TRUE(readiness.can_save());
  EXPECT_FALSE(readiness.can_place());
  EXPECT_FALSE(readiness.can_add_parallax_zone());
}

TEST(LevelAuthoringReadinessTest, ExplainsZeroAreaAndTileAlignment) {
  Level level{.name = "Cave", .width = 0, .height = 1080};
  LevelAuthoringReadiness readiness =
      EvaluateLevelAuthoringReadiness(level, false, true, true, true);
  ASSERT_FALSE(readiness.save_blockers.empty());
  EXPECT_EQ(readiness.save_blockers.front(), "World width and height must be positive.");

  level.width = 1919;
  readiness = EvaluateLevelAuthoringReadiness(level, false, true, true, true);
  ASSERT_FALSE(readiness.save_blockers.empty());
  EXPECT_EQ(readiness.save_blockers.front(),
            "World dimensions must be multiples of the tile size.");
}

TEST(LevelAuthoringReadinessTest, ReadyLevelSupportsEveryWorkflow) {
  const Level level{
      .name = "Cave",
      .tileset_id = "cave-tiles",
      .width = 1920,
      .height = 1088,
  };

  const LevelAuthoringReadiness readiness =
      EvaluateLevelAuthoringReadiness(level, true, true, true, true);

  EXPECT_TRUE(readiness.can_save());
  EXPECT_TRUE(readiness.can_place());
  EXPECT_TRUE(readiness.can_add_parallax_zone());
}

TEST(LevelAuthoringReadinessTest, MissingZoneThemeBlocksSaveAndFurtherZoneCreation) {
  const Level level{
      .name = "Cave",
      .width = 1920,
      .height = 1088,
      .zones = {{.id = 0,
                 .name = "Cave Zone",
                 .theme_id = "missing-theme",
                 .min_point = {0, 0},
                 .max_point = {1920, 1088}}},
  };

  const LevelAuthoringReadiness readiness =
      EvaluateLevelAuthoringReadiness(level, true, true, true, false);

  EXPECT_FALSE(readiness.can_save());
  EXPECT_FALSE(readiness.can_add_parallax_zone());
  EXPECT_EQ(readiness.save_blockers.front(),
            "Every parallax zone must reference an available theme.");
}

TEST(LevelAuthoringReadinessTest, IntersectingZoneFadesBlockSaveWithGeometryError) {
  const Level level{
      .name = "Cave",
      .width = 1920,
      .height = 1088,
      .zones =
          {
              {.id = 1,
               .name = "Top Left",
               .theme_id = "theme-tl",
               .min_point = {0, 0},
               .max_point = {100, 100},
               .fade_length = {20, 20}},
              {.id = 2,
               .name = "Top Right",
               .theme_id = "theme-tr",
               .min_point = {100, 0},
               .max_point = {200, 100},
               .fade_length = {20, 20}},
              {.id = 3,
               .name = "Bottom Left",
               .theme_id = "theme-bl",
               .min_point = {0, 100},
               .max_point = {100, 200},
               .fade_length = {20, 20}},
              {.id = 4,
               .name = "Bottom Right",
               .theme_id = "theme-br",
               .min_point = {100, 100},
               .max_point = {200, 200},
               .fade_length = {20, 20}},
          },
  };

  const LevelAuthoringReadiness readiness =
      EvaluateLevelAuthoringReadiness(level, true, true, true, true);

  ASSERT_FALSE(readiness.can_save());
  ASSERT_FALSE(readiness.save_blockers.empty());
  EXPECT_TRUE(absl::StrContains(readiness.save_blockers.front(), "Top Left"));
  EXPECT_TRUE(absl::StrContains(readiness.save_blockers.front(), "Top Right"));
}

}  // namespace
}  // namespace zebes
