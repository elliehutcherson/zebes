#include "editor/level_editor/parallax_panel.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "objects/texture.h"
#include "tests/api_mock.h"

namespace zebes {

class ParallaxPanelTestPeer {
 public:
  static void SetSelectedTextureIndex(ParallaxPanel& panel, int index) {
    panel.selected_texture_index_ = index;
  }
  static int GetSelectedTextureIndex(const ParallaxPanel& panel) {
    return panel.selected_texture_index_;
  }
};

namespace {

using ::testing::NiceMock;
using ::testing::Return;

class ParallaxPanelValidationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    level_.parallax_layers.clear();

    textures_ = {// ID, Name, Path, SDL_Texture*
                 {"t_001", "grass_ground", "assets/tiles/grass.png", nullptr},
                 {"t_002", "stone_wall", "assets/tiles/stone.png", nullptr},
                 {"t_003", "player_idle", "assets/chars/hero.png", nullptr},
                 {"t_004", "enemy_slime", "assets/chars/slime.png", nullptr},
                 {"t_005", "ui_button", "assets/ui/btn_ok.png", nullptr}};

    ON_CALL(*api_, GetAllTextures()).WillByDefault(Return(textures_));
    auto panel = ParallaxPanel::Create({.api = api_.get()});
    ASSERT_TRUE(panel.ok());
    panel_ = *std::move(panel);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  std::unique_ptr<ParallaxPanel> panel_;
  Level level_;
  std::vector<Texture> textures_;
};

TEST_F(ParallaxPanelValidationTest, CreateAddsLayer) {
  // Create adds immediately
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate);
  EXPECT_TRUE(result.ok());

  EXPECT_EQ(level_.parallax_layers.size(), 1);
  EXPECT_EQ(level_.parallax_layers[0].name, "Layer 0");

  // Verify selection
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);
  // editing_layer_ state depends on implementation choices, currently set to enter edit mode.
  EXPECT_TRUE(panel_->GetEditingLayer().has_value());
}

TEST_F(ParallaxPanelValidationTest, SaveUpdatesLayer) {
  // Create a layer
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);

  // Now Edit
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxEdit).ok());
  EXPECT_TRUE(panel_->GetEditingLayer().has_value());
  EXPECT_EQ(panel_->GetEditingLayer()->name, "Layer 0");

  // Modify
  panel_->GetEditingLayer()->name = "Updated";
  // We can manually set the string if we want to bypass the UI flow for simple tests
  panel_->GetEditingLayer()->texture_id = "t_001";

  // Update
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxSave);
  EXPECT_TRUE(result.ok());

  EXPECT_EQ(level_.parallax_layers[0].name, "Updated");
  EXPECT_EQ(level_.parallax_layers[0].texture_id, "t_001");
}

TEST_F(ParallaxPanelValidationTest, SaveValidatesFields) {
  // Create and select
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());

  // Edit
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxEdit).ok());

  // 1. Invalid Name
  std::string original_name = panel_->GetEditingLayer()->name;
  panel_->GetEditingLayer()->name = "";
  // Ensure valid texture so we test name failure
  panel_->GetEditingLayer()->texture_id = "valid_tex";

  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxSave);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.message(), "Layer name cannot be empty");

  // Reset Name
  panel_->GetEditingLayer()->name = original_name;

  // 2. Invalid Texture
  panel_->GetEditingLayer()->texture_id = "";

  result = panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxSave);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.message(), "Layer texture must be selected");
}

TEST_F(ParallaxPanelValidationTest, DeleteRemovesLayer) {
  // Create
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());
  EXPECT_EQ(level_.parallax_layers.size(), 1);
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);

  // Delete
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxDelete);
  EXPECT_TRUE(result.ok());

  EXPECT_EQ(level_.parallax_layers.size(), 0);
  EXPECT_EQ(panel_->GetSelectedIndex(), -1);
}

TEST_F(ParallaxPanelValidationTest, TextureSelectionWorkflow) {
  // Create a layer
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());

  // Enter Edit Mode
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxEdit).ok());

  // Verify initial state: no texture selected
  EXPECT_EQ(panel_->GetEditingLayer()->texture_id, "");

  // Simulate selecting a texture from the list (using TestPeer)
  // Selecting "stone_wall" at index 3 (sorted: enemy, grass, player, stone, ui)
  ParallaxPanelTestPeer::SetSelectedTextureIndex(*panel_, 3);

  // Verify GetTexture() returns the preview
  EXPECT_EQ(panel_->GetTexture(), "t_002");

  // Apply the texture change
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxTexture).ok());

  // Verify the layer is updated
  EXPECT_EQ(panel_->GetEditingLayer()->texture_id, "t_002");

  // Save
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxSave).ok());

  // Verify level is updated
  EXPECT_EQ(level_.parallax_layers[0].texture_id, "t_002");
}

TEST_F(ParallaxPanelValidationTest, TextureSelectionResetOnCreate) {
  // Set a selection
  ParallaxPanelTestPeer::SetSelectedTextureIndex(*panel_, 1);

  // Create new layer
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());

  // Verify selection reset
  EXPECT_EQ(ParallaxPanelTestPeer::GetSelectedTextureIndex(*panel_), -1);
}

TEST_F(ParallaxPanelValidationTest, TextureSelectionResetOnEdit) {
  // Create a layer
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxCreate).ok());

  // Set a selection
  ParallaxPanelTestPeer::SetSelectedTextureIndex(*panel_, 1);

  // Enter Edit Mode (re-entering should reset/sync with current layer)
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kParallaxEdit).ok());

  // Verify selection reset (since layer has no texture yet)
  EXPECT_EQ(ParallaxPanelTestPeer::GetSelectedTextureIndex(*panel_), -1);
}

}  // namespace
}  // namespace zebes
