#include <string>

#include "absl/status/status.h"
#include "engine/texture_handle.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "resources/blueprint_manager_mock.h"
#include "resources/collider_manager_mock.h"
#include "resources/level_asset_loader.h"
#include "resources/level_manager_mock.h"
#include "resources/parallax_theme_manager_mock.h"
#include "resources/sprite_manager_mock.h"
#include "resources/texture_manager_mock.h"
#include "resources/tileset_manager_mock.h"

namespace zebes {
namespace {

using ::testing::Return;

class LoadedLevelAssetsTest : public ::testing::Test {
 protected:
  LevelAssetLoaderOptions Resources() {
    return {
        .levels = levels_,
        .tilesets = tilesets_,
        .sprites = sprites_,
        .colliders = colliders_,
        .blueprints = blueprints_,
        .parallax_themes = parallax_themes_,
        .textures = textures_,
    };
  }

  LevelManagerMock levels_;
  TilesetManagerMock tilesets_;
  SpriteManagerMock sprites_;
  ColliderManagerMock colliders_;
  BlueprintManagerMock blueprints_;
  ParallaxThemeManagerMock parallax_themes_;
  TextureManagerMock textures_;
};

TEST_F(LoadedLevelAssetsTest, ResolvesDefinitionsAndRenderBindingsIntoSeparateGraphs) {
  Level level{
      .id = "level",
      .tileset_id = "tileset",
      .zones = {{.id = 4, .theme_id = "theme"}},
  };
  level.layers.front().entities.emplace(
      7, Entity{.id = 7, .sprite_id = "sprite", .collider_id = "collider"});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  Sprite sprite{.id = "sprite", .texture_id = "sprite-texture"};
  Collider collider{.id = "collider"};
  ParallaxTheme theme{
      .id = "theme",
      .layers = {{.elements = {{.id = 8, .texture_id = "parallax-texture"}}}},
  };
  int texture_owner = 0;
  const TextureHandle atlas = TextureHandleAccess::Create(1, &texture_owner);
  const TextureHandle sprite_texture = TextureHandleAccess::Create(2, &texture_owner);
  const TextureHandle parallax_texture = TextureHandleAccess::Create(3, &texture_owner);

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(sprites_, GetSprite("sprite")).WillOnce(Return(&sprite));
  EXPECT_CALL(colliders_, GetCollider("collider")).WillOnce(Return(&collider));
  EXPECT_CALL(parallax_themes_, GetTheme("theme")).WillOnce(Return(&theme));
  EXPECT_CALL(textures_, GetTextureHandle("atlas")).WillOnce(Return(atlas));
  EXPECT_CALL(textures_, GetTextureHandle("sprite-texture")).WillOnce(Return(sprite_texture));
  EXPECT_CALL(textures_, GetTextureHandle("parallax-texture")).WillOnce(Return(parallax_texture));

  ASSERT_OK_AND_ASSIGN(const LoadedLevelAssets assets, ResolveLevelAssets(Resources(), "level"));

  EXPECT_EQ(assets.content.level, level);
  EXPECT_EQ(assets.content.tileset, tileset);
  EXPECT_EQ(assets.content.sprites.at("sprite"), sprite);
  EXPECT_EQ(assets.content.colliders.at("collider"), collider);
  EXPECT_EQ(assets.content.parallax_themes.at("theme"), theme);
  EXPECT_EQ(assets.rendering.tileset_atlas, atlas);
  EXPECT_EQ(assets.rendering.sprite_textures.at("sprite"), sprite_texture);
  EXPECT_EQ(assets.rendering.parallax_textures.at("parallax-texture"), parallax_texture);
}

TEST_F(LoadedLevelAssetsTest, FailsBeforePublishingAnIncompleteTextureGraph) {
  Level level{.id = "level", .tileset_id = "tileset"};
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(textures_, GetTextureHandle("atlas")).WillOnce(Return(TextureHandle{}));

  const absl::Status status = ResolveLevelAssets(Resources(), "level").status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "level tileset texture is not loaded");
}

TEST_F(LoadedLevelAssetsTest, ResolvesEveryStateAssetForPlacedBlueprints) {
  Level level{.id = "level", .tileset_id = "tileset"};
  level.layers.front().entities.emplace(7, Entity{.id = 7,
                                                  .blueprint_id = "blueprint",
                                                  .blueprint_state_index = 0,
                                                  .sprite_id = "sprite-a",
                                                  .collider_id = "collider-a"});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  Blueprint blueprint{
      .id = "blueprint",
      .states = {
          {.key = "idle", .name = "idle", .sprite_id = "sprite-a", .collider_id = "collider-a"},
          {.key = "active",
           .name = "active",
           .sprite_id = "sprite-b",
           .collider_id = "collider-b"}}};
  Sprite sprite_a{.id = "sprite-a", .texture_id = "texture-a"};
  Sprite sprite_b{.id = "sprite-b", .texture_id = "texture-b"};
  Collider collider_a{.id = "collider-a"};
  Collider collider_b{.id = "collider-b"};
  int texture_owner = 0;

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(blueprints_, GetBlueprint("blueprint")).WillOnce(Return(&blueprint));
  EXPECT_CALL(sprites_, GetSprite("sprite-a")).WillOnce(Return(&sprite_a));
  EXPECT_CALL(sprites_, GetSprite("sprite-b")).WillOnce(Return(&sprite_b));
  EXPECT_CALL(colliders_, GetCollider("collider-a")).WillOnce(Return(&collider_a));
  EXPECT_CALL(colliders_, GetCollider("collider-b")).WillOnce(Return(&collider_b));
  EXPECT_CALL(textures_, GetTextureHandle("atlas"))
      .WillOnce(Return(TextureHandleAccess::Create(1, &texture_owner)));
  EXPECT_CALL(textures_, GetTextureHandle("texture-a"))
      .WillOnce(Return(TextureHandleAccess::Create(2, &texture_owner)));
  EXPECT_CALL(textures_, GetTextureHandle("texture-b"))
      .WillOnce(Return(TextureHandleAccess::Create(3, &texture_owner)));

  ASSERT_OK_AND_ASSIGN(const LoadedLevelAssets assets, ResolveLevelAssets(Resources(), "level"));

  EXPECT_EQ(assets.content.blueprints.at("blueprint"), blueprint);
  EXPECT_EQ(assets.content.sprites.size(), 2);
  EXPECT_EQ(assets.content.colliders.size(), 2);
  EXPECT_EQ(assets.rendering.sprite_textures.size(), 2);
}

TEST_F(LoadedLevelAssetsTest, RejectsEntityThatDoesNotMatchItsSelectedBlueprintState) {
  Level level{.id = "level", .tileset_id = "tileset"};
  level.layers.front().entities.emplace(7, Entity{.id = 7,
                                                  .blueprint_id = "blueprint",
                                                  .sprite_id = "entity-sprite",
                                                  .collider_id = "entity-collider"});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  Blueprint blueprint{
      .id = "blueprint",
      .states = {{.key = "idle",
                  .name = "idle",
                  .sprite_id = "state-sprite",
                  .collider_id = "state-collider"}},
  };
  Sprite entity_sprite{.id = "entity-sprite", .texture_id = "entity-texture"};
  Collider entity_collider{.id = "entity-collider"};
  int texture_owner = 0;

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(sprites_, GetSprite("entity-sprite")).WillOnce(Return(&entity_sprite));
  EXPECT_CALL(colliders_, GetCollider("entity-collider")).WillOnce(Return(&entity_collider));
  EXPECT_CALL(blueprints_, GetBlueprint("blueprint")).WillOnce(Return(&blueprint));
  EXPECT_CALL(textures_, GetTextureHandle("atlas"))
      .WillOnce(Return(TextureHandleAccess::Create(1, &texture_owner)));
  EXPECT_CALL(textures_, GetTextureHandle("entity-texture"))
      .WillOnce(Return(TextureHandleAccess::Create(2, &texture_owner)));

  const absl::Status status = ResolveLevelAssets(Resources(), "level").status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "entity 7 does not match its selected blueprint state assets");
}

