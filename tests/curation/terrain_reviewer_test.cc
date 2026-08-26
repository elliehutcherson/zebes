#include "curation/terrain_reviewer.h"

#include <cstdint>
#include <vector>

#include "api_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "terrain/terrain_style.h"

namespace zebes {
namespace {
using ::testing::Return;

struct TerrainGraph {
  TerrainRecipe recipe;
  Tileset tileset;
  Texture texture;
  RgbaImage atlas;
};

TerrainGraph TestGraph() {
  TerrainGenConfig config = BuiltInTerrainPresets().front().config;
  config.variant_period = 1;
  const int tile_size = config.tile_size;
  Terrain terrain{
      .id = 7,
      .name = "Cave Rock",
      .scheme = TerrainScheme::kDerived,
      .variant_period = 1,
      .derived_tiles = {{
          .tile_id = 1,
          .key = {.shape = TileShape::kFullBlock},
      }},
  };
  Tileset tileset{
      .id = "tileset-id",
      .name = "Cave",
      .texture_id = "texture-id",
      .tile_width = tile_size,
      .tile_height = tile_size,
      .tiles = {{.id = 1,
                 .name = "Cave Rock Full",
                 .source_x = 0,
                 .source_y = 0,
                 .shape = TileShape::kFullBlock}},
      .terrains = {terrain},
  };
  return {
      .recipe =
          {
              .id = "recipe-id",
              .name = terrain.name,
              .tileset_id = tileset.id,
              .texture_id = tileset.texture_id,
              .terrain_id = terrain.id,
              .source_preset = BuiltInTerrainPresets().front().name,
              .config = config,
          },
      .tileset = std::move(tileset),
      .texture = {.id = "texture-id", .name = "Cave", .path = "terrain.png"},
      .atlas = {.width = tile_size,
                .height = tile_size,
                .pixels = std::vector<uint8_t>(tile_size * tile_size * 4, 255)},
  };
}

TEST(TerrainReviewerTest, EmitsSlopeMatrixAtlasAndOwnedFrames) {
  TerrainGraph graph = TestGraph();
  MockApi api;
  EXPECT_CALL(api, GetTerrainRecipe(graph.recipe.id)).WillOnce(Return(&graph.recipe));
  EXPECT_CALL(api, GetTileset(graph.tileset.id)).WillOnce(Return(&graph.tileset));
  EXPECT_CALL(api, GetTexture(graph.texture.id)).WillOnce(Return(&graph.texture));
  EXPECT_CALL(api, ReadTexturePixels(graph.texture.id)).WillOnce(Return(graph.atlas));

  TerrainReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = graph.recipe.id}));

  EXPECT_EQ(review.kind, "terrain");
  ASSERT_EQ(review.artifacts.size(), 3);
  EXPECT_EQ(review.artifacts.at(0).id, "slope-matrix");
  EXPECT_EQ(review.artifacts.at(1).id, "atlas");
  EXPECT_EQ(review.artifacts.at(2).metadata.at("shape"), "kFullBlock");
  EXPECT_EQ(review.metadata.at("recipe").at("id"), graph.recipe.id);
  EXPECT_EQ(review.metadata.at("slope_bands").size(), 8);
}

TEST(TerrainReviewerTest, RejectsRecipeAndTilesetTextureDisagreement) {
  TerrainGraph graph = TestGraph();
  graph.recipe.texture_id = "different-texture";
  MockApi api;
  EXPECT_CALL(api, GetTerrainRecipe(graph.recipe.id)).WillOnce(Return(&graph.recipe));
  EXPECT_CALL(api, GetTileset(graph.tileset.id)).WillOnce(Return(&graph.tileset));

  TerrainReviewer reviewer;
  EXPECT_TRUE(
      absl::IsFailedPrecondition(reviewer.Review(api, {.asset_id = graph.recipe.id}).status()));
}

}  // namespace
}  // namespace zebes
