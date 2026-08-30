#include "curation/sprite_reviewer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "api_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {
using ::testing::Return;

RgbaImage TexturePixels() {
  RgbaImage texture{
      .width = 32,
      .height = 16,
      .pixels = std::vector<uint8_t>(32 * 16 * 4, 255),
  };
  for (int y = 0; y < texture.height; ++y) {
    for (int x = 0; x < texture.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * texture.width + x) * 4;
      texture.pixels[offset + 0] = x < 16 ? 220 : 40;
      texture.pixels[offset + 1] = x < 16 ? 80 : 180;
      texture.pixels[offset + 2] = 100;
    }
  }
  return texture;
}

Sprite TestSprite() {
  return {
      .id = "sprite-id",
      .name = "Crawler",
      .texture_id = "texture-id",
      .frames =
          {
              {.index = 0,
               .texture_x = 0,
               .texture_y = 0,
               .texture_w = 16,
               .texture_h = 16,
               .render_w = 24,
               .render_h = 24,
               .frames_per_cycle = 8,
               .offset_x = -12,
               .offset_y = -24},
              {.index = 1,
               .texture_x = 16,
               .texture_y = 0,
               .texture_w = 16,
               .texture_h = 16,
               .render_w = 24,
               .render_h = 24,
               .frames_per_cycle = 8,
               .offset_x = -12,
               .offset_y = -24},
          },
  };
}

TEST(SpriteReviewerTest, EmitsNativeEnlargedAndAnimationArtifacts) {
  Sprite sprite = TestSprite();
  Texture texture{.id = sprite.texture_id, .name = sprite.name, .path = "crawler.png"};
  RgbaImage pixels = TexturePixels();
  MockApi api;
  EXPECT_CALL(api, GetSprite(sprite.id)).WillOnce(Return(&sprite));
  EXPECT_CALL(api, GetTexture(texture.id)).WillOnce(Return(&texture));
  EXPECT_CALL(api, ReadTexturePixels(texture.id)).WillOnce(Return(pixels));

  SpriteReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = sprite.id}));

  EXPECT_EQ(review.kind, "sprite");
  ASSERT_EQ(review.artifacts.size(), 5);
  EXPECT_EQ(review.artifacts.front().id, "animation-strip");
  EXPECT_EQ(review.artifacts.at(1).image.width, 16);
  EXPECT_EQ(review.artifacts.at(2).image.width, 256);
  EXPECT_EQ(review.metadata.at("playback_mode"), "loop");
  EXPECT_EQ(review.metadata.at("frames").size(), 2);
}

TEST(SpriteReviewerTest, RejectsDuplicateFrameIdentity) {
  Sprite sprite = TestSprite();
  sprite.frames.back().index = sprite.frames.front().index;
  Texture texture{.id = sprite.texture_id, .name = sprite.name, .path = "crawler.png"};
  MockApi api;
  EXPECT_CALL(api, GetSprite(sprite.id)).WillOnce(Return(&sprite));
  EXPECT_CALL(api, GetTexture(texture.id)).WillOnce(Return(&texture));
  EXPECT_CALL(api, ReadTexturePixels(texture.id)).WillOnce(Return(TexturePixels()));

  SpriteReviewer reviewer;
  EXPECT_TRUE(absl::IsFailedPrecondition(reviewer.Review(api, {.asset_id = sprite.id}).status()));
}

}  // namespace
}  // namespace zebes
