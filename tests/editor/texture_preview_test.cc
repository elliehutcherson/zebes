#include "editor/texture_preview.h"

#include <limits>

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(TexturePreviewLayoutTest, FitsWideAndTallTexturesWithinBounds) {
  absl::StatusOr<TexturePreviewLayout> wide =
      CalculateTexturePreviewLayout(400, 200, 240.0f, 140.0f);
  ASSERT_TRUE(wide.ok()) << wide.status();
  EXPECT_FLOAT_EQ(wide->display_width, 240.0f);
  EXPECT_FLOAT_EQ(wide->display_height, 120.0f);

  absl::StatusOr<TexturePreviewLayout> tall =
      CalculateTexturePreviewLayout(100, 400, 240.0f, 140.0f);
  ASSERT_TRUE(tall.ok()) << tall.status();
  EXPECT_FLOAT_EQ(tall->display_width, 35.0f);
  EXPECT_FLOAT_EQ(tall->display_height, 140.0f);
}

TEST(TexturePreviewLayoutTest, RejectsInvalidDimensionsAndBounds) {
  EXPECT_EQ(CalculateTexturePreviewLayout(0, 100, 240.0f, 140.0f).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(CalculateTexturePreviewLayout(100, 100, std::numeric_limits<float>::infinity(), 140.0f)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
