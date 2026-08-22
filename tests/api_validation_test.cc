#include "api/api.h"
#include "common/image_digest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "resources/blueprint_manager_mock.h"
#include "resources/collider_manager_mock.h"
#include "resources/level_manager_mock.h"
#include "resources/parallax_theme_manager_mock.h"
#include "resources/prop_recipe_manager_mock.h"
#include "resources/source_artwork_manager_mock.h"
#include "resources/sprite_manager_mock.h"
#include "resources/terrain_recipe_manager_mock.h"
#include "resources/texture_manager_mock.h"
#include "resources/tileset_manager_mock.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

class ApiValidationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Api::Options options = {
        .config = &config_,
        .texture_manager = &texture_manager_,
        .sprite_manager = &sprite_manager_,
        .collider_manager = &collider_manager_,
        .blueprint_manager = &blueprint_manager_,
        .level_manager = &level_manager_,
        .parallax_theme_manager = &parallax_theme_manager_,
        .tileset_manager = &tileset_manager_,
        .terrain_recipe_manager = &terrain_recipe_manager_,
        .source_artwork_manager = &source_artwork_manager_,
        .prop_recipe_manager = &prop_recipe_manager_,
    };

    ASSERT_OK_AND_ASSIGN(api_, Api::Create(options));
  }

  EngineConfig config_;
  // Nice, because every delete reads every catalogue to find referrers. Those
  // reads are expected rather than interesting, and left strict they bury the
  // output in warnings about calls each test deliberately says nothing about.
  NiceMock<TextureManagerMock> texture_manager_;
  NiceMock<SpriteManagerMock> sprite_manager_;
  NiceMock<ColliderManagerMock> collider_manager_;
  NiceMock<BlueprintManagerMock> blueprint_manager_;
  NiceMock<LevelManagerMock> level_manager_;
  NiceMock<ParallaxThemeManagerMock> parallax_theme_manager_;
  NiceMock<TilesetManagerMock> tileset_manager_;
  NiceMock<TerrainRecipeManagerMock> terrain_recipe_manager_;
  NiceMock<SourceArtworkManagerMock> source_artwork_manager_;
  NiceMock<PropRecipeManagerMock> prop_recipe_manager_;
  std::unique_ptr<Api> api_;
};

// Deleting reads every catalogue, so a test that says nothing about one is
// asserting that it is empty. These helpers make that explicit rather than
// leaving it to gmock's default-constructed return.
Blueprint BlueprintUsing(std::string sprite_id, std::string collider_id) {
  Blueprint blueprint;
  blueprint.id = "bp";
  blueprint.name = "Tree";
  blueprint.states.push_back(Blueprint::State{
      .name = "Idle", .collider_id = std::move(collider_id), .sprite_id = std::move(sprite_id)});
  return blueprint;
}

Level LevelUsingTileset(std::string tileset_id) {
  Level level;
  level.id = "lv";
  level.name = "Cave Level";
  level.tileset_id = std::move(tileset_id);
  return level;
}

