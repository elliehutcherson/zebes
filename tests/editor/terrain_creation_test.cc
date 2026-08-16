#include "editor/terrain_editor/terrain_creation.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
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
  EXPECT_OK(atlas);
  return WriteBlob47Manifest(*atlas);
}

// Replaces `tileset`'s tiles with a blob-grid layout of real IDs and source
// rects, and returns the kDerived terrain that records what each one depicts.
//
// Regeneration's whole input is derived_tiles, so a terrain without it renders a
// zero-height atlas. A mocked ReplaceTexturePixels accepts that; the PNG writer
// behind the real one does not, so a fixture that skipped this would be
// asserting against a state production cannot reach.
Terrain MakeDerivedTerrain(int terrain_id, Tileset& tileset) {
  Terrain terrain{.id = terrain_id, .scheme = TerrainScheme::kDerived, .variant_period = 1};

  TerrainCellKey buried;
  buried.shape = TileShape::kFullBlock;
  buried.neighbors.fill(TileShape::kFullBlock);

  tileset.tiles.clear();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    const int tile_id = i + 1;
    tileset.tiles.push_back(Tile{
        .id = tile_id,
        .name = absl::StrCat("tile_", tile_id),
        .source_x = (i % kBlob47Columns) * tileset.tile_width,
        .source_y = (i / kBlob47Columns) * tileset.tile_height,
        .shape = TileShape::kFullBlock,
    });
    terrain.derived_tiles.push_back(DerivedTile{.tile_id = tile_id, .key = buried});
  }
  return terrain;
}

class TerrainCreationTest : public ::testing::Test {
 protected:
  NiceMock<MockApi> api_;
};

class RecipeTerrainCreationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() / "zebes-terrain-creation-recipes";
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(recipes_, TerrainRecipeManager::Create(path_.string()));
    ASSERT_OK(recipes_->LoadAllRecipes());
    // Recipes reach the editor through the Api now, but this fixture still
    // wants real persistence: it asserts what actually lands on disk.
    api_.DelegateTerrainRecipesTo(*recipes_);
  }
  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<TerrainRecipeManager> recipes_;
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
  ASSERT_OK(created);
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
  EXPECT_EQ(saved.terrains[0].scheme, TerrainScheme::kDerived);
  // No rule table: a derived terrain resolves by rendering for the cell's real
  // neighbourhood, and no slope units, because those were one drawing per shape
  // against a neighbourhood the generator had to guess at.
  EXPECT_TRUE(saved.terrains[0].rules.empty());
  EXPECT_EQ(saved.tiles.size(), static_cast<size_t>(kBlob47TileCount));
  EXPECT_EQ(saved.terrains[0].derived_tiles.size(), saved.tiles.size())
      << "the terrain owns its artwork even without rules, or the brush reads it as foreign";
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
  ASSERT_OK(CreateGeneratedTerrainTileset(api_, "meadow", config));

  ASSERT_EQ(saved.terrains.size(), 1);
  EXPECT_EQ(saved.terrains[0].variant_period, 2);
  // Four phases of the pattern, which the key carries so a cell is drawn at the
  // phase its coordinates put it in.
  EXPECT_EQ(saved.tiles.size(), static_cast<size_t>(kBlob47TileCount) * 4);
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
  ASSERT_OK(created);

  EXPECT_EQ(saved.texture_id, "existing-texture");
  // The cell size comes from the manifest; the tileset has no other way to know
  // what grid the source coordinates were cut on.
  EXPECT_EQ(saved.tile_width, 8);
  ASSERT_EQ(saved.terrains.size(), 1);
  EXPECT_EQ(saved.terrains[0].rules.size(), static_cast<size_t>(kBlob47TileCount));
}

TEST_F(TerrainCreationTest, ImportingRefusesWithoutATexture) {
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);

  absl::Status status = CreateImportedTerrainTileset(api_, "drawn", "", ManifestFor(1)).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(std::string(status.message()), HasSubstr("texture"));
}

TEST_F(TerrainCreationTest, ImportingRejectsAMalformedManifest) {
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);
  EXPECT_FALSE(CreateImportedTerrainTileset(api_, "drawn", "tex", "{not json").ok());
}

