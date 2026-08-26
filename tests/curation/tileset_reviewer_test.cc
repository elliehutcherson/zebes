#include "curation/tileset_reviewer.h"

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

RgbaImage AtlasPixels() {
  RgbaImage atlas{
      .width = 32,
      .height = 16,
      .pixels = std::vector<uint8_t>(32 * 16 * 4, 255),
  };
  for (int y = 0; y < atlas.height; ++y) {
    for (int x = 0; x < atlas.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * atlas.width + x) * 4;
      atlas.pixels[offset + 0] = x < 16 ? 200 : 30;
      atlas.pixels[offset + 1] = x < 16 ? 40 : 180;
      atlas.pixels[offset + 2] = 80;
    }
  }
  return atlas;
}

Tileset TestTileset() {
  return {
      .id = "tileset-id",
      .name = "Cave",
      .texture_id = "texture-id",
      .tile_width = 16,
      .tile_height = 16,
      .tiles =
          {
              {.id = 1,
               .name = "Floor",
               .source_x = 0,
               .source_y = 0,
               .shape = TileShape::kFullBlock},
              {.id = 2,
               .name = "Slope",
               .source_x = 16,
               .source_y = 0,
               .shape = TileShape::kSlope45FloorTallRight},
          },
  };
}

TEST(TilesetReviewerTest, EmitsAtlasPerTileFramesAndPlacementContext) {
  Tileset tileset = TestTileset();
  Texture texture{.id = tileset.texture_id, .name = tileset.name, .path = "atlas.png"};
  RgbaImage atlas = AtlasPixels();
  MockApi api;
  EXPECT_CALL(api, GetTileset(tileset.id)).WillOnce(Return(&tileset));
  EXPECT_CALL(api, GetTexture(texture.id)).WillOnce(Return(&texture));
  EXPECT_CALL(api, ReadTexturePixels(texture.id)).WillOnce(Return(atlas));

  TilesetReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = tileset.id}));

  EXPECT_EQ(review.kind, "tileset");
  ASSERT_EQ(review.artifacts.size(), 4);
  EXPECT_EQ(review.artifacts.at(0).id, "atlas");
  EXPECT_EQ(review.artifacts.at(1).id, "placement-context");
  EXPECT_EQ(review.artifacts.at(2).image.width, tileset.tile_width);
  EXPECT_EQ(review.artifacts.at(3).metadata.at("shape"), "kSlope45FloorTallRight");
  EXPECT_EQ(review.metadata.at("tiles").size(), 2);
}

TEST(TilesetReviewerTest, RejectsATileOutsideTheManagedAtlas) {
  Tileset tileset = TestTileset();
  tileset.tiles.back().source_x = 32;
  Texture texture{.id = tileset.texture_id, .name = tileset.name, .path = "atlas.png"};
  MockApi api;
  EXPECT_CALL(api, GetTileset(tileset.id)).WillOnce(Return(&tileset));
  EXPECT_CALL(api, GetTexture(texture.id)).WillOnce(Return(&texture));
  EXPECT_CALL(api, ReadTexturePixels(texture.id)).WillOnce(Return(AtlasPixels()));

  TilesetReviewer reviewer;
  EXPECT_TRUE(absl::IsFailedPrecondition(reviewer.Review(api, {.asset_id = tileset.id}).status()));
}

}  // namespace
}  // namespace zebes
