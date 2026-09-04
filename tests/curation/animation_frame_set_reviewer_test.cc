#include "curation/animation_frame_set_reviewer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "api_mock.h"
#include "common/image_digest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {
using ::testing::Return;

constexpr int kFrameSize = 8;
constexpr int kFrameCount = 3;
constexpr int kContactLineY = 6;

// Three packed 8 x 8 frames. Each holds a two-pixel column at a different
// horizontal position and height, so adjacent frames are never duplicates and
// every subject bound is exactly predictable. Frames 0 and 1 are planted on the
// contact line; frame 2 is lifted clear of it.
RgbaImage TexturePixels() {
  RgbaImage texture{
      .width = kFrameSize * kFrameCount,
      .height = kFrameSize,
      .pixels = std::vector<uint8_t>(kFrameSize * kFrameCount * kFrameSize * 4, 0),
  };
  const std::array<int, kFrameCount> left = {2, 3, 4};
  const std::array<int, kFrameCount> top = {2, 1, 0};
  const std::array<int, kFrameCount> bottom = {kContactLineY, kContactLineY, kContactLineY - 3};
  for (int frame = 0; frame < kFrameCount; ++frame) {
    for (int y = top[frame]; y < bottom[frame]; ++y) {
      for (int x = left[frame]; x < left[frame] + 2; ++x) {
        const size_t offset =
            ((static_cast<size_t>(y) * texture.width) + frame * kFrameSize + x) * 4;
        texture.pixels[offset + 0] = 200;
        texture.pixels[offset + 1] = 90;
        texture.pixels[offset + 2] = 60;
        texture.pixels[offset + 3] = 255;
      }
    }
  }
  return texture;
}

std::vector<SpriteFrame> PackedFrames() {
  std::vector<SpriteFrame> frames;
  frames.reserve(kFrameCount);
  for (int index = 0; index < kFrameCount; ++index) {
    frames.push_back({
        .index = index,
        .texture_x = index * kFrameSize,
        .texture_y = 0,
        .texture_w = kFrameSize,
        .texture_h = kFrameSize,
        .render_w = kFrameSize,
        .render_h = kFrameSize,
        .frames_per_cycle = 4,
        .offset_x = -kFrameSize / 2,
        .offset_y = -kContactLineY,
    });
  }
  return frames;
}

AnimationFrameSetRecipe TestRecipe(const RgbaImage& texture) {
  AnimationFrameSetPipelineConfig pipeline{
      .sheet =
          AnimationFrameSetSheetLayout{
              .grid_x = 0,
              .grid_y = 0,
              .cell_width = kFrameSize,
              .cell_height = kFrameSize,
              .column_gap = 0,
              .row_gap = 0,
              .columns = kFrameCount,
              .rows = 1,
          },
      .output_width = kFrameSize,
      .output_height = kFrameSize,
      .origin_x = kFrameSize / 2,
      .origin_y = kContactLineY,
      .contact_line_y = kContactLineY,
      .render_scale = 1,
      .contact_tolerance = 1,
      .minimum_visible_pixels = 1,
      .maximum_horizontal_anchor_drift = 2,
      .maximum_vertical_anchor_drift = 2,
      .packing_columns = kFrameCount,
      .playback_mode = SpritePlaybackMode::kLoop,
      .frames_per_cycle = {4, 4, 4},
      .planted_frames = {true, true, false},
  };
  absl::StatusOr<std::string> digest = RgbaImageDigest(texture);
  return {
      .id = "recipe-id",
      .name = "Run Left",
      .source_artwork_id = "source-id",
      .style =
          AnimationFrameSetStyle{
              .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
              .matte = {255, 0, 255, 255},
              .transparent_matte_distance = 12.0f,
              .opaque_matte_distance = 100.0f,
              .alpha_threshold = 128,
              .palette = {{200, 90, 60, 255}},
          },
      .pipeline = std::move(pipeline),
      .texture_id = "texture-id",
      .sprite_id = "sprite-id",
      .blueprint_id = "blueprint-id",
      .blueprint_bindings = {{
          .state_key = "run-left",
          .previous_sprite_id = "placeholder-id",
      }},
      .expected_frames = PackedFrames(),
      .final_pixel_digest = digest.value(),
      .pipeline_version = kAnimationFrameSetPipelineVersion,
  };
}

struct Fixture {
  RgbaImage texture_pixels = TexturePixels();
  AnimationFrameSetRecipe recipe = TestRecipe(texture_pixels);
  Sprite sprite{
      .id = "sprite-id",
      .name = "Run Left",
      .texture_id = "texture-id",
      .frames = PackedFrames(),
      .playback_mode = SpritePlaybackMode::kLoop,
  };
  Blueprint blueprint{
      .id = "blueprint-id",
      .name = "Player",
      .states = {{
          .key = "run-left",
          .name = "Run Left",
          .collider_id = "collider-id",
          .sprite_id = "sprite-id",
      }},
  };
  Texture texture{.id = "texture-id", .name = "Run Left", .path = "run-left.png"};
};

void ExpectLookups(MockApi& api, Fixture& fixture) {
  EXPECT_CALL(api, GetAnimationFrameSetRecipe(fixture.recipe.id))
      .WillRepeatedly(Return(&fixture.recipe));
  EXPECT_CALL(api, GetTexture(fixture.texture.id)).WillRepeatedly(Return(&fixture.texture));
  EXPECT_CALL(api, GetSprite(fixture.sprite.id)).WillRepeatedly(Return(&fixture.sprite));
  EXPECT_CALL(api, GetBlueprint(fixture.blueprint.id)).WillRepeatedly(Return(&fixture.blueprint));
  EXPECT_CALL(api, ReadTexturePixels(fixture.texture.id))
      .WillRepeatedly(Return(fixture.texture_pixels));
}

