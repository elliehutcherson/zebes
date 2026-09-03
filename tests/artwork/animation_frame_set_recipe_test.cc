#include "artwork/animation_frame_set_recipe.h"

#include <array>
#include <utility>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "tests/macros.h"

namespace zebes {
namespace {

AnimationFrameSetRecipe ValidRecipe() {
  AnimationFrameSetPipelineConfig pipeline{
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
  };
  return {
      .id = "recipe-id",
      .name = "Run Left",
      .source_artwork_id = "source-id",
      .style =
          AnimationFrameSetStyle{
              .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
              .matte = {255, 0, 255, 255},
              .transparent_matte_distance = 12.0f,
              .opaque_matte_distance = 100.0f,
              .alpha_threshold = 128,
              .palette = {{32, 64, 128, 255}},
          },
      .pipeline = std::move(pipeline),
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
      .final_pixel_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .pipeline_version = kAnimationFrameSetPipelineVersion,
  };
}

TEST(AnimationFrameSetRecipeTest, RoundTripsEveryField) {
  const AnimationFrameSetRecipe recipe = ValidRecipe();
  const nlohmann::json json = AnimationFrameSetRecipeToJson(recipe);

  ASSERT_OK_AND_ASSIGN(const AnimationFrameSetRecipe parsed, AnimationFrameSetRecipeFromJson(json));

  EXPECT_EQ(AnimationFrameSetRecipeToJson(parsed), json);
}

TEST(AnimationFrameSetRecipeTest, WriterEmitsEmptyCollections) {
  AnimationFrameSetRecipe recipe = ValidRecipe();
  recipe.style.palette.clear();
  recipe.pipeline.frames_per_cycle.clear();
  recipe.pipeline.planted_frames.clear();
  recipe.blueprint_bindings.clear();
  recipe.expected_frames.clear();

  const nlohmann::json json = AnimationFrameSetRecipeToJson(recipe);

  EXPECT_EQ(json.at("style").at("palette"), nlohmann::json::array());
  EXPECT_EQ(json.at("pipeline").at("frames_per_cycle"), nlohmann::json::array());
  EXPECT_EQ(json.at("pipeline").at("planted_frames"), nlohmann::json::array());
  EXPECT_EQ(json.at("blueprint_bindings"), nlohmann::json::array());
  EXPECT_EQ(json.at("expected_frames"), nlohmann::json::array());
}

TEST(AnimationFrameSetRecipeTest, RejectsEveryMissingTopLevelField) {
  const nlohmann::json complete = AnimationFrameSetRecipeToJson(ValidRecipe());
  constexpr std::array<const char*, 13> kFields = {
      "schema_version",
      "id",
      "name",
      "source_artwork_id",
      "style",
      "pipeline",
      "texture_id",
      "sprite_id",
      "blueprint_id",
      "blueprint_bindings",
      "expected_frames",
      "final_pixel_digest",
      "pipeline_version",
  };

  for (const char* field : kFields) {
    nlohmann::json missing = complete;
    missing.erase(field);
    const absl::Status status = AnimationFrameSetRecipeFromJson(missing).status();
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument) << field;
  }
}

TEST(AnimationFrameSetRecipeTest, RejectsMissingNestedFields) {
  const nlohmann::json complete = AnimationFrameSetRecipeToJson(ValidRecipe());
  constexpr std::array<const char*, 6> kStyleFields = {
      "extraction",      "matte",   "transparent_matte_distance", "opaque_matte_distance",
      "alpha_threshold", "palette",
  };
  for (const char* field : kStyleFields) {
    nlohmann::json missing = complete;
    missing.at("style").erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }

  constexpr std::array<const char*, 15> kPipelineFields = {
      "source_limits",
      "sheet",
      "output_width",
      "output_height",
      "origin_x",
      "origin_y",
      "contact_line_y",
      "render_scale",
      "contact_tolerance",
      "minimum_visible_pixels",
      "maximum_horizontal_anchor_drift",
      "maximum_vertical_anchor_drift",
      "packing_columns",
      "playback_mode",
      "frames_per_cycle",
  };
  for (const char* field : kPipelineFields) {
    nlohmann::json missing = complete;
    missing.at("pipeline").erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }
  nlohmann::json missing_planted = complete;
  missing_planted.at("pipeline").erase("planted_frames");
  EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing_planted).status().code(),
            absl::StatusCode::kInvalidArgument);

  for (const char* field : {"maximum_width", "maximum_height", "maximum_pixels", "maximum_bytes",
                            "maximum_encoded_bytes"}) {
    nlohmann::json missing = complete;
    missing.at("pipeline").at("source_limits").erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }
  for (const char* field : {"grid_x", "grid_y", "cell_width", "cell_height", "column_gap",
                            "row_gap", "columns", "rows"}) {
    nlohmann::json missing = complete;
    missing.at("pipeline").at("sheet").erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }
  for (const char* field : {"state_key", "previous_sprite_id"}) {
    nlohmann::json missing = complete;
    missing.at("blueprint_bindings").at(0).erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }
  for (const char* field : {"index", "texture_x", "texture_y", "texture_w", "texture_h", "render_w",
                            "render_h", "frames_per_cycle", "offset_x", "offset_y"}) {
    nlohmann::json missing = complete;
    missing.at("expected_frames").at(0).erase(field);
    EXPECT_EQ(AnimationFrameSetRecipeFromJson(missing).status().code(),
              absl::StatusCode::kInvalidArgument)
        << field;
  }
}

TEST(AnimationFrameSetRecipeTest, RejectsUnsupportedSchemaVersion) {
  nlohmann::json json = AnimationFrameSetRecipeToJson(ValidRecipe());
  json.at("schema_version") = kAnimationFrameSetRecipeSchemaVersion + 1;

  EXPECT_EQ(AnimationFrameSetRecipeFromJson(json).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(AnimationFrameSetRecipeTest, KeepsTextureGeometryNativeWhenRenderScaleExceedsOne) {
  AnimationFrameSetRecipe recipe = ValidRecipe();
  recipe.pipeline.render_scale = 2;
  recipe.expected_frames[0].render_w = 4;
  recipe.expected_frames[0].render_h = 4;
  recipe.expected_frames[0].offset_x = -2;
  recipe.expected_frames[0].offset_y = -4;

  EXPECT_TRUE(ValidateAnimationFrameSetRecipe(recipe).ok());
}

TEST(AnimationFrameSetRecipeTest, RejectsCollidingAndSelfRestoringOwnedIds) {
  AnimationFrameSetRecipe recipe = ValidRecipe();
  recipe.sprite_id = recipe.texture_id;
  EXPECT_EQ(ValidateAnimationFrameSetRecipe(recipe).code(), absl::StatusCode::kInvalidArgument);

  recipe = ValidRecipe();
  recipe.blueprint_bindings[0].previous_sprite_id = recipe.sprite_id;
  EXPECT_EQ(ValidateAnimationFrameSetRecipe(recipe).code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