TEST_F(ApiValidationTest, DeleteTextureInUseByATilesetReturnsError) {
  const std::string texture_id = "test_texture";
  EXPECT_CALL(tileset_manager_, GetAllTilesets())
      .WillOnce(Return(
          std::vector<Tileset>{Tileset{.id = "ts", .name = "Cave", .texture_id = texture_id}}));
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  const absl::Status status = api_->DeleteTexture(texture_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  // The refusal has to say what to go and change.
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave"));
}

// The old check asked only whether a sprite used the texture, so a tileset, a
// parallax layer or a recipe naming it went unnoticed.
TEST_F(ApiValidationTest, DeleteTextureInUseByARecipeReturnsError) {
  const std::string texture_id = "test_texture";
  EXPECT_CALL(terrain_recipe_manager_, GetAllRecipes())
      .WillOnce(Return(std::vector<TerrainRecipe>{
          TerrainRecipe{.id = "rc", .name = "Cave", .texture_id = texture_id}}));
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  EXPECT_EQ(api_->DeleteTexture(texture_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteTextureInUseByAParallaxThemeReturnsError) {
  EXPECT_CALL(parallax_theme_manager_, GetAllThemes())
      .WillOnce(Return(std::vector<ParallaxTheme>{{
          .id = "theme",
          .name = "Crystal Cave",
          .layers = {{.name = "Far", .texture_id = "texture"}},
      }}));
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  const absl::Status status = api_->DeleteTexture("texture");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Crystal Cave"));
}

TEST_F(ApiValidationTest, DeleteReferencedParallaxThemeReturnsError) {
  Level first;
  first.id = "first";
  first.name = "Cave Entrance";
  first.zones.push_back({.id = 0, .name = "Entry", .theme_id = "shared"});
  Level second;
  second.id = "second";
  second.name = "Cave Depths";
  second.zones.push_back({.id = 0, .name = "Depths", .theme_id = "shared"});
  EXPECT_CALL(level_manager_, GetAllLevels()).WillOnce(Return(std::vector<Level>{first, second}));
  EXPECT_CALL(parallax_theme_manager_, DeleteTheme(_)).Times(0);

  const absl::Status status = api_->DeleteParallaxTheme("shared");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Entrance"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Depths"));
}

TEST_F(ApiValidationTest, CreateParallaxThemeRefusesMissingTextureBeforePublishing) {
  EXPECT_CALL(texture_manager_, GetTexture("missing"))
      .WillOnce(Return(absl::NotFoundError("missing")));
  EXPECT_CALL(parallax_theme_manager_, CreateTheme(_)).Times(0);
  ParallaxTheme theme{
      .name = "Cave",
      .layers = {{.name = "Far", .texture_id = "missing"}},
  };

  EXPECT_EQ(api_->CreateParallaxTheme(theme).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, CreateLevelRefusesMissingThemeBeforePublishing) {
  Level level{.name = "Cave", .width = 320, .height = 320};
  level.zones.push_back({
      .id = 0,
      .name = "Main",
      .theme_id = "missing",
      .min_point = {0, 0},
      .max_point = {320, 320},
  });
  EXPECT_CALL(parallax_theme_manager_, GetTheme("missing"))
      .WillOnce(Return(absl::NotFoundError("missing")));
  EXPECT_CALL(level_manager_, CreateLevel(_)).Times(0);

  const absl::Status status = api_->CreateLevel(std::move(level)).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("missing theme"));
}

TEST_F(ApiValidationTest, DeleteTextureNotInUseCallsDelete) {
  const std::string texture_id = "test_texture";
  EXPECT_CALL(texture_manager_, DeleteTexture(texture_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteTexture(texture_id));
}

TEST_F(ApiValidationTest, DeleteSpriteInUseInBlueprintReturnsError) {
  const std::string sprite_id = "test_sprite";
  EXPECT_CALL(blueprint_manager_, GetAllBlueprints())
      .WillOnce(Return(std::vector<Blueprint>{BlueprintUsing(sprite_id, "")}));
  EXPECT_CALL(sprite_manager_, DeleteSprite(_)).Times(0);

  EXPECT_EQ(api_->DeleteSprite(sprite_id).code(), absl::StatusCode::kFailedPrecondition);
}

// An entity carries its own sprite ID, so a level can hold the last reference to
// a sprite no blueprint mentions. The old blueprint-only check missed this.
TEST_F(ApiValidationTest, DeleteSpritePlacedInALevelReturnsError) {
  const std::string sprite_id = "test_sprite";
  Level level = LevelUsingTileset("");
  Entity entity;
  entity.id = 3;
  entity.sprite_id = sprite_id;
  level.layers.front().entities.emplace(3, std::move(entity));
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));
  EXPECT_CALL(sprite_manager_, DeleteSprite(_)).Times(0);

  EXPECT_EQ(api_->DeleteSprite(sprite_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteSpriteNotInUseCallsDelete) {
  const std::string sprite_id = "test_sprite";
  EXPECT_CALL(sprite_manager_, DeleteSprite(sprite_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteSprite(sprite_id));
}

TEST_F(ApiValidationTest, DeleteColliderInUseInBlueprintReturnsError) {
  const std::string collider_id = "test_collider";
  EXPECT_CALL(blueprint_manager_, GetAllBlueprints())
      .WillOnce(Return(std::vector<Blueprint>{BlueprintUsing("", collider_id)}));
  EXPECT_CALL(collider_manager_, DeleteCollider(_)).Times(0);

  EXPECT_EQ(api_->DeleteCollider(collider_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteColliderNotInUseCallsDelete) {
  const std::string collider_id = "test_collider";
  EXPECT_CALL(collider_manager_, DeleteCollider(collider_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteCollider(collider_id));
}

// Nothing checked this before: deleting a tileset a level painted through left
// that level naming tile IDs it could no longer resolve, which stops its
// viewport rendering rather than degrading it.
TEST_F(ApiValidationTest, DeleteTilesetBoundToALevelReturnsError) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{LevelUsingTileset(tileset_id)}));
  EXPECT_CALL(tileset_manager_, DeleteTileset(_)).Times(0);

  const absl::Status status = api_->DeleteTileset(tileset_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
}

TEST_F(ApiValidationTest, DeleteTilesetUnreferencedCallsDelete) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(tileset_manager_, DeleteTileset(tileset_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteTileset(tileset_id));
}

// The tile hole. Deleting a painted tile is not recoverable by re-adding it:
// NextTileId is max+1, so the new tile gets a new ID while the level still names
// the old one, and the level stops rendering rather than losing a cell.
TEST_F(ApiValidationTest, CheckTileDeletablePaintedInALevelReturnsError) {
  const std::string tileset_id = "test_tileset";
  Level level = LevelUsingTileset(tileset_id);
  TileChunk chunk;
  chunk.tiles[0] = 7;
  chunk.tiles[1] = 7;
  level.layers.front().tile_chunks[0] = chunk;
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));

  const absl::Status status = api_->CheckTileDeletable(tileset_id, 7);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("2 painted cells"));
}

// A tile nothing painted stays one click away, which is the whole reason tile
// deletion is not confirmed.
TEST_F(ApiValidationTest, CheckTileDeletableUnpaintedPasses) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{LevelUsingTileset(tileset_id)}));

  EXPECT_OK(api_->CheckTileDeletable(tileset_id, 7));
}

