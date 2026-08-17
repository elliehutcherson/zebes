#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "common/image_digest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class PropArtworkControlsPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(panel_, PropArtworkControlsPanel::Create(&gui_));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol index, const ImVec4& color) {
          return ScopedStyleColor(&gui_, index, color);
        }));
  }

  void ClickOnly(const std::string& label) {
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([label](const char* candidate, const ImVec2&) {
      return label == candidate;
    }));
  }

  static TerrainRecipe Terrain(int tile_size) {
    TerrainRecipe recipe{.id = "terrain-1", .name = "Cave"};
    recipe.config.tile_size = tile_size;
    return recipe;
  }

  NiceMock<MockGui> gui_;
  std::unique_ptr<PropArtworkControlsPanel> panel_;
  PropArtworkEditorModel model_;
};

TEST_F(PropArtworkControlsPanelTest, RefreshUsesTheCurrentTerrainRecipeSnapshot) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  ClickOnly("Refresh style##PropArtwork");

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(16)}));

  EXPECT_EQ(model_.settings().style.tile_size, 16);
}

TEST_F(PropArtworkControlsPanelTest, DetachKeepsTheResolvedStyleSnapshot) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  const PropArtworkStyle style = model_.settings().style;
  ClickOnly("Detach style##PropArtwork");

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}));

  EXPECT_FALSE(model_.terrain_recipe().has_value());
  EXPECT_FALSE(model_.settings().terrain_recipe_id.has_value());
  EXPECT_TRUE(model_.has_style());
  EXPECT_EQ(model_.settings().style.palette.colors, style.palette.colors);
}

TEST_F(PropArtworkControlsPanelTest, DeletingARetainedSourceRequiresConfirmation) {
  RgbaImage pixels{.width = 1, .height = 1, .pixels = {20, 30, 40, 255}};
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(pixels));
  SourceArtwork source{
      .id = "source-1",
      .name = "Boulder source",
      .source_path = "source_art/props/source-1.png",
      .provenance = ImportedArtworkProvenance{.original_filename = "boulder.png",
                                              .imported_at_utc = "2026-08-17T12:00:00Z"},
      .width = 1,
      .height = 1,
      .content_digest = digest,
  };
  ASSERT_OK(model_.SelectSource(source, pixels));

  ClickOnly("Delete source##PropArtworkSource");
  ASSERT_OK_AND_ASSIGN(PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {source}, {}));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kNone);

  ClickOnly("Confirm##PropArtworkSource");
  ASSERT_OK_AND_ASSIGN(action, panel_->Render(model_, {source}, {}));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kDeleteSource);
}

TEST_F(PropArtworkControlsPanelTest, FreeAttachmentStartsAtTheCanvasCenter) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(true));
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Attachment##PropArtwork";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "Free / background";
      }));

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}));

  const PropAttachmentConfig& attachment = model_.settings().pipeline.composition.attachment;
  EXPECT_EQ(attachment.mode, PropAttachmentMode::kFree);
  ASSERT_TRUE(attachment.free_anchor.has_value());
  EXPECT_EQ(attachment.free_anchor->x, 12);
  EXPECT_EQ(attachment.free_anchor->y, 8);
}

TEST_F(PropArtworkControlsPanelTest, ReselectingFreeAttachmentKeepsTheAuthoredAnchor) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  model_.settings().pipeline.composition.attachment = PropAttachmentConfig{
      .mode = PropAttachmentMode::kFree,
      .free_anchor = PropFreeAnchor{.x = 2, .y = 3},
  };
  ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(true));
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Attachment##PropArtwork";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "Free / background";
      }));

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}));

  ASSERT_TRUE(model_.settings().pipeline.composition.attachment.free_anchor.has_value());
  EXPECT_EQ(model_.settings().pipeline.composition.attachment.free_anchor->x, 2);
  EXPECT_EQ(model_.settings().pipeline.composition.attachment.free_anchor->y, 3);
}

}  // namespace
}  // namespace zebes