bool HasFinding(const CurationReview& review, std::string_view code) {
  for (const CurationFinding& finding : review.findings) {
    if (finding.code == code) return true;
  }
  return false;
}

bool HasArtifact(const CurationReview& review, std::string_view id) {
  for (const CurationArtifact& artifact : review.artifacts) {
    if (artifact.id == id) return true;
  }
  return false;
}

TEST(AnimationFrameSetReviewerTest, PublishesRecipeIdentityWithSharedFrameSetEvidence) {
  Fixture fixture;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       reviewer.Review(api, {.asset_id = fixture.recipe.id}));

  EXPECT_EQ(review.kind, "animation-frame-set");
  EXPECT_EQ(review.asset_id, "recipe-id");
  EXPECT_TRUE(HasArtifact(review, "animation-strip"));
  EXPECT_TRUE(HasArtifact(review, "contact-sheet"));
  EXPECT_TRUE(HasArtifact(review, "alignment-overlay"));
  EXPECT_TRUE(HasArtifact(review, "loop-closure"));
  EXPECT_EQ(review.metadata.at("recipe").at("id"), "recipe-id");
  EXPECT_EQ(review.metadata.at("blueprint_id"), "blueprint-id");
  EXPECT_EQ(review.metadata.at("source_artwork_id"), "source-id");
  EXPECT_EQ(review.metadata.at("registration").size(), kFrameCount);
  EXPECT_EQ(review.metadata.at("registration").at(0).at("vertical_anchor_drift"), 0);
  EXPECT_TRUE(review.findings.empty());
}

TEST(AnimationFrameSetReviewerTest, WarnsWhenAPlantedFrameMissesTheContactLine) {
  Fixture fixture;
  // Lift the second frame's subject clear of the contact line without touching
  // any definition, exactly as a bad re-import would.
  for (int y = 0; y < kFrameSize; ++y) {
    for (int x = 0; x < kFrameSize; ++x) {
      const size_t offset =
          ((static_cast<size_t>(y) * fixture.texture_pixels.width) + kFrameSize + x) * 4;
      fixture.texture_pixels.pixels[offset + 3] = y >= 1 && y < 3 && x >= 3 && x < 5 ? 255 : 0;
    }
  }
  ASSERT_OK_AND_ASSIGN(fixture.recipe.final_pixel_digest, RgbaImageDigest(fixture.texture_pixels));
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       reviewer.Review(api, {.asset_id = fixture.recipe.id}));

  EXPECT_TRUE(HasFinding(review, "planted-frame-misses-contact"));
}

TEST(AnimationFrameSetReviewerTest, WarnsWhenASubjectDriftsFromTheDeclaredOrigin) {
  Fixture fixture;
  fixture.recipe.pipeline.maximum_horizontal_anchor_drift = 0;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       reviewer.Review(api, {.asset_id = fixture.recipe.id}));

  EXPECT_TRUE(HasFinding(review, "horizontal-anchor-drift"));
}

TEST(AnimationFrameSetReviewerTest, RejectsTexturePixelsThatDriftedFromTheRecipeDigest) {
  Fixture fixture;
  fixture.texture_pixels.pixels[3] = 255;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  const absl::Status status = reviewer.Review(api, {.asset_id = fixture.recipe.id}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("recipe digest"));
}

TEST(AnimationFrameSetReviewerTest, RejectsSpriteFramesThatNoLongerMatchTheRecipe) {
  Fixture fixture;
  fixture.sprite.frames.back().frames_per_cycle = 9;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  const absl::Status status = reviewer.Review(api, {.asset_id = fixture.recipe.id}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("no longer match the recipe"));
}

TEST(AnimationFrameSetReviewerTest, RejectsPlaybackModeThatNoLongerMatchesTheRecipe) {
  Fixture fixture;
  fixture.sprite.playback_mode = SpritePlaybackMode::kHoldLast;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  const absl::Status status = reviewer.Review(api, {.asset_id = fixture.recipe.id}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("playback mode"));
}

TEST(AnimationFrameSetReviewerTest, RejectsBlueprintStateThatNoLongerBindsTheSprite) {
  Fixture fixture;
  fixture.blueprint.states.front().sprite_id = "some-other-sprite";
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  const absl::Status status = reviewer.Review(api, {.asset_id = fixture.recipe.id}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("no longer binds"));
}

TEST(AnimationFrameSetReviewerTest, RejectsAMissingBoundBlueprintState) {
  Fixture fixture;
  fixture.blueprint.states.front().key = "idle-left";
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  const absl::Status status = reviewer.Review(api, {.asset_id = fixture.recipe.id}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("missing Blueprint state"));
}

TEST(AnimationFrameSetReviewerTest, RepeatsIdenticalEvidenceForAnUnchangedFrameSet) {
  Fixture fixture;
  MockApi api;
  ExpectLookups(api, fixture);

  AnimationFrameSetReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview first, reviewer.Review(api, {.asset_id = fixture.recipe.id}));
  ASSERT_OK_AND_ASSIGN(CurationReview second,
                       reviewer.Review(api, {.asset_id = fixture.recipe.id}));

  ASSERT_EQ(first.artifacts.size(), second.artifacts.size());
  EXPECT_EQ(first.metadata, second.metadata);
  for (size_t index = 0; index < first.artifacts.size(); ++index) {
    EXPECT_EQ(first.artifacts[index].relative_path, second.artifacts[index].relative_path);
    EXPECT_EQ(first.artifacts[index].image.pixels, second.artifacts[index].image.pixels);
  }
}

}  // namespace
}  // namespace zebes
