#include "editor/texture_editor/texture_editor_model.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common/common.h"

namespace zebes {
namespace {

TEST(TextureEditorModelTest, SortsTextureListByName) {
  TextureEditorModel model;
  model.SetTextures({
      {.id = "z", .name = "Zebes", .path = "z.png"},
      {.id = "a", .name = "Aether", .path = "a.png"},
  });

  ASSERT_EQ(model.textures().size(), 2);
  EXPECT_EQ(model.textures()[0].id, "a");
  EXPECT_EQ(model.textures()[1].id, "z");
}

TEST(TextureEditorModelTest, BeginNewTextureResetsSelectionAndBuildsCreateRequest) {
  TextureEditorModel model;
  model.SelectTexture({.id = "old", .name = "Old", .path = "old.png"});

  model.BeginNewTexture();
  model.SetSelectedPath("new.png");
  model.edit_name_buffer().replace(0, 3, "New");

  ASSERT_TRUE(model.is_new_texture());
  absl::StatusOr<Texture> texture = model.BuildTextureForCreate();
  ASSERT_TRUE(texture.ok());
  EXPECT_TRUE(texture->id.empty());
  EXPECT_EQ(texture->name, "New");
  EXPECT_EQ(texture->path, "new.png");
}

TEST(TextureEditorModelTest, CreateRequiresNewModeAndPath) {
  TextureEditorModel model;
  EXPECT_EQ(model.BuildTextureForCreate().status().code(), absl::StatusCode::kFailedPrecondition);

  model.BeginNewTexture();
  EXPECT_EQ(model.BuildTextureForCreate().status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(TextureEditorModelTest, SelectingTexturePreparesFixedCapacityNameBuffer) {
  TextureEditorModel model;
  model.SelectTexture({.id = "texture", .name = "Tiles", .path = "tiles.png"});

  ASSERT_FALSE(model.is_new_texture());
  ASSERT_NE(model.selected_texture(), nullptr);
  EXPECT_EQ(std::string(model.edit_name_buffer().c_str()), "Tiles");
  EXPECT_EQ(model.edit_name_buffer().size(), kMaxTextureNameLength + 1);
}

TEST(TextureEditorModelTest, BuildUpdateAppliesEditedNameWithoutMutatingUntilFinished) {
  TextureEditorModel model;
  model.SelectTexture({.id = "texture", .name = "Old", .path = "tiles.png"});
  model.edit_name_buffer().replace(0, 3, "New");

  absl::StatusOr<Texture> update = model.BuildTextureForUpdate();
  ASSERT_TRUE(update.ok());
  EXPECT_EQ(update->name, "New");
  EXPECT_EQ(model.selected_texture()->name, "Old");

  model.FinishUpdate();
  EXPECT_EQ(model.selected_texture()->name, "New");
}

TEST(TextureEditorModelTest, FinishCreateSelectsCanonicalTexture) {
  TextureEditorModel model;
  model.BeginNewTexture();

  model.FinishCreate({.id = "created", .name = "Canonical", .path = "textures/file.png"});

  EXPECT_FALSE(model.is_new_texture());
  ASSERT_NE(model.selected_texture(), nullptr);
  EXPECT_EQ(model.selected_texture()->id, "created");
  EXPECT_EQ(std::string(model.edit_name_buffer().c_str()), "Canonical");
}

TEST(TextureEditorModelTest, ZoomIsClampedAndAffectsAspectCorrectPreviewSize) {
  TextureEditorModel model;
  for (int i = 0; i < 100; ++i) model.ZoomIn();
  EXPECT_FLOAT_EQ(model.zoom(), 10.0f);

  TexturePreviewSize size = model.CalculatePreviewSize(400, 200);
  EXPECT_FLOAT_EQ(size.width, 2000.0f);
  EXPECT_FLOAT_EQ(size.height, 1000.0f);

  model.AdjustZoom(-100.0f);
  EXPECT_FLOAT_EQ(model.zoom(), 0.1f);
  size = model.CalculatePreviewSize(0, 0);
  EXPECT_FLOAT_EQ(size.width, 20.0f);
  EXPECT_FLOAT_EQ(size.height, 20.0f);
}

}  // namespace
}  // namespace zebes
