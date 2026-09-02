#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "artwork/delete_animation_frame_set_asset.h"
#include "artwork/prepare_animation_frame_set_asset.h"
#include "artwork/regenerate_animation_frame_set_asset.h"
#include "common/image_digest.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr RgbaColor kBody{32, 64, 128, 255};

void PaintPixel(RgbaImage* image, int x, int y, RgbaColor color) {
  const size_t offset = (static_cast<size_t>(y) * image->width + x) * 4;
  image->pixels[offset + 0] = color.r;
  image->pixels[offset + 1] = color.g;
  image->pixels[offset + 2] = color.b;
  image->pixels[offset + 3] = color.a;
}

RgbaImage SourcePixels() {
  RgbaImage image{
      .width = 4,
      .height = 4,
      .pixels = std::vector<uint8_t>(4 * 4 * 4, 0),
  };
  PaintPixel(&image, 1, 2, kBody);
  PaintPixel(&image, 2, 2, kBody);
  return image;
}

SourceArtwork SourceFor(const RgbaImage& pixels) {
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

AnimationFrameSetStyle Style() {
  return {
      .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
      .palette = {kBody},
  };
}

AnimationFrameSetPipelineConfig Pipeline() {
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

Blueprint TargetBlueprint() {
  return {
      .id = "blueprint-id",
      .name = "Player",
      .states =
          {
              Blueprint::State{
                  .key = "run-left",
                  .name = "Run Left",
                  .collider_id = "body-collider",
                  .sprite_id = "run-placeholder",
              },
              Blueprint::State{
                  .key = "idle-left",
                  .name = "Idle Left",
                  .collider_id = "body-collider",
                  .sprite_id = "idle-placeholder",
              },
          },
  };
}

PrepareAnimationFrameSetAssetRequest CreateRequest() {
  return {
      .name = "Run Left",
      .style = Style(),
      .pipeline = Pipeline(),
      .ids =
          AnimationFrameSetAssetIds{
              .texture_id = "texture-id",
              .sprite_id = "sprite-id",
              .recipe_id = "recipe-id",
          },
      .blueprint_state_keys = {"run-left"},
  };
}

absl::StatusOr<PreparedAnimationFrameSetAsset> PreparedCreate() {
  const RgbaImage pixels = SourcePixels();
  return PrepareAnimationFrameSetAsset(SourceFor(pixels), pixels, TargetBlueprint(),
                                       CreateRequest());
}

TEST(AnimationFrameSetAssetTest, PreparationReturnsCompleteBindingChange) {
  ASSERT_OK_AND_ASSIGN(const PreparedAnimationFrameSetAsset prepared, PreparedCreate());

  EXPECT_EQ(prepared.texture.id, "texture-id");
  EXPECT_EQ(prepared.texture.path, "textures/animation_frame_sets/texture-id.png");
  EXPECT_EQ(prepared.sprite.texture_id, prepared.texture.id);
  EXPECT_EQ(prepared.sprite.playback_mode, SpritePlaybackMode::kLoop);
  EXPECT_EQ(prepared.sprite.frames, prepared.artwork.sprite_frames);
  ASSERT_EQ(prepared.recipe.blueprint_bindings.size(), 1);
  EXPECT_EQ(prepared.recipe.blueprint_bindings[0].state_key, "run-left");
  EXPECT_EQ(prepared.recipe.blueprint_bindings[0].previous_sprite_id, "run-placeholder");
  EXPECT_EQ(prepared.updated_blueprint.states[0].sprite_id, "sprite-id");
  EXPECT_EQ(prepared.updated_blueprint.states[0].collider_id, "body-collider");
  EXPECT_EQ(prepared.updated_blueprint.states[1], prepared.blueprint_snapshot.states[1]);
}

TEST(AnimationFrameSetAssetTest, RejectsGeneratedRetainedSource) {
  const RgbaImage pixels = SourcePixels();
  SourceArtwork source = SourceFor(pixels);
  source.provenance = GeneratedArtworkProvenance{
      .provider = "provider",
      .model = "model",
      .submitted_prompt = "prompt",
      .generated_at_utc = "2026-09-01T00:00:00Z",
  };

  const absl::Status status =
      PrepareAnimationFrameSetAsset(source, pixels, TargetBlueprint(), CreateRequest()).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(AnimationFrameSetAssetTest, RegenerationPreservesIdsAndMovesOnlyOwnedBinding) {
  ASSERT_OK_AND_ASSIGN(const PreparedAnimationFrameSetAsset created, PreparedCreate());
  AnimationFrameSetPipelineConfig updated_pipeline = Pipeline();
  updated_pipeline.playback_mode = SpritePlaybackMode::kHoldLast;
  updated_pipeline.frames_per_cycle = {7};

  ASSERT_OK_AND_ASSIGN(
      const PreparedAnimationFrameSetRegeneration regenerated,
      PrepareAnimationFrameSetRegeneration(created.source_snapshot, SourcePixels(), created.recipe,
                                           created.texture, created.artwork.packed_texture,
                                           created.sprite, created.updated_blueprint,
                                           AnimationFrameSetRegenerationSettings{
                                               .style = Style(),
                                               .pipeline = updated_pipeline,
                                               .blueprint_state_keys = {"idle-left"},
                                           }));

  EXPECT_EQ(regenerated.updated_recipe.id, created.recipe.id);
  EXPECT_EQ(regenerated.updated_recipe.texture_id, created.recipe.texture_id);
  EXPECT_EQ(regenerated.updated_recipe.sprite_id, created.recipe.sprite_id);
  EXPECT_EQ(regenerated.updated_recipe.blueprint_id, created.recipe.blueprint_id);
  EXPECT_EQ(regenerated.updated_sprite.playback_mode, SpritePlaybackMode::kHoldLast);
  EXPECT_EQ(regenerated.updated_blueprint.states[0].sprite_id, "run-placeholder");
  EXPECT_EQ(regenerated.updated_blueprint.states[1].sprite_id, "sprite-id");
  EXPECT_EQ(regenerated.updated_blueprint.states[0].collider_id, "body-collider");
  EXPECT_EQ(regenerated.updated_blueprint.states[1].collider_id, "body-collider");
  ASSERT_EQ(regenerated.updated_recipe.blueprint_bindings.size(), 1);
  EXPECT_EQ(regenerated.updated_recipe.blueprint_bindings[0].state_key, "idle-left");
  EXPECT_EQ(regenerated.updated_recipe.blueprint_bindings[0].previous_sprite_id,
            "idle-placeholder");
}

TEST(AnimationFrameSetAssetTest, DeletionPreparationRestoresPriorBindingOnly) {
  ASSERT_OK_AND_ASSIGN(const PreparedAnimationFrameSetAsset created, PreparedCreate());

  ASSERT_OK_AND_ASSIGN(
      const PreparedAnimationFrameSetDeletion deletion,
      PrepareAnimationFrameSetDeletion(created.source_snapshot, created.recipe, created.texture,
                                       created.artwork.packed_texture, created.sprite,
                                       created.updated_blueprint));

  EXPECT_EQ(deletion.updated_blueprint, created.blueprint_snapshot);
}

TEST(AnimationFrameSetAssetTest, RegenerationRefusesChangedOwnedBinding) {
  ASSERT_OK_AND_ASSIGN(const PreparedAnimationFrameSetAsset created, PreparedCreate());
  Blueprint changed = created.updated_blueprint;
  changed.states[0].sprite_id = "different-sprite";

  const absl::Status status =
      PrepareAnimationFrameSetRegeneration(created.source_snapshot, SourcePixels(), created.recipe,
                                           created.texture, created.artwork.packed_texture,
                                           created.sprite, changed,
                                           AnimationFrameSetRegenerationSettings{
                                               .style = Style(),
                                               .pipeline = Pipeline(),
                                               .blueprint_state_keys = {"run-left"},
                                           })
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