// Tile IDs are bare integers with no tileset qualifier, so the same number in a
// level bound elsewhere is different artwork and not a reference.
TEST_F(ApiValidationTest, CheckTileDeletableIgnoresLevelsBoundToAnotherTileset) {
  Level level = LevelUsingTileset("some_other_tileset");
  TileChunk chunk;
  chunk.tiles[0] = 7;
  level.layers.front().tile_chunks[0] = chunk;
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));

  EXPECT_OK(api_->CheckTileDeletable("test_tileset", 7));
}

// --- Bundle deletion ---------------------------------------------------------

// The three records of one build product. Only the recipe knows they belong
// together, so the bundle is resolved from it.
class GeneratedTerrainTest : public ApiValidationTest {
 protected:
  void SetUp() override {
    ApiValidationTest::SetUp();
    recipe_.id = "rc";
    recipe_.name = "lucinda_cave";
    recipe_.tileset_id = "ts";
    recipe_.texture_id = "tx";
    ON_CALL(terrain_recipe_manager_, GetRecipe("rc")).WillByDefault(Return(&recipe_));
  }

  // The bundle as the catalogue sees it: a recipe naming both, and a tileset
  // naming the artwork. None of these are outside references.
  //
  // The catalogue is stateful because the ordering is the whole point. Each
  // delete re-scans, so a member that has been removed must stop appearing --
  // and a fixed catalogue would report the recipe as still blocking the tileset,
  // which is a state production cannot reach.
  void CatalogueHoldsTheBundle() {
    recipes_ = {recipe_};
    tilesets_ = {Tileset{.id = "ts", .name = "lucinda_cave", .texture_id = "tx"}};

    ON_CALL(terrain_recipe_manager_, GetAllRecipes()).WillByDefault([this] { return recipes_; });
    ON_CALL(tileset_manager_, GetAllTilesets()).WillByDefault([this] { return tilesets_; });
    ON_CALL(terrain_recipe_manager_, DeleteRecipe(_)).WillByDefault([this](const std::string&) {
      recipes_.clear();
      return absl::OkStatus();
    });
    ON_CALL(tileset_manager_, DeleteTileset(_)).WillByDefault([this](const std::string&) {
      tilesets_.clear();
      return absl::OkStatus();
    });
  }

  TerrainRecipe recipe_;
  std::vector<TerrainRecipe> recipes_;
  std::vector<Tileset> tilesets_;
};

TEST_F(GeneratedTerrainTest, DeletesRecipeThenTilesetThenArtwork) {
  CatalogueHoldsTheBundle();

  // Ordered, because each member has to stop blocking the next. Times(1) rather
  // than WillOnce, so the fixture's stateful defaults still run and each delete
  // actually leaves the catalogue.
  ::testing::InSequence sequence;
  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe("rc")).Times(1);
  EXPECT_CALL(tileset_manager_, DeleteTileset("ts")).Times(1);
  EXPECT_CALL(texture_manager_, DeleteTexture("tx")).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteGeneratedTerrain("rc"));
}

