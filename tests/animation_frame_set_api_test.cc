#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "api/api.h"
#include "artwork/delete_animation_frame_set_asset.h"
#include "artwork/prepare_animation_frame_set_asset.h"
#include "artwork/regenerate_animation_frame_set_asset.h"
#include "common/image_digest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "resources/animation_frame_set_recipe_manager_mock.h"
#include "resources/blueprint_manager_mock.h"
#include "resources/collider_manager_mock.h"
#include "resources/level_manager_mock.h"
#include "resources/parallax_artwork_recipe_manager_mock.h"
#include "resources/parallax_theme_manager_mock.h"
#include "resources/prop_recipe_manager_mock.h"
#include "resources/source_artwork_manager_mock.h"
#include "resources/sprite_manager_mock.h"
#include "resources/terrain_recipe_manager_mock.h"
#include "resources/texture_manager_mock.h"
#include "resources/tileset_manager_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

constexpr RgbaColor kBody{32, 64, 128, 255};

RgbaImage FrameSetPixels() {
  RgbaImage image{
      .width = 4,
      .height = 4,
      .pixels = std::vector<uint8_t>(4 * 4 * 4, 0),
  };
  for (int x = 1; x <= 2; ++x) {
    const size_t offset = (static_cast<size_t>(2) * image.width + x) * 4;
    image.pixels[offset + 0] = kBody.r;
    image.pixels[offset + 1] = kBody.g;
    image.pixels[offset + 2] = kBody.b;
    image.pixels[offset + 3] = kBody.a;
  }
  return image;
}

SourceArtwork FrameSetSource(const RgbaImage& pixels) {
  const absl::StatusOr<std::string> digest = RgbaImageDigest(pixels);
  EXPECT_TRUE(digest.ok());
  return {
      .id = "source-id",
      .name = "Run Source",
      .source_path = "source_artworks/source-id.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "run.png",
              .imported_at_utc = "2026-09-01T00:00:00Z",
          },
      .width = pixels.width,
      .height = pixels.height,
      .content_digest = digest.ok() ? *digest : "",
  };
}

AnimationFrameSetStyle FrameSetStyle() {
  return {
      .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
      .palette = {kBody},
  };
}

AnimationFrameSetPipelineConfig FrameSetPipeline() {
  return {
      .sheet =
          AnimationFrameSetSheetLayout{
              .grid_x = 0,
              .grid_y = 0,
              .cell_width = 4,
              .cell_height = 4,
              .column_gap = 0,
              .row_gap = 0,
              .columns = 1,
              .rows = 1,
          },
      .output_width = 4,
      .output_height = 4,
      .origin_x = 2,
      .origin_y = 3,
      .contact_line_y = 3,
      .render_scale = 1,
      .contact_tolerance = 1,
      .minimum_visible_pixels = 1,
      .maximum_horizontal_anchor_drift = 2,
      .maximum_vertical_anchor_drift = 2,
      .packing_columns = 1,
      .playback_mode = SpritePlaybackMode::kLoop,
      .frames_per_cycle = {5},
      .planted_frames = {false},
  };
}

Blueprint FrameSetBlueprint() {
  return {
      .id = "blueprint-id",
      .name = "Player",
      .states = {Blueprint::State{
          .key = "run-left",
          .name = "Run Left",
          .collider_id = "body-collider",
          .sprite_id = "placeholder-id",
      }},
  };
}

class AnimationFrameSetApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(
        api_, Api::Create({
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
                  .parallax_artwork_recipe_manager = &parallax_artwork_recipe_manager_,
                  .animation_frame_set_recipe_manager = &animation_frame_set_recipe_manager_,
              }));
    source_pixels_ = FrameSetPixels();
    const SourceArtwork source = FrameSetSource(source_pixels_);
    current_blueprint_ = FrameSetBlueprint();
    ASSERT_OK_AND_ASSIGN(created_,
                         PrepareAnimationFrameSetAsset(source, source_pixels_, current_blueprint_,
                                                       PrepareAnimationFrameSetAssetRequest{
                                                           .name = "Run Left",
                                                           .style = FrameSetStyle(),
                                                           .pipeline = FrameSetPipeline(),
                                                           .ids =
                                                               AnimationFrameSetAssetIds{
                                                                   .texture_id = "texture-id",
                                                                   .sprite_id = "sprite-id",
                                                                   .recipe_id = "recipe-id",
                                                               },
                                                           .blueprint_state_keys = {"run-left"},
                                                       }));
    placeholder_sprite_ = {
        .id = "placeholder-id",
        .name = "Placeholder",
        .texture_id = "placeholder-texture",
    };

    ON_CALL(source_artwork_manager_, GetArtwork("source-id"))
        .WillByDefault(Return(&created_.source_snapshot));
    ON_CALL(source_artwork_manager_, ReadArtworkPixels("source-id"))
        .WillByDefault(Return(source_pixels_));
    ON_CALL(blueprint_manager_, GetBlueprint("blueprint-id"))
        .WillByDefault(Return(&current_blueprint_));
    ON_CALL(sprite_manager_, GetSprite("placeholder-id"))
        .WillByDefault(Return(&placeholder_sprite_));
    ON_CALL(texture_manager_, GetTexture("texture-id"))
        .WillByDefault(Return(absl::NotFoundError("missing Texture")));
    ON_CALL(sprite_manager_, GetSprite("sprite-id"))
        .WillByDefault(Return(absl::NotFoundError("missing Sprite")));
    ON_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
        .WillByDefault(Return(absl::NotFoundError("missing recipe")));
    ON_CALL(texture_manager_, PreflightGeneratedTexture(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(sprite_manager_, PreflightSpriteWithId(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(animation_frame_set_recipe_manager_, PreflightRecipeWithId(_))
        .WillByDefault(Return(absl::OkStatus()));
  }

  EngineConfig config_;
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
  NiceMock<ParallaxArtworkRecipeManagerMock> parallax_artwork_recipe_manager_;
  NiceMock<AnimationFrameSetRecipeManagerMock> animation_frame_set_recipe_manager_;
  std::unique_ptr<Api> api_;
  RgbaImage source_pixels_;
  Sprite placeholder_sprite_;
  Blueprint current_blueprint_;
  PreparedAnimationFrameSetAsset created_;
};

TEST_F(AnimationFrameSetApiTest, CreatePublishesDependenciesThenBindingThenRecipe) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(created_.updated_blueprint))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::OkStatus()));

  ASSERT_OK_AND_ASSIGN(const std::string id, api_->CreateAnimationFrameSet(created_));
  EXPECT_EQ(id, "recipe-id");
}

TEST_F(AnimationFrameSetApiTest, CreateRefusesStaleBlueprintBeforeWrites) {
  current_blueprint_.states[0].collider_id = "edited-collider";
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->CreateAnimationFrameSet(created_).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(AnimationFrameSetApiTest, CreateTextureFailureWritesNothingElse) {
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _))
      .WillOnce(Return(absl::InternalError("Texture failed")));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).Times(0);

  EXPECT_FALSE(api_->CreateAnimationFrameSet(created_).ok());
}

TEST_F(AnimationFrameSetApiTest, CreateSpriteFailureDeletesTexture) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_))
      .WillOnce(Return(absl::InternalError("Sprite failed")));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateAnimationFrameSet(created_).ok());
}

TEST_F(AnimationFrameSetApiTest, CreateBlueprintFailureUnwindsSpriteThenTexture) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(created_.updated_blueprint))
      .WillOnce(Return(absl::InternalError("Blueprint failed")));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateAnimationFrameSet(created_).ok());
}

TEST_F(AnimationFrameSetApiTest, CreateRecipeFailureUnwindsInReverseOrder) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, _, _, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(created_.updated_blueprint))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::InternalError("recipe failed")));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(created_.blueprint_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id")).WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->CreateAnimationFrameSet(created_).ok());
}

