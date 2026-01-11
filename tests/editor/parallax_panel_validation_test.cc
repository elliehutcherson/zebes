#include "editor/level_editor/parallax_panel.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "objects/texture.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

class ParallaxPanelValidationTest : public ::testing::Test {
 protected:
  ParallaxPanelValidationTest() : api_(std::make_unique<NiceMock<MockApi>>()) {}

  void SetUp() override {
    level_.parallax_layers.clear();

    textures_ = {// ID, Name, Path, SDL_Texture*
                 {"t_001", "grass_ground", "assets/tiles/grass.png", nullptr},
                 {"t_002", "stone_wall", "assets/tiles/stone.png", nullptr},
                 {"t_003", "player_idle", "assets/chars/hero.png", nullptr},
                 {"t_004", "enemy_slime", "assets/chars/slime.png", nullptr},
                 {"t_005", "ui_button", "assets/ui/btn_ok.png", nullptr}};

    ON_CALL(*api_, GetAllTextures()).WillByDefault(Return(textures_));
    auto panel_or = ParallaxPanel::Create({.api = api_.get()});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);
  }

  std::unique_ptr<MockApi> api_;
  std::unique_ptr<ParallaxPanel> panel_;
  Level level_;
  std::vector<Texture> textures_;
};

TEST_F(ParallaxPanelValidationTest, CreateAddsLayer) {
  // Create adds immediately
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kLayerCreate);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->type, ParallaxResult::kChanged);

  EXPECT_EQ(level_.parallax_layers.size(), 1);
  EXPECT_EQ(level_.parallax_layers[0].name, "Layer 0");

  // Verify selection
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);
  // editing_layer_ state depends on implementation choices, currently set to enter edit mode.
  EXPECT_TRUE(panel_->GetEditingLayer().has_value());
}

TEST_F(ParallaxPanelValidationTest, UpdateUpdatesLayer) {
  // Create a layer
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kLayerCreate).ok());
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);

  // Now Edit
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kLayerEdit).ok());
  EXPECT_TRUE(panel_->GetEditingLayer().has_value());
  EXPECT_EQ(panel_->GetEditingLayer()->name, "Layer 0");

  // Modify
  panel_->GetEditingLayer()->name = "Updated";
  panel_->GetEditingLayer()->texture_id = "tex1";

  // Update
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kLayerUpdate);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->type, ParallaxResult::kChanged);

  EXPECT_EQ(level_.parallax_layers[0].name, "Updated");
  EXPECT_EQ(level_.parallax_layers[0].texture_id, "tex1");
  EXPECT_FALSE(panel_->GetEditingLayer().has_value());
}

TEST_F(ParallaxPanelValidationTest, UpdateValidatesFields) {
  // Create and select
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kLayerCreate).ok());

  // Edit
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kLayerEdit).ok());

  // 1. Invalid Name
  std::string original_name = panel_->GetEditingLayer()->name;
  panel_->GetEditingLayer()->name = "";
  // Ensure valid texture so we test name failure
  panel_->GetEditingLayer()->texture_id = "valid_tex";

  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kLayerUpdate);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "Layer name cannot be empty");

  // Reset Name
  panel_->GetEditingLayer()->name = original_name;

  // 2. Invalid Texture
  panel_->GetEditingLayer()->texture_id = "";

  result = panel_->HandleOp(level_, ParallaxPanel::Op::kLayerUpdate);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "Layer texture must be selected");
}

TEST_F(ParallaxPanelValidationTest, DeleteRemovesLayer) {
  // Create
  ASSERT_TRUE(panel_->HandleOp(level_, ParallaxPanel::Op::kLayerCreate).ok());
  EXPECT_EQ(level_.parallax_layers.size(), 1);
  EXPECT_EQ(panel_->GetSelectedIndex(), 0);

  // Delete
  auto result = panel_->HandleOp(level_, ParallaxPanel::Op::kLayerDelete);
  EXPECT_TRUE(result.ok());

  EXPECT_EQ(level_.parallax_layers.size(), 0);
  EXPECT_EQ(panel_->GetSelectedIndex(), -1);
}

}  // namespace
}  // namespace zebes
