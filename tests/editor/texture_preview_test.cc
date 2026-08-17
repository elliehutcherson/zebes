#include "editor/texture_preview.h"

#include <limits>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(TexturePreviewLayoutTest, FitsWideAndTallTexturesWithinBounds) {
  absl::StatusOr<TexturePreviewLayout> wide =
      CalculateTexturePreviewLayout(400, 200, 240.0f, 140.0f);
  ASSERT_OK(wide);
  EXPECT_FLOAT_EQ(wide->display_width, 240.0f);
  EXPECT_FLOAT_EQ(wide->display_height, 120.0f);

  absl::StatusOr<TexturePreviewLayout> tall =
      CalculateTexturePreviewLayout(100, 400, 240.0f, 140.0f);
  ASSERT_OK(tall);
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

TEST(TexturePreviewCameraTest, CentersAndFitsTheImageInsideTheCanvasViewport) {
  Camera camera{.viewport_width = 800, .viewport_height = 600};

  ASSERT_OK(FrameImagePreviewCamera(camera, 400, 400, 0.9f,
                                    CameraZoomRange{.minimum = 0.1, .maximum = 10.0}));

  EXPECT_EQ(camera.position, (Vec{200, 200}));
  EXPECT_NEAR(camera.zoom, 1.35, 1e-6);
}

TEST(TexturePreviewCameraTest, RejectsInvalidViewportAndFillFraction) {
  Camera missing_viewport;
  EXPECT_EQ(FrameImagePreviewCamera(missing_viewport, 100, 100, 0.9f,
                                    CameraZoomRange{.minimum = 0.1, .maximum = 10.0})
                .code(),
            absl::StatusCode::kInvalidArgument);

  Camera camera{.viewport_width = 800, .viewport_height = 600};
  EXPECT_EQ(FrameImagePreviewCamera(camera, 100, 100, 0.0f,
                                    CameraZoomRange{.minimum = 0.1, .maximum = 10.0})
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(TexturePreviewCameraTest, RespectsTheOwningCanvasZoomRange) {
  Camera camera{.viewport_width = 800, .viewport_height = 600};

  ASSERT_OK(FrameImagePreviewCamera(camera, 4, 4, 0.9f,
                                    CameraZoomRange{.minimum = 0.1, .maximum = 10.0}));

  EXPECT_DOUBLE_EQ(camera.zoom, 10.0);
}

}  // namespace
}  // namespace zebes