class AnimationFrameSetRegenerationApiTest : public AnimationFrameSetApiTest {
 protected:
  void SetUp() override {
    AnimationFrameSetApiTest::SetUp();
    current_blueprint_ = created_.updated_blueprint;
    current_texture_ = created_.texture;
    current_sprite_ = created_.sprite;
    current_recipe_ = created_.recipe;
    AnimationFrameSetPipelineConfig pipeline = FrameSetPipeline();
    pipeline.playback_mode = SpritePlaybackMode::kHoldLast;
    pipeline.frames_per_cycle = {7};
    ASSERT_OK_AND_ASSIGN(
        regenerated_, PrepareAnimationFrameSetRegeneration(created_.source_snapshot, source_pixels_,
                                                           current_recipe_, current_texture_,
                                                           created_.artwork.packed_texture,
                                                           current_sprite_, current_blueprint_,
                                                           AnimationFrameSetRegenerationSettings{
                                                               .style = FrameSetStyle(),
                                                               .pipeline = pipeline,
                                                               .blueprint_state_keys = {"run-left"},
                                                           }));

    ON_CALL(texture_manager_, GetTexture("texture-id")).WillByDefault(Return(&current_texture_));
    ON_CALL(texture_manager_, ReadTexturePixels("texture-id"))
        .WillByDefault(Return(created_.artwork.packed_texture));
    ON_CALL(sprite_manager_, GetSprite("sprite-id")).WillByDefault(Return(&current_sprite_));
    ON_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
        .WillByDefault(Return(&current_recipe_));
  }

  Texture current_texture_;
  Sprite current_sprite_;
  AnimationFrameSetRecipe current_recipe_;
  PreparedAnimationFrameSetRegeneration regenerated_;
};

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateCommitsInDependencyOrder) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.updated_sprite))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(regenerated_.updated_blueprint))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->RegenerateAnimationFrameSet(regenerated_));
}

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateRefusesStaleRecipeBeforeWrites) {
  current_recipe_.name = "Changed";
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  EXPECT_EQ(api_->RegenerateAnimationFrameSet(regenerated_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateTextureFailureWritesNothingElse) {
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::InternalError("Texture failed")));
  EXPECT_CALL(sprite_manager_, SaveSprite(_)).Times(0);

  EXPECT_FALSE(api_->RegenerateAnimationFrameSet(regenerated_).ok());
}

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateSpriteFailureRestoresTexture) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.updated_sprite))
      .WillOnce(Return(absl::InternalError("Sprite failed")));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->RegenerateAnimationFrameSet(regenerated_).ok());
}

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateRecipeFailureUnwindsSpriteThenTexture) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.updated_sprite))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::InternalError("recipe failed")));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.sprite_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->RegenerateAnimationFrameSet(regenerated_).ok());
}

