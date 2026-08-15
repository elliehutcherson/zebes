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

  EXPECT_TRUE(api_->DeleteTexture(texture_id).ok());
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

  EXPECT_TRUE(api_->DeleteSprite(sprite_id).ok());
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

  EXPECT_TRUE(api_->DeleteCollider(collider_id).ok());
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

  EXPECT_TRUE(api_->DeleteTileset(tileset_id).ok());
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

  EXPECT_TRUE(api_->DeleteBlueprint(blueprint_id).ok());
}

}  // namespace
}  // namespace zebes