TEST_F(RecipeTerrainCreationTest, GeneratedTerrainRecordsAReopenableRecipe) {
  EXPECT_CALL(api_, CreateTextureFromPixels("meadow", _, _, _))
      .WillOnce(Return(std::string("texture-id")));
  EXPECT_CALL(api_, CreateTileset(_)).WillOnce(Return(std::string("tileset-id")));

  const TerrainGenConfig config = SmallConfig();
  ASSERT_OK_AND_ASSIGN(CreatedTerrain created,
                       CreateGeneratedTerrainTileset(api_, "meadow", config,
                                                     std::optional<std::string>("Cozy Meadow")));
  ASSERT_FALSE(created.recipe_id.empty());
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * recipe, recipes_->GetRecipe(created.recipe_id));
  EXPECT_EQ(recipe->tileset_id, "tileset-id");
  EXPECT_EQ(recipe->texture_id, "texture-id");
  EXPECT_EQ(recipe->terrain_id, 1);
  EXPECT_EQ(recipe->source_preset, std::optional<std::string>("Cozy Meadow"));
  EXPECT_EQ(TerrainRecipeToJson(*recipe)["config"],
            TerrainRecipeToJson(TerrainRecipe{.id = "x",
                                              .name = "x",
                                              .tileset_id = "x",
                                              .texture_id = "x",
                                              .terrain_id = 1,
                                              .config = config})["config"]);
}

TEST_F(RecipeTerrainCreationTest, RegenerationReplacesOnlyPixelsAndPreservesIds) {
  TerrainRecipe recipe{.name = "meadow",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 9,
                       .config = SmallConfig()};
  ASSERT_OK_AND_ASSIGN(recipe.id, recipes_->CreateRecipe(recipe));

  Tileset tileset{.id = "tileset-id",
                  .name = "meadow",
                  .texture_id = "texture-id",
                  .tile_width = 8,
                  .tile_height = 8};
  tileset.terrains.push_back(MakeDerivedTerrain(/*terrain_id=*/9, tileset));
  EXPECT_CALL(api_, GetTileset("tileset-id")).WillOnce(Return(&tileset));
  EXPECT_CALL(api_, ReplaceTexturePixels("texture-id", _, _, _)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _)).Times(0);
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);
  EXPECT_CALL(api_, UpdateTileset(_)).Times(0);

  TerrainGenConfig edited = recipe.config;
  edited.seed = 777;
  ASSERT_OK(RegenerateTerrainTileset(api_, recipe, edited));

  ASSERT_OK_AND_ASSIGN(TerrainRecipe * saved, recipes_->GetRecipe(recipe.id));
  EXPECT_EQ(saved->tileset_id, recipe.tileset_id);
  EXPECT_EQ(saved->texture_id, recipe.texture_id);
  EXPECT_EQ(saved->terrain_id, recipe.terrain_id);
  EXPECT_EQ(saved->config.seed, 777u);
}

TEST_F(RecipeTerrainCreationTest, RegenerationRedrawsTilesAddedAfterGeneration) {
  // The reason a derived tile carries its key. Regeneration used to overwrite
  // the atlas positionally, which only held while the tileset had exactly the
  // tiles generation produced; a level asking for a neighbourhood adds more,
  // and those could not be redrawn at all. Each is now redrawn from what it
  // records, wherever it sits in the atlas.
  TerrainRecipe recipe{.name = "meadow",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 9,
                       .config = SmallConfig()};
  ASSERT_OK_AND_ASSIGN(recipe.id, recipes_->CreateRecipe(recipe));

  Tileset tileset{.id = "tileset-id",
                  .name = "meadow",
                  .texture_id = "texture-id",
                  .tile_width = 8,
                  .tile_height = 8};
  Terrain terrain{.id = 9, .scheme = TerrainScheme::kDerived, .variant_period = 1};

  // One tile from generation, and one a level asked for later: a ramp meeting
  // open air, which no baked atlas ever held.
  tileset.tiles.push_back(Tile{.id = 1, .name = "block", .shape = TileShape::kFullBlock});
  TerrainCellKey buried;
  buried.shape = TileShape::kFullBlock;
  buried.neighbors.fill(TileShape::kFullBlock);
  terrain.derived_tiles.push_back(DerivedTile{.tile_id = 1, .key = buried});

  tileset.tiles.push_back(
      Tile{.id = 2, .name = "ledge", .source_y = 8, .shape = TileShape::kSlope45FloorTallRight});
  TerrainCellKey ledge;
  ledge.shape = TileShape::kSlope45FloorTallRight;
  ledge.neighbors.fill(TileShape::kNone);
  ledge.neighbors[4] = TileShape::kFullBlock;
  terrain.derived_tiles.push_back(DerivedTile{.tile_id = 2, .key = ledge});

  tileset.terrains.push_back(std::move(terrain));
  EXPECT_CALL(api_, GetTileset("tileset-id")).WillOnce(Return(&tileset));

  // Both rows are rewritten, so the added tile is redrawn rather than left
  // showing the old material.
  int written_height = 0;
  EXPECT_CALL(api_, ReplaceTexturePixels("texture-id", _, _, _))
      .WillOnce([&](const std::string&, int, int height, absl::Span<const uint8_t>) {
        written_height = height;
        return absl::OkStatus();
      });

  TerrainGenConfig edited = recipe.config;
  edited.seed = 4242;
  ASSERT_OK(RegenerateTerrainTileset(api_, recipe, edited));

  EXPECT_EQ(written_height, 16) << "the atlas keeps the extent its tiles occupy";
}