TEST_F(AnimationFrameSetRegenerationApiTest, RegenerateBlueprintFailureUnwindsAllOutputs) {
  InSequence sequence;
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.updated_sprite))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(regenerated_.updated_blueprint))
      .WillOnce(Return(absl::InternalError("Blueprint failed")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, SaveRecipe(_))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, SaveSprite(regenerated_.sprite_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, ReplaceTexturePixels("texture-id", 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->RegenerateAnimationFrameSet(regenerated_).ok());
}

class AnimationFrameSetDeleteApiTest : public AnimationFrameSetRegenerationApiTest {
 protected:
  void SetUp() override {
    AnimationFrameSetRegenerationApiTest::SetUp();
    ASSERT_OK_AND_ASSIGN(deletion_,
                         PrepareAnimationFrameSetDeletion(
                             created_.source_snapshot, current_recipe_, current_texture_,
                             created_.artwork.packed_texture, current_sprite_, current_blueprint_));
    ON_CALL(blueprint_manager_, GetAllBlueprints())
        .WillByDefault(Return(std::vector<Blueprint>{current_blueprint_}));
    ON_CALL(sprite_manager_, GetAllSprites())
        .WillByDefault(Return(std::vector<Sprite>{current_sprite_, placeholder_sprite_}));
    ON_CALL(animation_frame_set_recipe_manager_, GetAllRecipes())
        .WillByDefault(Return(std::vector<AnimationFrameSetRecipe>{current_recipe_}));
  }

  PreparedAnimationFrameSetDeletion deletion_;
};

TEST_F(AnimationFrameSetDeleteApiTest, DeleteRefusesStaleBlueprintBeforeWrites) {
  current_blueprint_.states[0].collider_id = "edited-collider";
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe(_)).Times(0);

  EXPECT_EQ(api_->DeleteAnimationFrameSet(deletion_).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteRecipeFailureChangesNothing) {
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::InternalError("recipe delete failed")));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(_)).Times(0);

  EXPECT_FALSE(api_->DeleteAnimationFrameSet(deletion_).ok());
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteCommitsCompleteGraphInOrder) {
  InSequence sequence;
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(deletion_.updated_blueprint))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork("source-id"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(api_->DeleteAnimationFrameSet(deletion_));
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteBlueprintFailureRestoresRecipe) {
  InSequence sequence;
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(&current_recipe_));
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(deletion_.updated_blueprint))
      .WillOnce(Return(absl::InternalError("Blueprint failed")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->DeleteAnimationFrameSet(deletion_).ok());
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteSpriteFailureRestoresBlueprintThenRecipe) {
  Blueprint restored_binding = deletion_.updated_blueprint;
  InSequence sequence;
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(&current_recipe_));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id"))
      .WillOnce(Return(&current_blueprint_));
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(restored_binding))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id"))
      .WillOnce(Return(absl::InternalError("Sprite failed")));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id")).WillOnce(Return(&restored_binding));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(deletion_.blueprint_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->DeleteAnimationFrameSet(deletion_).ok());
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteTextureFailureRestoresInReverseOrder) {
  Blueprint restored_binding = deletion_.updated_blueprint;
  InSequence sequence;
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(&current_recipe_));
  EXPECT_CALL(texture_manager_, GetTexture("texture-id")).WillOnce(Return(&current_texture_));
  EXPECT_CALL(sprite_manager_, GetSprite("sprite-id")).WillOnce(Return(&current_sprite_));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id"))
      .WillOnce(Return(&current_blueprint_));
  EXPECT_CALL(sprite_manager_, GetSprite("placeholder-id")).WillOnce(Return(&placeholder_sprite_));
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(restored_binding))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id"))
      .WillOnce(Return(absl::InternalError("Texture failed")));
  EXPECT_CALL(texture_manager_, GetTexture("texture-id")).WillOnce(Return(&current_texture_));
  EXPECT_CALL(sprite_manager_, GetSprite("sprite-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(deletion_.sprite_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id")).WillOnce(Return(&restored_binding));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(deletion_.blueprint_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_FALSE(api_->DeleteAnimationFrameSet(deletion_).ok());
}

TEST_F(AnimationFrameSetDeleteApiTest, DeleteSourceFailureRestoresCompleteGraph) {
  Blueprint restored_binding = deletion_.updated_blueprint;
  InSequence sequence;
  EXPECT_CALL(source_artwork_manager_, GetArtwork("source-id"))
      .WillOnce(Return(&created_.source_snapshot));
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(&current_recipe_));
  EXPECT_CALL(texture_manager_, GetTexture("texture-id")).WillOnce(Return(&current_texture_));
  EXPECT_CALL(sprite_manager_, GetSprite("sprite-id")).WillOnce(Return(&current_sprite_));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id"))
      .WillOnce(Return(&current_blueprint_));
  EXPECT_CALL(sprite_manager_, GetSprite("placeholder-id")).WillOnce(Return(&placeholder_sprite_));
  EXPECT_CALL(animation_frame_set_recipe_manager_, DeleteRecipe("recipe-id"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(restored_binding))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, DeleteSprite("sprite-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, DeleteTexture("texture-id")).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(source_artwork_manager_, DeleteArtwork("source-id"))
      .WillOnce(Return(absl::InternalError("source delete failed")));
  EXPECT_CALL(source_artwork_manager_, GetArtwork("source-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(source_artwork_manager_, CreateArtworkWithId(_, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(texture_manager_, GetTexture("texture-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(texture_manager_, CreateGeneratedTexture(_, 4, 4, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(sprite_manager_, GetSprite("sprite-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(sprite_manager_, CreateSpriteWithId(deletion_.sprite_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(blueprint_manager_, GetBlueprint("blueprint-id")).WillOnce(Return(&restored_binding));
  EXPECT_CALL(blueprint_manager_, SaveBlueprint(deletion_.blueprint_snapshot))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(animation_frame_set_recipe_manager_, GetRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("deleted")));
  EXPECT_CALL(animation_frame_set_recipe_manager_, CreateRecipeWithId(_))
      .WillOnce(Return(absl::OkStatus()));

  const absl::Status status = api_->DeleteAnimationFrameSet(deletion_);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(std::string(status.message()), HasSubstr("source delete failed"));
}

}  // namespace
}  // namespace zebes