// Nothing is deleted, because the sequence cannot be unwound: removing the
// recipe first and then finding the level would leave the un-regenerable pair
// this operation exists to prevent.
TEST_F(GeneratedTerrainTest, RefusesAndChangesNothingWhenALevelUsesTheTileset) {
  CatalogueHoldsTheBundle();
  ON_CALL(level_manager_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{LevelUsingTileset("ts")}));

  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe(_)).Times(0);
  EXPECT_CALL(tileset_manager_, DeleteTileset(_)).Times(0);
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  const absl::Status status = api_->DeleteGeneratedTerrain("rc");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
}

// Artwork a sprite also draws from is not this terrain's alone to delete, even
// though the recipe generated it.
TEST_F(GeneratedTerrainTest, RefusesWhenASpriteAlsoUsesTheArtwork) {
  CatalogueHoldsTheBundle();
  ON_CALL(sprite_manager_, GetAllSprites())
      .WillByDefault(
          Return(std::vector<Sprite>{Sprite{.id = "sp", .name = "Crystal", .texture_id = "tx"}}));

  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe(_)).Times(0);

  const absl::Status status = api_->DeleteGeneratedTerrain("rc");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Crystal"));
}

// The members reference each other by construction. Counting those as blockers
// would make every bundle undeletable.
TEST_F(GeneratedTerrainTest, TheBundlesOwnReferencesDoNotBlockIt) {
  CatalogueHoldsTheBundle();
  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe("rc")).Times(1);
  EXPECT_CALL(tileset_manager_, DeleteTileset("ts")).Times(1);
  EXPECT_CALL(texture_manager_, DeleteTexture("tx")).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteGeneratedTerrain("rc"));
}

// A half-finished bundle has to stay finishable, so a member already gone is the
// postcondition rather than a failure. Here the tileset was removed by hand
// earlier: it is absent from the catalogue and its delete reports NotFound.
TEST_F(GeneratedTerrainTest, ToleratesAMemberThatIsAlreadyGone) {
  CatalogueHoldsTheBundle();
  tilesets_.clear();

  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe("rc")).Times(1);
  EXPECT_CALL(tileset_manager_, DeleteTileset("ts"))
      .WillOnce(Return(absl::NotFoundError("Tileset not found")));
  EXPECT_CALL(texture_manager_, DeleteTexture("tx")).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteGeneratedTerrain("rc"));
}

// Deleting a generated tileset on its own is blocked by its recipe, which is
// correct and useless on its own: there is nothing to unbind.
TEST_F(GeneratedTerrainTest, ARecipeBlockingATilesetPointsAtTheTerrainEditor) {
  CatalogueHoldsTheBundle();
  EXPECT_CALL(tileset_manager_, DeleteTileset(_)).Times(0);

  const absl::Status status = api_->DeleteTileset("ts");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Terrain Editor"));
}

TEST_F(ApiValidationTest, DeleteBlueprintPlacedInALevelReturnsError) {
  const std::string blueprint_id = "test_blueprint";
  Level level = LevelUsingTileset("");
  Entity entity;
  entity.id = 9;
  entity.blueprint_id = blueprint_id;
  level.layers.front().entities.emplace(9, std::move(entity));
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(_)).Times(0);

  EXPECT_EQ(api_->DeleteBlueprint(blueprint_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteBlueprintUnplacedCallsDelete) {
  const std::string blueprint_id = "test_blueprint";
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(blueprint_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteBlueprint(blueprint_id));
}

TEST_F(ApiValidationTest, DeleteSourceArtworkReferencedByAPropRecipeReturnsError) {
  EXPECT_CALL(prop_recipe_manager_, GetAllRecipes())
      .WillOnce(Return(std::vector<PropRecipe>{
          PropRecipe{.id = "prop", .name = "Tree", .source_artwork_id = "source"}}));
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork(_)).Times(0);

  const absl::Status status = api_->DeleteSourceArtwork("source");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Tree"));
}

TEST_F(ApiValidationTest, DeleteSourceArtworkUnreferencedCallsDelete) {
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork("source")).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteSourceArtwork("source"));
}

TEST_F(ApiValidationTest, DeleteTerrainRecipeAttachedToAPropReturnsError) {
  EXPECT_CALL(prop_recipe_manager_, GetAllRecipes())
      .WillOnce(Return(std::vector<PropRecipe>{PropRecipe{
          .id = "prop",
          .name = "Tree",
          .terrain_recipe_id = "terrain",
      }}));
  EXPECT_CALL(terrain_recipe_manager_, DeleteRecipe(_)).Times(0);

  const absl::Status status = api_->DeleteTerrainRecipe("terrain");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Tree"));
}