TEST_F(LoadedLevelAssetsTest, RejectsMissingPlacedBlueprint) {
  Level level{.id = "level", .tileset_id = "tileset"};
  level.layers.front().entities.emplace(7, Entity{.id = 7, .blueprint_id = "missing"});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(blueprints_, GetBlueprint("missing"))
      .WillOnce(Return(absl::NotFoundError("blueprint missing")));
  EXPECT_CALL(textures_, GetTextureHandle("atlas"))
      .WillOnce(Return(TextureHandleAccess::Create(1, nullptr)));

  EXPECT_EQ(ResolveLevelAssets(Resources(), "level").status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LoadedLevelAssetsTest, RejectsMissingAssetReferencedByBlueprintState) {
  Level level{.id = "level", .tileset_id = "tileset"};
  level.layers.front().entities.emplace(7, Entity{.id = 7, .blueprint_id = "blueprint"});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  Blueprint blueprint{
      .id = "blueprint",
      .states = {{.key = "idle", .name = "idle"},
                 {.key = "active", .name = "active", .sprite_id = "missing-sprite"}},
  };

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(blueprints_, GetBlueprint("blueprint")).WillOnce(Return(&blueprint));
  EXPECT_CALL(sprites_, GetSprite("missing-sprite"))
      .WillOnce(Return(absl::NotFoundError("sprite missing")));
  EXPECT_CALL(textures_, GetTextureHandle("atlas"))
      .WillOnce(Return(TextureHandleAccess::Create(1, nullptr)));

  EXPECT_EQ(ResolveLevelAssets(Resources(), "level").status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LoadedLevelAssetsTest, RejectsAnOutOfRangeAuthoredBlueprintState) {
  Level level{.id = "level", .tileset_id = "tileset"};
  level.layers.front().entities.emplace(
      7, Entity{.id = 7, .blueprint_id = "blueprint", .blueprint_state_index = 1});
  Tileset tileset{.id = "tileset", .texture_id = "atlas"};
  Blueprint blueprint{.id = "blueprint", .states = {{.key = "idle", .name = "idle"}}};

  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&level));
  EXPECT_CALL(tilesets_, GetTileset("tileset")).WillOnce(Return(&tileset));
  EXPECT_CALL(blueprints_, GetBlueprint("blueprint")).WillOnce(Return(&blueprint));
  EXPECT_CALL(textures_, GetTextureHandle("atlas"))
      .WillOnce(Return(TextureHandleAccess::Create(1, nullptr)));

  const absl::Status status = ResolveLevelAssets(Resources(), "level").status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "entity 7 blueprint state index is out of range");
}

TEST_F(LoadedLevelAssetsTest, RejectsEmptyAndMismatchedLevelLookups) {
  EXPECT_EQ(ResolveLevelAssets(Resources(), "").status().code(),
            absl::StatusCode::kInvalidArgument);

  Level wrong_level{.id = "different"};
  EXPECT_CALL(levels_, GetLevel("level")).WillOnce(Return(&wrong_level));
  EXPECT_EQ(ResolveLevelAssets(Resources(), "level").status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
