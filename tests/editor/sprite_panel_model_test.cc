#include "editor/blueprint_editor/sprite_panel_model.h"

#include <gtest/gtest.h>

#include <vector>

namespace zebes {
namespace {

Sprite MakeSprite(std::string id, std::string name, int frame_count = 0) {
  Sprite sprite{.id = std::move(id), .name = std::move(name)};
  for (int i = 0; i < frame_count; ++i) {
    sprite.frames.push_back(SpriteFrame{.index = i, .texture_w = 16, .texture_h = 16});
  }
  return sprite;
}

TEST(SpritePanelModelTest, SortsSpritesAndAttachesSelection) {
  SpritePanelModel model;
  model.SetSprites({MakeSprite("z", "Zebes"), MakeSprite("a", "Aether")});

  ASSERT_EQ(model.sprites().size(), 2);
  EXPECT_EQ(model.sprites()[0].id, "a");
  EXPECT_EQ(model.sprites()[1].id, "z");

  model.SelectSpriteIndex(1);
  ASSERT_TRUE(model.AttachSelectedSprite().ok());
  ASSERT_NE(model.editing_sprite(), nullptr);
  EXPECT_EQ(model.editing_sprite()->id, "z");
}

TEST(SpritePanelModelTest, InvalidSelectionCannotAttach) {
  SpritePanelModel model;
  model.SetSprites({MakeSprite("a", "Aether")});

  model.SelectSpriteIndex(5);

  EXPECT_FALSE(model.AttachSelectedSprite().ok());
  EXPECT_FALSE(model.has_editing_sprite());
}

TEST(SpritePanelModelTest, DetachResetsEditingAndSelectionState) {
  SpritePanelModel model;
  model.SetSprites({MakeSprite("a", "Aether", 2)});
  model.SelectSpriteIndex(0);
  ASSERT_TRUE(model.AttachSelectedSprite().ok());
  ASSERT_TRUE(model.SetFrameIndex(1).ok());

  model.DetachSprite();

  EXPECT_FALSE(model.has_editing_sprite());
  EXPECT_EQ(model.selected_sprite_index(), -1);
  EXPECT_EQ(model.frame_index(), 0);
  EXPECT_EQ(model.current_frame(), nullptr);
}

TEST(SpritePanelModelTest, FrameSelectionRequiresValidAttachedFrame) {
  SpritePanelModel model;
  EXPECT_EQ(model.SetFrameIndex(0).code(), absl::StatusCode::kFailedPrecondition);

  model.AttachSprite(MakeSprite("a", "Aether", 2));
  EXPECT_EQ(model.SetFrameIndex(2).code(), absl::StatusCode::kOutOfRange);
  EXPECT_TRUE(model.SetFrameIndex(1).ok());
  ASSERT_NE(model.current_frame(), nullptr);
  EXPECT_EQ(model.current_frame()->index, 1);
}

TEST(SpritePanelModelTest, CalculatesUvsAndScalesPreviewToAvailableWidth) {
  SpriteFrame frame{
      .texture_x = 32,
      .texture_y = 16,
      .texture_w = 64,
      .texture_h = 32,
  };

  absl::StatusOr<SpriteFramePreview> preview =
      SpritePanelModel::CalculateFramePreview(frame, 256, 128, 32.0f);

  ASSERT_TRUE(preview.ok());
  EXPECT_FLOAT_EQ(preview->uv0_x, 0.125f);
  EXPECT_FLOAT_EQ(preview->uv0_y, 0.125f);
  EXPECT_FLOAT_EQ(preview->uv1_x, 0.375f);
  EXPECT_FLOAT_EQ(preview->uv1_y, 0.375f);
  EXPECT_FLOAT_EQ(preview->width, 32.0f);
  EXPECT_FLOAT_EQ(preview->height, 16.0f);
}

TEST(SpritePanelModelTest, RejectsInvalidPreviewDimensions) {
  SpriteFrame frame{.texture_w = 16, .texture_h = 16};

  EXPECT_FALSE(SpritePanelModel::CalculateFramePreview(frame, 0, 64, 100.0f).ok());
  frame.texture_w = 0;
  EXPECT_FALSE(SpritePanelModel::CalculateFramePreview(frame, 64, 64, 100.0f).ok());
}

}  // namespace
}  // namespace zebes