TEST_F(ApiValidationTest, CreatePropRecipeRefusesAMissingSourceBeforePublishingRecipe) {
  PropRecipe recipe;
  recipe.source_artwork_id = "missing";
  EXPECT_CALL(source_artwork_manager_, GetArtwork("missing"))
      .WillOnce(Return(absl::NotFoundError("missing source")));
  EXPECT_CALL(prop_recipe_manager_, CreateRecipe(_)).Times(0);

  EXPECT_EQ(api_->CreatePropRecipe(std::move(recipe)).status().code(), absl::StatusCode::kNotFound);
}

class GeneratedPropCommitTest : public ApiValidationTest {
 protected:
  void SetUp() override {
    ApiValidationTest::SetUp();
    const TerrainGenConfig terrain;
    ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(terrain));
    const RgbaImage final_image{
        .width = 1,
        .height = 1,
        .pixels = {palette.at(TerrainPaletteRole::kOutline).r,
                   palette.at(TerrainPaletteRole::kOutline).g,
                   palette.at(TerrainPaletteRole::kOutline).b, 255},
    };
    ASSERT_OK_AND_ASSIGN(const std::string final_digest, RgbaImageDigest(final_image));
    const std::string source_digest(64, '1');
    const SpriteFrame frame{
        .index = 0,
        .texture_x = 0,
        .texture_y = 0,
        .texture_w = 1,
        .texture_h = 1,
        .render_w = 1,
        .render_h = 1,
        .frames_per_cycle = 0,
        .offset_x = 0,
        .offset_y = 0,
    };
    prepared_ = PreparedPropAsset{
        .source =
            SourceArtwork{
                .id = "source-1",
                .name = "Boulder source",
                .source_path = "source_art/props/source-1.png",
                .provenance =
                    ImportedArtworkProvenance{
                        .original_filename = "boulder.png",
                        .imported_at_utc = "2026-08-16T15:04:05Z",
                    },
                .width = 1,
                .height = 1,
                .content_digest = source_digest,
            },
        .artwork =
            PropArtworkPipelineResult{
                .pipeline_version = kPropArtworkPipelineVersion,
                .source_digest = source_digest,
                .finished = PropArtwork{.image = final_image, .anchor_x = 0, .anchor_y = 0},
            },
        .texture =
            Texture{
                .id = "texture-1",
                .name = "Cave boulder",
                .path = "textures/props/texture-1.png",
            },
        .sprite =
            Sprite{
                .id = "sprite-1",
                .name = "Cave boulder",
                .texture_id = "texture-1",
                .frames = {frame},
            },
        .blueprint =
            Blueprint{
                .id = "blueprint-1",
                .name = "Cave boulder",
                .states = {Blueprint::State{
                    .name = "Default",
                    .collider_id = "",
                    .sprite_id = "sprite-1",
                }},
            },
        .recipe =
            PropRecipe{
                .id = "recipe-1",
                .name = "Cave boulder",
                .source_artwork_id = "source-1",
                .style =
                    PropArtworkStyle{.tile_size = 1, .pixel_block_size = 1, .palette = palette},
                .pipeline =
                    PropArtworkPipelineConfig{
                        .composition =
                            PropCompositionConfig{
                                .canvas_tiles_wide = 1,
                                .canvas_tiles_high = 1,
                            },
                    },
                .texture_id = "texture-1",
                .sprite_id = "sprite-1",
                .blueprint_id = "blueprint-1",
                .expected_frame = frame,
                .final_pixel_digest = final_digest,
            },
    };
    ASSERT_OK(ValidatePreparedPropAsset(prepared_));

    ON_CALL(source_artwork_manager_, GetArtwork("source-1"))
        .WillByDefault(Return(&prepared_.source));
    ON_CALL(texture_manager_, GetTexture("texture-1"))
        .WillByDefault(Return(absl::NotFoundError("missing texture")));
    ON_CALL(sprite_manager_, GetSprite("sprite-1"))
        .WillByDefault(Return(absl::NotFoundError("missing sprite")));
    ON_CALL(blueprint_manager_, GetBlueprint("blueprint-1"))
        .WillByDefault(Return(absl::NotFoundError("missing blueprint")));
    ON_CALL(prop_recipe_manager_, GetRecipe("recipe-1"))
        .WillByDefault(Return(absl::NotFoundError("missing recipe")));
    ON_CALL(texture_manager_, PreflightGeneratedTexture(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(sprite_manager_, PreflightSpriteWithId(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(blueprint_manager_, PreflightBlueprintWithId(_))
        .WillByDefault(Return(absl::OkStatus()));
    ON_CALL(prop_recipe_manager_, PreflightRecipeWithId(_)).WillByDefault(Return(absl::OkStatus()));
  }

  PreparedPropAsset prepared_;
};

TEST_F(GeneratedPropCommitTest, PublishesTheRecipeOnlyAfterEveryDependencyExists) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, CreateBlueprintWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, CreateRecipeWithId(_)).WillOnce(Return(absl::OkStatus()));

  ASSERT_OK_AND_ASSIGN(const std::string id, api_->CreateGeneratedProp(prepared_));
  EXPECT_EQ(id, "recipe-1");
}

TEST_F(GeneratedPropCommitTest, TextureFailureLeavesNoBundleMembers) {
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::InternalError("texture write failed")));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).Times(0);
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  EXPECT_FALSE(api_->CreateGeneratedProp(prepared_).ok());
}

