#include "editor/terrain_editor/terrain_creation.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_mask.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;

// Small and cheap: these tests care about what gets registered and saved, not
// about how the artwork looks.
TerrainGenConfig SmallConfig() {
  TerrainGenConfig config;
  config.tile_size = 8;
  config.supersample = 1;
  config.variant_period = 1;
  return config;
}

// A manifest describing artwork that already exists, the way compose_blob47
// emits one.
std::string ManifestFor(int variant_count) {
  QuadrantSheet sheet;
  sheet.quadrant_size = 4;
  sheet.variant_count = variant_count;
  sheet.image.width = sheet.quadrant_size * kQuadrantStateCount * variant_count;
  sheet.image.height = sheet.quadrant_size * kQuadrantCount;
  sheet.image.pixels.assign(static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 255);

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(sheet);
  EXPECT_TRUE(atlas.ok()) << atlas.status();
  return WriteBlob47Manifest(*atlas);
}

class TerrainCreationTest : public ::testing::Test {
 protected:
  NiceMock<MockApi> api_;
};

TEST_F(TerrainCreationTest, GeneratingWritesArtworkAndSavesATileset) {
  EXPECT_CALL(api_, CreateTextureFromPixels("meadow", _, _, _))
      .WillOnce([](const std::string&, int width, int height, absl::Span<const uint8_t> pixels) {
        EXPECT_EQ(width, kBlob47Columns * 8);
        EXPECT_EQ(pixels.size(), static_cast<size_t>(width) * height * 4);
        return std::string("texture-id");
      });

  Tileset saved;
  EXPECT_CALL(api_, CreateTileset(_)).WillOnce([&](Tileset tileset) {
    saved = std::move(tileset);
    return std::string("tileset-id");
  });

  absl::StatusOr<CreatedTerrain> created =
      CreateGeneratedTerrainTileset(api_, "meadow", SmallConfig());
  ASSERT_TRUE(created.ok()) << created.status();
  EXPECT_EQ(created->texture_id, "texture-id");
  EXPECT_EQ(created->tileset_id, "tileset-id");

  // The tileset must point at the artwork just written, at the cell size it was
  // generated for, or every tile samples the wrong rectangle.
  EXPECT_EQ(saved.name, "meadow");
  EXPECT_EQ(saved.texture_id, "texture-id");
  EXPECT_EQ(saved.tile_width, 8);
  EXPECT_EQ(saved.tile_height, 8);

  ASSERT_EQ(saved.terrains.size(), 1);
  EXPECT_EQ(saved.terrains[0].name, "meadow") << "the terrain should be named, not left 'Terrain'";
  EXPECT_EQ(saved.terrains[0].rules.size(), static_cast<size_t>(kBlob47TileCount));
  EXPECT_EQ(saved.tiles.size(), static_cast<size_t>(kBlob47TileCount) + kSlopeShapeCount);
  EXPECT_EQ(created->tile_count, static_cast<int>(saved.tiles.size()));
}

TEST_F(TerrainCreationTest, GeneratingCarriesThePatternPeriodOntoTheTerrain) {
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _)).WillOnce(Return(std::string("tex")));
  Tileset saved;
  EXPECT_CALL(api_, CreateTileset(_)).WillOnce([&](Tileset tileset) {
    saved = std::move(tileset);
    return std::string("ts");
  });

  TerrainGenConfig config = SmallConfig();
  config.variant_period = 2;
  ASSERT_TRUE(CreateGeneratedTerrainTileset(api_, "meadow", config).ok());

  ASSERT_EQ(saved.terrains.size(), 1);
  EXPECT_EQ(saved.terrains[0].variant_period, 2);
  // Four phases per mask, so the brush can lay the pattern back down in phase.
  EXPECT_EQ(saved.terrains[0].rules[0].variants.size(), 4u);
}

// Artwork is written before the tileset, so a name collision fails before
// anything is saved rather than leaving a tileset pointing at nothing.
TEST_F(TerrainCreationTest, NoTilesetIsSavedWhenArtworkCannotBeWritten) {
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _))
      .WillOnce(Return(absl::AlreadyExistsError("artwork exists")));
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);

  EXPECT_FALSE(CreateGeneratedTerrainTileset(api_, "meadow", SmallConfig()).ok());
}

TEST_F(TerrainCreationTest, GeneratingRefusesAnUnnamedTerrain) {
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _)).Times(0);
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);

  EXPECT_FALSE(CreateGeneratedTerrainTileset(api_, "", SmallConfig()).ok());
}

// The import route names artwork that already exists rather than writing any.
TEST_F(TerrainCreationTest, ImportingUsesTheChosenTextureAndWritesNoArtwork) {
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _)).Times(0);
  Tileset saved;
  EXPECT_CALL(api_, CreateTileset(_)).WillOnce([&](Tileset tileset) {
    saved = std::move(tileset);
    return std::string("tileset-id");
  });

  absl::StatusOr<CreatedTerrain> created =
      CreateImportedTerrainTileset(api_, "drawn", "existing-texture", ManifestFor(1));
  ASSERT_TRUE(created.ok()) << created.status();

  EXPECT_EQ(saved.texture_id, "existing-texture");
  // The cell size comes from the manifest; the tileset has no other way to know
  // what grid the source coordinates were cut on.
  EXPECT_EQ(saved.tile_width, 8);
  ASSERT_EQ(saved.terrains.size(), 1);
  EXPECT_EQ(saved.terrains[0].rules.size(), static_cast<size_t>(kBlob47TileCount));
}

TEST_F(TerrainCreationTest, ImportingRefusesWithoutATexture) {
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);

  absl::Status status =
      CreateImportedTerrainTileset(api_, "drawn", "", ManifestFor(1)).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(std::string(status.message()), HasSubstr("texture"));
}

TEST_F(TerrainCreationTest, ImportingRejectsAMalformedManifest) {
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);
  EXPECT_FALSE(CreateImportedTerrainTileset(api_, "drawn", "tex", "{not json").ok());
}

}  // namespace
}  // namespace zebes
