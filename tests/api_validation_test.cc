#include "api/api.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "resources/blueprint_manager_mock.h"
#include "resources/collider_manager_mock.h"
#include "resources/level_manager_mock.h"
#include "resources/sprite_manager_mock.h"
#include "resources/terrain_recipe_manager_mock.h"
#include "resources/texture_manager_mock.h"
#include "resources/tileset_manager_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
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
        .tileset_manager = &tileset_manager_,
        .terrain_recipe_manager = &terrain_recipe_manager_,
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
  NiceMock<TilesetManagerMock> tileset_manager_;
  NiceMock<TerrainRecipeManagerMock> terrain_recipe_manager_;
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

TEST_F(ApiValidationTest, DeleteTexture_InUseByATileset_ReturnsError) {
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
TEST_F(ApiValidationTest, DeleteTexture_InUseByARecipe_ReturnsError) {
  const std::string texture_id = "test_texture";
  EXPECT_CALL(terrain_recipe_manager_, GetAllRecipes())
      .WillOnce(Return(std::vector<TerrainRecipe>{
          TerrainRecipe{.id = "rc", .name = "Cave", .texture_id = texture_id}}));
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  EXPECT_EQ(api_->DeleteTexture(texture_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteTexture_NotInUse_CallsDelete) {
  const std::string texture_id = "test_texture";
  EXPECT_CALL(texture_manager_, DeleteTexture(texture_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteTexture(texture_id));
}

TEST_F(ApiValidationTest, DeleteSprite_InUseInBlueprint_ReturnsError) {
  const std::string sprite_id = "test_sprite";
  EXPECT_CALL(blueprint_manager_, GetAllBlueprints())
      .WillOnce(Return(std::vector<Blueprint>{BlueprintUsing(sprite_id, "")}));
  EXPECT_CALL(sprite_manager_, DeleteSprite(_)).Times(0);

  EXPECT_EQ(api_->DeleteSprite(sprite_id).code(), absl::StatusCode::kFailedPrecondition);
}

// An entity carries its own sprite ID, so a level can hold the last reference to
// a sprite no blueprint mentions. The old blueprint-only check missed this.
TEST_F(ApiValidationTest, DeleteSprite_PlacedInALevel_ReturnsError) {
  const std::string sprite_id = "test_sprite";
  Level level = LevelUsingTileset("");
  Entity entity;
  entity.id = 3;
  entity.sprite_id = sprite_id;
  level.AddEntity(std::move(entity));
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));
  EXPECT_CALL(sprite_manager_, DeleteSprite(_)).Times(0);

  EXPECT_EQ(api_->DeleteSprite(sprite_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteSprite_NotInUse_CallsDelete) {
  const std::string sprite_id = "test_sprite";
  EXPECT_CALL(sprite_manager_, DeleteSprite(sprite_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteSprite(sprite_id));
}

TEST_F(ApiValidationTest, DeleteCollider_InUseInBlueprint_ReturnsError) {
  const std::string collider_id = "test_collider";
  EXPECT_CALL(blueprint_manager_, GetAllBlueprints())
      .WillOnce(Return(std::vector<Blueprint>{BlueprintUsing("", collider_id)}));
  EXPECT_CALL(collider_manager_, DeleteCollider(_)).Times(0);

  EXPECT_EQ(api_->DeleteCollider(collider_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteCollider_NotInUse_CallsDelete) {
  const std::string collider_id = "test_collider";
  EXPECT_CALL(collider_manager_, DeleteCollider(collider_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteCollider(collider_id));
}

// Nothing checked this before: deleting a tileset a level painted through left
// that level naming tile IDs it could no longer resolve, which stops its
// viewport rendering rather than degrading it.
TEST_F(ApiValidationTest, DeleteTileset_BoundToALevel_ReturnsError) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{LevelUsingTileset(tileset_id)}));
  EXPECT_CALL(tileset_manager_, DeleteTileset(_)).Times(0);

  const absl::Status status = api_->DeleteTileset(tileset_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
}

TEST_F(ApiValidationTest, DeleteTileset_Unreferenced_CallsDelete) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(tileset_manager_, DeleteTileset(tileset_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteTileset(tileset_id));
}

// The tile hole. Deleting a painted tile is not recoverable by re-adding it:
// NextTileId is max+1, so the new tile gets a new ID while the level still names
// the old one, and the level stops rendering rather than losing a cell.
TEST_F(ApiValidationTest, CheckTileDeletable_PaintedInALevel_ReturnsError) {
  const std::string tileset_id = "test_tileset";
  Level level = LevelUsingTileset(tileset_id);
  TileChunk chunk;
  chunk.tiles[0] = 7;
  chunk.tiles[1] = 7;
  level.tile_chunks[0] = chunk;
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));

  const absl::Status status = api_->CheckTileDeletable(tileset_id, 7);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("Cave Level"));
  EXPECT_THAT(std::string(status.message()), HasSubstr("2 painted cells"));
}

// A tile nothing painted stays one click away, which is the whole reason tile
// deletion is not confirmed.
TEST_F(ApiValidationTest, CheckTileDeletable_Unpainted_Passes) {
  const std::string tileset_id = "test_tileset";
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{LevelUsingTileset(tileset_id)}));

  EXPECT_OK(api_->CheckTileDeletable(tileset_id, 7));
}

// Tile IDs are bare integers with no tileset qualifier, so the same number in a
// level bound elsewhere is different artwork and not a reference.
TEST_F(ApiValidationTest, CheckTileDeletable_IgnoresLevelsBoundToAnotherTileset) {
  Level level = LevelUsingTileset("some_other_tileset");
  TileChunk chunk;
  chunk.tiles[0] = 7;
  level.tile_chunks[0] = chunk;
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

TEST_F(ApiValidationTest, DeleteBlueprint_PlacedInALevel_ReturnsError) {
  const std::string blueprint_id = "test_blueprint";
  Level level = LevelUsingTileset("");
  Entity entity;
  entity.id = 9;
  entity.blueprint_id = blueprint_id;
  level.AddEntity(std::move(entity));
  EXPECT_CALL(level_manager_, GetAllLevels())
      .WillOnce(Return(std::vector<Level>{std::move(level)}));
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(_)).Times(0);

  EXPECT_EQ(api_->DeleteBlueprint(blueprint_id).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteBlueprint_Unplaced_CallsDelete) {
  const std::string blueprint_id = "test_blueprint";
  EXPECT_CALL(blueprint_manager_, DeleteBlueprint(blueprint_id)).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteBlueprint(blueprint_id));
}

}  // namespace
}  // namespace zebes