TEST_F(GeneratedPropCommitTest, PreflightFailureOccursBeforeAnyBundleMemberIsWritten) {
  EXPECT_CALL(blueprint_manager_, PreflightBlueprintWithId(_))
      .WillOnce(Return(absl::AlreadyExistsError("definition path exists")));
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->CreateGeneratedProp(prepared_).status().code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(GeneratedPropCommitTest, SpriteFailureDeletesTheTexture) {
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_))
      .WillOnce(Return(absl::InternalError("sprite write failed")));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateGeneratedProp(prepared_).ok());
}

TEST_F(GeneratedPropCommitTest, BlueprintFailureUnwindsSpriteThenTexture) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, CreateBlueprintWithId(_))
      .WillOnce(Return(absl::InternalError("blueprint write failed")));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-1")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateGeneratedProp(prepared_).ok());
}

TEST_F(GeneratedPropCommitTest, RecipeFailureUnwindsEveryRuntimeDependencyInReverseOrder) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, CreateBlueprintWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::InternalError("recipe write failed")));
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-1")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateGeneratedProp(prepared_).ok());
}

TEST_F(GeneratedPropCommitTest, ReportsPrimaryAndCompensationFailures) {
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_))
      .WillOnce(Return(absl::InternalError("sprite write failed")));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1"))
      .WillOnce(Return(absl::InternalError("texture cleanup failed")));

  const absl::Status status = api_->CreateGeneratedProp(prepared_).status();
  EXPECT_THAT(std::string(status.message()), HasSubstr("sprite write failed"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("texture cleanup failed"));
}

class GeneratedPropDeleteTest : public ApiValidationTest {
 protected:
  void SetUp() override {
    ApiValidationTest::SetUp();
    recipe_ = PropRecipe{
        .id = "recipe-1",
        .name = "Cave boulder",
        .source_artwork_id = "source-1",
        .texture_id = "texture-1",
        .sprite_id = "sprite-1",
        .blueprint_id = "blueprint-1",
    };
    recipes_ = {recipe_};
    sprites_ = {Sprite{
        .id = "sprite-1",
        .name = "Cave boulder",
        .texture_id = "texture-1",
    }};
    blueprints_ = {Blueprint{
        .id = "blueprint-1",
        .name = "Cave boulder",
        .states = {Blueprint::State{.name = "Default", .sprite_id = "sprite-1"}},
    }};

    ON_CALL(prop_recipe_manager_, GetRecipe("recipe-1")).WillByDefault(Return(&recipe_));
    ON_CALL(prop_recipe_manager_, GetAllRecipes()).WillByDefault([this] { return recipes_; });
    ON_CALL(sprite_manager_, GetAllSprites()).WillByDefault([this] { return sprites_; });
    ON_CALL(blueprint_manager_, GetAllBlueprints()).WillByDefault([this] { return blueprints_; });
    ON_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1"))
        .WillByDefault([this](const std::string&) {
          blueprints_.clear();
          return absl::OkStatus();
        });
    ON_CALL(sprite_manager_, DeleteSprite("sprite-1")).WillByDefault([this](const std::string&) {
      sprites_.clear();
      return absl::OkStatus();
    });
    ON_CALL(prop_recipe_manager_, DeleteRecipe("recipe-1"))
        .WillByDefault([this](const std::string&) {
          recipes_.clear();
          return absl::OkStatus();
        });
    ON_CALL(texture_manager_, DeleteTexture("texture-1")).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(source_artwork_manager_, DeleteArtwork("source-1"))
        .WillByDefault(Return(absl::OkStatus()));
  }

  PropRecipe recipe_;
  std::vector<PropRecipe> recipes_;
  std::vector<Sprite> sprites_;
  std::vector<Blueprint> blueprints_;
};

