#include "curation/parallax_theme_reviewer.h"

#include <string>
#include <vector>

#include "api_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {
using ::testing::Return;

TEST(ParallaxThemeReviewerTest, ProducesRouteLayerAndSeamArtifactsWithoutSdlOrImGui) {
  ParallaxTheme theme{
      .id = "theme-id",
      .name = "Theme",
      .layers = {{
          .name = "Repeated",
          .scroll_factor = {0.5, 0.5},
          .repeat_period = {128, 128},
          .elements = {{
              .id = 0,
              .name = "Tile",
              .texture_id = "texture-id",
          }},
      }},
  };
  Level level{
      .id = "level-id",
      .name = "Level",
      .width = 2000,
      .height = 1000,
      .zones = {{
          .id = 7,
          .name = "Zone",
          .theme_id = theme.id,
          .min_point = {0, 0},
          .max_point = {2000, 1000},
      }},
  };
  const RgbaImage pixels{
      .width = 128,
      .height = 128,
      .pixels = std::vector<uint8_t>(128 * 128 * 4, 255),
  };
  MockApi api;
  EXPECT_CALL(api, GetParallaxTheme(theme.id)).Times(3).WillRepeatedly(Return(&theme));
  EXPECT_CALL(api, GetAllLevels()).Times(2).WillRepeatedly(Return(std::vector<Level>{level}));
  EXPECT_CALL(api, ReadTexturePixels("texture-id")).Times(2).WillRepeatedly(Return(pixels));

  ParallaxThemeReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = theme.id}));

  EXPECT_EQ(review.kind, "parallax-theme");
  EXPECT_EQ(review.artifacts.size(), 12);
  for (const CurationArtifact& artifact : review.artifacts) {
    EXPECT_TRUE(artifact.image.IsValid());
  }

  ParallaxTheme candidate = theme;
  candidate.name = "Candidate Theme";
  ASSERT_OK_AND_ASSIGN(
      CurationReview candidate_review,
      reviewer.ReviewCandidate(api, {.asset_id = theme.id}, ParallaxThemeToJson(candidate)));
  EXPECT_TRUE(candidate_review.metadata.at("candidate"));
  EXPECT_EQ(candidate_review.asset_name, candidate.name);
  EXPECT_EQ(candidate_review.metadata.at("definition").at("name"), candidate.name);

  EXPECT_CALL(api, UpdateParallaxTheme(candidate)).WillOnce(Return(absl::OkStatus()));
  EXPECT_OK(reviewer.CommitCandidate(api, {.asset_id = theme.id}, ParallaxThemeToJson(candidate)));
}

}  // namespace
}  // namespace zebes