TEST_F(RecipeTerrainCreationTest, StructuralRegenerationRequiresSaveAsBeforeWrites) {
  TerrainRecipe recipe{.id = "recipe-id",
                       .name = "meadow",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 1,
                       .config = SmallConfig()};
  TerrainGenConfig edited = recipe.config;
  edited.tile_size = 16;

  EXPECT_CALL(api_, GetTileset(_)).Times(0);
  EXPECT_CALL(api_, ReplaceTexturePixels(_, _, _, _)).Times(0);
  const absl::Status status = RegenerateTerrainTileset(api_, recipe, edited);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Save As"));
}

// A generated terrain from before this phase is kBlob47 with baked slope units
// and no record of what any tile depicts. Regeneration's input is exactly that
// record, so it has nothing to draw -- and without this guard it would rasterise
// a zero-height atlas and fail in the PNG writer, reporting the image size
// instead of the reason.
TEST_F(RecipeTerrainCreationTest, RegenerationRefusesATerrainThatRecordsNoNeighbourhoods) {
  TerrainRecipe recipe{.name = "lucinda",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 1,
                       .config = SmallConfig()};
  ASSERT_OK_AND_ASSIGN(recipe.id, recipes_->CreateRecipe(recipe));

  Tileset tileset{
      .id = "tileset-id", .texture_id = "texture-id", .tile_width = 8, .tile_height = 8};
  tileset.tiles.resize(kBlob47TileCount);
  tileset.terrains.push_back(
      Terrain{.id = 1, .name = "lucinda", .scheme = TerrainScheme::kBlob47, .variant_period = 1});
  EXPECT_CALL(api_, GetTileset("tileset-id")).WillOnce(Return(&tileset));
  EXPECT_CALL(api_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  TerrainGenConfig edited = recipe.config;
  edited.seed = 4242;
  const absl::Status status = RegenerateTerrainTileset(api_, recipe, edited);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Save As"));

  // The refusal happens before anything is written, so the recipe is untouched.
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * saved, recipes_->GetRecipe(recipe.id));
  EXPECT_EQ(saved->config.seed, recipe.config.seed);
}

TEST_F(RecipeTerrainCreationTest, ArtworkFailureRollsRecipeBack) {
  TerrainRecipe recipe{.name = "meadow",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 1,
                       .config = SmallConfig()};
  ASSERT_OK_AND_ASSIGN(recipe.id, recipes_->CreateRecipe(recipe));
  Tileset tileset{
      .id = "tileset-id", .texture_id = "texture-id", .tile_width = 8, .tile_height = 8};
  tileset.terrains.push_back(MakeDerivedTerrain(/*terrain_id=*/1, tileset));
  EXPECT_CALL(api_, GetTileset(_)).WillOnce(Return(&tileset));
  EXPECT_CALL(api_, ReplaceTexturePixels(_, _, _, _))
      .WillOnce(Return(absl::InternalError("disk full")));

  TerrainGenConfig edited = recipe.config;
  edited.seed = 999;
  EXPECT_FALSE(RegenerateTerrainTileset(api_, recipe, edited).ok());
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * saved, recipes_->GetRecipe(recipe.id));
  EXPECT_EQ(saved->config.seed, recipe.config.seed);
}

}  // namespace
}  // namespace zebes