TEST_F(GeneratedPropDeleteTest, DeletesOutputsThenRecipeThenUnsharedSource) {
  InSequence sequence;
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1")).Times(1);
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-1")).Times(1);
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1")).Times(1);
  EXPECT_CALL(prop_recipe_manager_, DeleteRecipe("recipe-1")).Times(1);
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork("source-1")).Times(1);

  EXPECT_OK(api_->DeleteGeneratedProp("recipe-1"));
}

TEST_F(GeneratedPropDeleteTest, OwnReferencesDoNotBlockDeletion) {
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1")).Times(1);
  EXPECT_OK(api_->DeleteGeneratedProp("recipe-1"));
}

TEST_F(GeneratedPropDeleteTest, LevelPlacementBlocksEverythingBeforeDeletion) {
  Level level = LevelUsingTileset("");
  Entity entity;
  entity.id = 4;
  entity.blueprint_id = "blueprint-1";
  level.layers.front().entities.emplace(entity.id, entity);
  ON_CALL(level_manager_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{std::move(level)}));

  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(_)).Times(0);
  EXPECT_CALL(prop_recipe_manager_, DeleteRecipe(_)).Times(0);

  const absl::Status status = api_->DeleteGeneratedProp("recipe-1");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
}

TEST_F(GeneratedPropDeleteTest, ReuseOfSpriteOrTextureBlocksTheBundle) {
  blueprints_.push_back(Blueprint{
      .id = "other-blueprint",
      .name = "Other blueprint",
      .states = {Blueprint::State{.name = "Default", .sprite_id = "sprite-1"}},
  });
  sprites_.push_back(Sprite{
      .id = "other-sprite",
      .name = "Other sprite",
      .texture_id = "texture-1",
  });

  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(_)).Times(0);
  const absl::Status status = api_->DeleteGeneratedProp("recipe-1");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Other blueprint"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("Other sprite"));
}

TEST_F(GeneratedPropDeleteTest, SharedSourceArtworkSurvives) {
  recipes_.push_back(PropRecipe{
      .id = "recipe-2",
      .name = "Meadow boulder",
      .source_artwork_id = "source-1",
  });
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork(_)).Times(0);

  EXPECT_OK(api_->DeleteGeneratedProp("recipe-1"));
}

TEST_F(GeneratedPropDeleteTest, MissingOutputMembersStillAllowDeletionToFinish) {
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1"))
      .WillOnce(Return(absl::NotFoundError("already absent")));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-1"))
      .WillOnce(Return(absl::NotFoundError("already absent")));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-1"))
      .WillOnce(Return(absl::NotFoundError("already absent")));
  EXPECT_CALL(prop_recipe_manager_, DeleteRecipe("recipe-1")).Times(1);

  EXPECT_OK(api_->DeleteGeneratedProp("recipe-1"));
}

TEST_F(GeneratedPropDeleteTest, FailedOutputDeleteKeepsRecipeForRetry) {
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint("blueprint-1"))
      .WillOnce(Return(absl::InternalError("definition is locked")));
  EXPECT_CALL(prop_recipe_manager_, DeleteRecipe(_)).Times(0);

  const absl::Status status = api_->DeleteGeneratedProp("recipe-1");
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_THAT(std::string(status.message()), HasSubstr("locked"));
}

class GeneratedPropRegenerationTest : public GeneratedPropCommitTest {
 protected:
  void SetUp() override {
    GeneratedPropCommitTest::SetUp();
    regeneration_ = PreparedPropRegeneration{
        .source_snapshot = prepared_.source,
        .recipe_snapshot = prepared_.recipe,
        .texture_snapshot = prepared_.texture,
        .texture_pixel_digest = prepared_.recipe.final_pixel_digest,
        .sprite_snapshot = prepared_.sprite,
        .artwork = prepared_.artwork,
        .updated_sprite = prepared_.sprite,
        .updated_recipe = prepared_.recipe,
    };
    ASSERT_OK(ValidatePreparedPropRegeneration(regeneration_));

    ON_CALL(prop_recipe_manager_, GetRecipe("recipe-1"))
        .WillByDefault(Return(&regeneration_.recipe_snapshot));
    ON_CALL(texture_manager_, GetTexture("texture-1"))
        .WillByDefault(Return(&regeneration_.texture_snapshot));
    ON_CALL(texture_manager_, ReadTexturePixels("texture-1"))
        .WillByDefault(Return(regeneration_.artwork.finished.image));
    ON_CALL(sprite_manager_, GetSprite("sprite-1"))
        .WillByDefault(Return(&regeneration_.sprite_snapshot));
    ON_CALL(blueprint_manager_, GetBlueprint("blueprint-1"))
        .WillByDefault(Return(&prepared_.blueprint));
  }

