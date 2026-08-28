#include "curation/level_review_route.h"

#include <array>
#include <string>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

Level HorizontalLevel() {
  return {
      .id = "level-id",
      .name = "Level",
      .width = 16384,
      .height = 1280,
      .spawn_point = {256, 512},
      .zones = {{
          .id = 7,
          .name = "Gallery",
          .theme_id = "theme-id",
          .min_point = {0, 0},
          .max_point = {16384, 1280},
      }},
  };
}

TEST(LevelReviewRouteTest, PlansOverlappingHorizontalRoutesAtSupportedZooms) {
  const Level level = HorizontalLevel();
  constexpr GameViewSize kGameView{.width = 960, .height = 540};
  constexpr std::array<double, 3> kZooms = {0.5, 1.0, 2.0};

  ASSERT_OK_AND_ASSIGN(const std::vector<LevelReviewRoute> routes,
                       PlanLevelReviewRoutes(level, kGameView, kZooms));

  ASSERT_EQ(routes.size(), 3);
  EXPECT_EQ(routes[0].id, "zone-007-track-00");
  EXPECT_TRUE(routes[0].horizontal);
  EXPECT_DOUBLE_EQ(routes[0].centers.min.x, 960);
  EXPECT_DOUBLE_EQ(routes[0].centers.max.x, 15424);
  EXPECT_DOUBLE_EQ(routes[0].centers.min.y, 540);
  EXPECT_DOUBLE_EQ(routes[0].samples.front().camera.position.x, 960);
  EXPECT_DOUBLE_EQ(routes[0].samples.back().camera.position.x, 15424);
  EXPECT_EQ(routes[0].samples.front().key_roles, (std::vector<std::string>{"start"}));
  EXPECT_EQ(routes[0].samples.back().key_roles, (std::vector<std::string>{"end"}));
  for (size_t index = 1; index < routes[0].samples.size(); ++index) {
    EXPECT_LE(
        routes[0].samples[index].camera.position.x - routes[0].samples[index - 1].camera.position.x,
        960);
  }
}

TEST(LevelReviewRouteTest, AddsOnlyTheTracksNeededToCoverSeparatedWorldContent) {
  Level level{
      .id = "level-id",
      .name = "Two Floors",
      .width = 1280,
      .height = 720,
      .spawn_point = {100, 200},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"}},
      .zones = {{
          .id = 3,
          .name = "Room",
          .theme_id = "theme-id",
          .min_point = {0, 0},
          .max_point = {1280, 720},
      }},
  };
  level.layers.front().entities.emplace(1, Entity{.id = 1, .transform = {.position = {100, 600}}});
  constexpr GameViewSize kGameView{.width = 640, .height = 360};
  constexpr std::array<double, 1> kZooms = {2.0};

  ASSERT_OK_AND_ASSIGN(const std::vector<LevelReviewRoute> routes,
                       PlanLevelReviewRoutes(level, kGameView, kZooms));

  ASSERT_EQ(routes.size(), 2);
  EXPECT_EQ(routes[0].track_index, 0);
  EXPECT_EQ(routes[1].track_index, 1);
  EXPECT_LE(routes[0].centers.min.y - kGameView.height / (2.0 * kZooms[0]), 200);
  EXPECT_GE(routes[0].centers.min.y + kGameView.height / (2.0 * kZooms[0]), 200);
  EXPECT_LE(routes[1].centers.min.y - kGameView.height / (2.0 * kZooms[0]), 600);
  EXPECT_GE(routes[1].centers.min.y + kGameView.height / (2.0 * kZooms[0]), 600);
  for (size_t index = 1; index < routes[1].samples.size(); ++index) {
    EXPECT_LE(
        routes[1].samples[index].camera.position.x - routes[1].samples[index - 1].camera.position.x,
        kGameView.width / kZooms[0]);
  }
}

TEST(LevelReviewRouteTest, UsesVerticalCenterlineForTallZones) {
  Level level{
      .id = "level-id",
      .name = "Shaft",
      .width = 640,
      .height = 1920,
      .spawn_point = {320, 200},
      .zones = {{
          .id = 2,
          .name = "Shaft",
          .theme_id = "theme-id",
          .min_point = {0, 0},
          .max_point = {640, 1920},
      }},
  };
  constexpr GameViewSize kGameView{.width = 320, .height = 240};
  constexpr std::array<double, 1> kZooms = {1.0};

  ASSERT_OK_AND_ASSIGN(const std::vector<LevelReviewRoute> routes,
                       PlanLevelReviewRoutes(level, kGameView, kZooms));

  ASSERT_EQ(routes.size(), 1);
  EXPECT_FALSE(routes.front().horizontal);
  EXPECT_DOUBLE_EQ(routes.front().centers.min.x, 320);
  EXPECT_DOUBLE_EQ(routes.front().centers.min.y, 120);
  EXPECT_DOUBLE_EQ(routes.front().centers.max.y, 1800);
}

TEST(LevelReviewRouteTest, AddsFadeEvidenceAtAnAdjacentZoneBoundary) {
  Level level{
      .id = "level-id",
      .name = "Gallery",
      .width = 960,
      .height = 320,
      .spawn_point = {100, 160},
      .zones =
          {
              {.id = 1,
               .name = "Left",
               .theme_id = "left-theme",
               .min_point = {0, 0},
               .max_point = {480, 320},
               .fade_length = {40, 0}},
              {.id = 2,
               .name = "Right",
               .theme_id = "right-theme",
               .min_point = {480, 0},
               .max_point = {960, 320},
               .fade_length = {20, 0}},
          },
  };
  constexpr GameViewSize kGameView{.width = 320, .height = 160};
  constexpr std::array<double, 1> kZooms = {1.0};

  ASSERT_OK_AND_ASSIGN(const std::vector<LevelReviewRoute> routes,
                       PlanLevelReviewRoutes(level, kGameView, kZooms));

  ASSERT_EQ(routes.size(), 2);
  bool found_fade_middle = false;
  bool found_boundary = false;
  for (const LevelReviewCameraSample& sample : routes.front().samples) {
    found_fade_middle |= std::find(sample.key_roles.begin(), sample.key_roles.end(),
                                   "fade-middle") != sample.key_roles.end();
    found_boundary |= std::find(sample.key_roles.begin(), sample.key_roles.end(),
                                "fade-boundary") != sample.key_roles.end();
  }
  EXPECT_TRUE(found_fade_middle);
  EXPECT_TRUE(found_boundary);
}

TEST(LevelReviewRouteTest, RejectsAViewportThatCannotFitInsideTheLevel) {
  Level level = HorizontalLevel();
  level.width = 640;
  level.height = 320;
  level.spawn_point = {256, 160};
  level.zones.front().max_point = {640, 320};
  constexpr GameViewSize kGameView{.width = 960, .height = 540};
  constexpr std::array<double, 1> kZooms = {0.5};

  EXPECT_EQ(PlanLevelReviewRoutes(level, kGameView, kZooms).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
