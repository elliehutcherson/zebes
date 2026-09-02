#include "resources/animation_frame_set_recipe_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "artwork/animation_frame_set_recipe.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

AnimationFrameSetRecipe CompleteRecipe(std::string name = "Run Left") {
  return {
      .name = std::move(name),
      .source_artwork_id = "source-id",
      .style =
          AnimationFrameSetStyle{
              .palette = {{32, 64, 128, 255}},
          },
      .pipeline =
          AnimationFrameSetPipelineConfig{
              .sheet =
                  AnimationFrameSetSheetLayout{
                      .grid_x = 0,
                      .grid_y = 0,
                      .cell_width = 2,
                      .cell_height = 2,
                      .column_gap = 0,
                      .row_gap = 0,
                      .columns = 1,
                      .rows = 1,
                  },
              .output_width = 2,
              .output_height = 2,
              .origin_x = 1,
              .origin_y = 2,
              .contact_line_y = 2,
              .render_scale = 1,
              .contact_tolerance = 1,
              .minimum_visible_pixels = 1,
              .maximum_horizontal_anchor_drift = 1,
              .maximum_vertical_anchor_drift = 1,
              .packing_columns = 1,
              .playback_mode = SpritePlaybackMode::kLoop,
              .frames_per_cycle = {5},
              .planted_frames = {false},
          },
      .texture_id = "texture-id",
      .sprite_id = "sprite-id",
      .blueprint_id = "blueprint-id",
      .blueprint_bindings = {{
          .state_key = "run-left",
          .previous_sprite_id = "placeholder-id",
      }},
      .expected_frames = {{
          .index = 0,
          .texture_x = 0,
          .texture_y = 0,
          .texture_w = 2,
          .texture_h = 2,
          .render_w = 2,
          .render_h = 2,
          .frames_per_cycle = 5,
          .offset_x = -1,
          .offset_y = -2,
      }},
      .final_pixel_digest = std::string(64, 'a'),
  };
}

class AnimationFrameSetRecipeManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-animation-frame-set-recipes-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, AnimationFrameSetRecipeManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllRecipes());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<AnimationFrameSetRecipeManager> manager_;
  static int sequence;
};

int AnimationFrameSetRecipeManagerTest::sequence = 0;

TEST_F(AnimationFrameSetRecipeManagerTest, RoundTripsCompleteStrictRecipe) {
  AnimationFrameSetRecipe recipe = CompleteRecipe();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnimationFrameSetRecipeManager> reloaded,
                       AnimationFrameSetRecipeManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllRecipes());
  ASSERT_OK_AND_ASSIGN(AnimationFrameSetRecipe * loaded, reloaded->GetRecipe(id));
  recipe.id = id;
  EXPECT_EQ(AnimationFrameSetRecipeToJson(*loaded), AnimationFrameSetRecipeToJson(recipe));
}

TEST_F(AnimationFrameSetRecipeManagerTest, CreateWithIdPreflightsMemoryAndDiskCollisions) {
  AnimationFrameSetRecipe recipe = CompleteRecipe();
  recipe.id = "prepared-recipe";

  ASSERT_OK(manager_->CreateRecipeWithId(recipe));
  EXPECT_EQ(manager_->CreateRecipeWithId(recipe).code(), absl::StatusCode::kAlreadyExists);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnimationFrameSetRecipeManager> second,
                       AnimationFrameSetRecipeManager::Create(path_.string()));
  EXPECT_EQ(second->PreflightRecipeWithId(recipe).code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(AnimationFrameSetRecipeManagerTest, LoadRejectsUnknownFieldsWithoutReplacingCurrentCatalog) {
  AnimationFrameSetRecipe current = CompleteRecipe("Current");
  current.id = "current";
  ASSERT_OK(manager_->CreateRecipeWithId(current));
  AnimationFrameSetRecipe broken = CompleteRecipe("Broken");
  broken.id = "broken";
  nlohmann::json json = AnimationFrameSetRecipeToJson(broken);
  json.at("pipeline")["unknown"] = 1;
  const std::filesystem::path directory = path_ / "definitions/animation_frame_set_recipes";
  std::ofstream(directory / "broken.json") << json.dump(2);

  const absl::Status status = manager_->LoadAllRecipes();

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  ASSERT_OK_AND_ASSIGN(AnimationFrameSetRecipe * still_loaded, manager_->GetRecipe("current"));
  EXPECT_EQ(still_loaded->name, "Current");
}

}  // namespace
}  // namespace zebes