  PreparedPropRegeneration regeneration_;
};

TEST_F(GeneratedPropRegenerationTest, CommitsSpriteAndRecipeBeforeReplacingPixels) {
  InSequence sequence;
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-1", 1, 1, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(_)).Times(0);

  EXPECT_OK(api_->RegenerateGeneratedProp(regeneration_));
}

TEST_F(GeneratedPropRegenerationTest, BlueprintColliderEditsAreNeverOverwritten) {
  prepared_.blueprint.states.front().collider_id = "hand-authored-collider";
  prepared_.blueprint.states.push_back(
      Blueprint::State{.name = "Broken", .collider_id = "another-collider"});
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(_)).Times(0);
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->RegenerateGeneratedProp(regeneration_));
  EXPECT_EQ(prepared_.blueprint.states.front().collider_id, "hand-authored-collider");
  EXPECT_EQ(prepared_.blueprint.states.size(), 2u);
}

TEST_F(GeneratedPropRegenerationTest, StaleRecipeRefusesEveryWrite) {
  PropRecipe changed = regeneration_.recipe_snapshot;
  changed.name = "Renamed while worker ran";
  ON_CALL(prop_recipe_manager_, GetRecipe("recipe-1")).WillByDefault(Return(&changed));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(GeneratedPropRegenerationTest, StaleSourceRefusesEveryWrite) {
  SourceArtwork changed = regeneration_.source_snapshot;
  changed.name = "Renamed source while worker ran";
  ON_CALL(source_artwork_manager_, GetArtwork("source-1")).WillByDefault(Return(&changed));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(GeneratedPropRegenerationTest, StaleTextureDefinitionRefusesEveryWrite) {
  Texture changed = regeneration_.texture_snapshot;
  changed.name = "Renamed texture while worker ran";
  ON_CALL(texture_manager_, GetTexture("texture-1")).WillByDefault(Return(&changed));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(GeneratedPropRegenerationTest, StaleTexturePixelsRefuseEveryWrite) {
  RgbaImage changed = regeneration_.artwork.finished.image;
  changed.pixels[0] ^= 0xff;
  ON_CALL(texture_manager_, ReadTexturePixels("texture-1")).WillByDefault(Return(changed));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(GeneratedPropRegenerationTest, StaleSpriteRefusesEveryWrite) {
  Sprite changed = regeneration_.sprite_snapshot;
  ++changed.frames.front().offset_x;
  ON_CALL(sprite_manager_, GetSprite("sprite-1")).WillByDefault(Return(&changed));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(GeneratedPropRegenerationTest, MissingBlueprintRefusesEveryWrite) {
  ON_CALL(blueprint_manager_, GetBlueprint("blueprint-1"))
      .WillByDefault(Return(absl::NotFoundError("blueprint is gone")));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->RegenerateGeneratedProp(regeneration_).code(), absl::StatusCode::kNotFound);
}

TEST_F(GeneratedPropRegenerationTest, RecipeFailureRestoresTheSpriteSnapshot) {
  InSequence sequence;
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::InternalError("recipe write failed")));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  const absl::Status status = api_->RegenerateGeneratedProp(regeneration_);
  EXPECT_THAT(std::string(status.message()), HasSubstr("recipe write failed"));
}

TEST_F(GeneratedPropRegenerationTest, PixelFailureRestoresRecipeThenSprite) {
  InSequence sequence;
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _))
      .WillOnce(Return(absl::InternalError("pixel replacement failed")));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).WillOnce(Return(absl::OkStatus()));

  const absl::Status status = api_->RegenerateGeneratedProp(regeneration_);
  EXPECT_THAT(std::string(status.message()), HasSubstr("pixel replacement failed"));
}

TEST_F(GeneratedPropRegenerationTest, ReportsFailedRollbackAlongsidePixelFailure) {
  EXPECT_CALL(sprite_manager_, SaveSprite(_))
      .WillOnce(Return(absl::OkStatus()))
      .WillOnce(Return(absl::InternalError("sprite restore failed")));
  EXPECT_CALL(prop_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::OkStatus()))
      .WillOnce(Return(absl::InternalError("recipe restore failed")));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _))
      .WillOnce(Return(absl::InternalError("pixel replacement failed")));

  const absl::Status status = api_->RegenerateGeneratedProp(regeneration_);
  EXPECT_THAT(std::string(status.message()), HasSubstr("pixel replacement failed"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("recipe restore failed"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("sprite restore failed"));
}

}  // namespace
}  // namespace zebes
