#include "editor/sprite_editor/sprite_editor_model.h"

#include <gtest/gtest.h>

namespace zebes {
namespace {

TEST(SpriteEditorModelTest, OrderedCatalogPreservesDuplicateNames) {
  SpriteEditorModel model;
  model.SetSprites({{.id = "b", .name = "Hero"}, {.id = "a", .name = "Hero"}});
  ASSERT_EQ(model.sprites().size(), 2);
  EXPECT_EQ(model.sprites().begin()->first.display_name, "Hero");
  EXPECT_EQ(model.sprites().begin()->first.id, "a");
  EXPECT_EQ(model.sprites().begin()->second.id, "a");
}

TEST(SpriteEditorModelTest, FrameOperationsMaintainIndicesAndActiveSelection) {
  SpriteEditorModel model;
  model.BeginNewSprite();
  ASSERT_TRUE(model.AddFrame().ok());
  ASSERT_TRUE(model.AddFrame().ok());
  EXPECT_EQ(model.active_frame_index(), 1);
  ASSERT_TRUE(model.MoveFrameLeft(1).ok());
  EXPECT_EQ(model.active_frame_index(), 0);
  ASSERT_TRUE(model.DeleteFrame(0).ok());
  EXPECT_EQ(model.active_frame_index(), -1);
  ASSERT_EQ(model.sprite().frames.size(), 1);
  EXPECT_EQ(model.sprite().frames[0].index, 0);
}

TEST(SpriteEditorModelTest, BuildRequestsValidateModeAndTexture) {
  SpriteEditorModel model;
  model.BeginNewSprite();
  EXPECT_EQ(model.BuildCreateRequest().status().code(), absl::StatusCode::kInvalidArgument);
  model.sprite().texture_id = "texture";
  EXPECT_TRUE(model.BuildCreateRequest().ok());
  EXPECT_EQ(model.BuildUpdateRequest().status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SpriteEditorModelTest, ScaleClampAndResetAreDeterministic) {
  SpriteEditorModel model;
  model.SetSprites(
      {{.id = "sprite",
        .name = "Sprite",
        .frames = {{.texture_x = 20, .texture_y = 10, .texture_w = 30, .texture_h = 40}}}});
  ASSERT_TRUE(model.SelectSprite("sprite").ok());
  ASSERT_TRUE(model.ClampFrameToTexture(0, 32, 32).ok());
  ASSERT_TRUE(model.ApplyFrameScale(0, 2).ok());
  EXPECT_EQ(model.sprite().frames[0].texture_w, 12);
  EXPECT_EQ(model.sprite().frames[0].texture_h, 22);
  EXPECT_EQ(model.sprite().frames[0].render_w, 24);
  model.ResetFrames();
  EXPECT_EQ(model.sprite().frames[0].texture_w, 30);
}

TEST(SpriteEditorModelTest, AnimationPreviewUsesOffsetAwareBoundsForEveryFrame) {
  const std::vector<SpriteFrame> frames = {
      {.render_w = 20, .render_h = 10, .offset_x = -5, .offset_y = -3},
      {.render_w = 40, .render_h = 30, .offset_x = 10, .offset_y = 5},
  };

  absl::StatusOr<AnimationPreviewLayout> layout =
      SpriteEditorModel::CalculateAnimationPreviewLayout(frames, 0, 500.0f, 500.0f);

  ASSERT_TRUE(layout.ok());
  EXPECT_EQ(layout->bounds_left, -5);
  EXPECT_EQ(layout->bounds_top, -3);
  EXPECT_EQ(layout->bounds_right, 50);
  EXPECT_EQ(layout->bounds_bottom, 35);
  EXPECT_FLOAT_EQ(layout->canvas_width, 55.0f);
  EXPECT_FLOAT_EQ(layout->canvas_height, 38.0f);
  EXPECT_FLOAT_EQ(layout->frame_x, 0.0f);
  EXPECT_FLOAT_EQ(layout->frame_y, 0.0f);
}

TEST(SpriteEditorModelTest, AnimationPreviewFitsWithinLimitsWithoutUpscaling) {
  const std::vector<SpriteFrame> large_frame = {
      {.render_w = 1000, .render_h = 500},
  };
  absl::StatusOr<AnimationPreviewLayout> scaled =
      SpriteEditorModel::CalculateAnimationPreviewLayout(large_frame, 0, 200.0f, 150.0f);
  ASSERT_TRUE(scaled.ok());
  EXPECT_FLOAT_EQ(scaled->scale, 0.2f);
  EXPECT_FLOAT_EQ(scaled->canvas_width, 200.0f);
  EXPECT_FLOAT_EQ(scaled->canvas_height, 100.0f);

  const std::vector<SpriteFrame> small_frame = {
      {.render_w = 20, .render_h = 10},
  };
  absl::StatusOr<AnimationPreviewLayout> unscaled =
      SpriteEditorModel::CalculateAnimationPreviewLayout(small_frame, 0, 200.0f, 150.0f);
  ASSERT_TRUE(unscaled.ok());
  EXPECT_FLOAT_EQ(unscaled->scale, 1.0f);
}

TEST(SpriteEditorModelTest, AnimationPreviewRejectsInvalidCurrentFrame) {
  const std::vector<SpriteFrame> frames = {
      {.render_w = 0, .render_h = 10},
      {.render_w = 20, .render_h = 20},
  };

  EXPECT_EQ(
      SpriteEditorModel::CalculateAnimationPreviewLayout(frames, 0, 200.0f, 200.0f).status().code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      SpriteEditorModel::CalculateAnimationPreviewLayout(frames, 2, 200.0f, 200.0f).status().code(),
      absl::StatusCode::kOutOfRange);
}

}  // namespace
}  // namespace zebes
