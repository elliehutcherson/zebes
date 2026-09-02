#include "artwork/profile_silhouette.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

void PaintPixel(RgbaImage* image, int x, int y) {
  if (x < 0 || y < 0 || x >= image->width || y >= image->height) return;
  const size_t pixel = (static_cast<size_t>(y) * image->width + x) * 4;
  image->pixels[pixel] = 90;
  image->pixels[pixel + 1] = 140;
  image->pixels[pixel + 2] = 80;
  image->pixels[pixel + 3] = 255;
}

void PaintRectangle(RgbaImage* image, int left, int top, int right, int bottom) {
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) PaintPixel(image, x, y);
  }
}

void PaintEllipse(RgbaImage* image, int center_x, int center_y, int radius_x, int radius_y) {
  for (int y = center_y - radius_y; y <= center_y + radius_y; ++y) {
    for (int x = center_x - radius_x; x <= center_x + radius_x; ++x) {
      const double normalized_x = static_cast<double>(x - center_x) / radius_x;
      const double normalized_y = static_cast<double>(y - center_y) / radius_y;
      if (normalized_x * normalized_x + normalized_y * normalized_y <= 1.0) {
        PaintPixel(image, x, y);
      }
    }
  }
}

RgbaImage ProfileMouse() {
  RgbaImage image{
      .width = 128,
      .height = 128,
      .pixels = std::vector<uint8_t>(128 * 128 * 4, 0),
  };
  PaintEllipse(&image, 64, 27, 18, 19);
  PaintEllipse(&image, 45, 15, 10, 11);
  PaintEllipse(&image, 83, 15, 10, 11);
  PaintRectangle(&image, 59, 43, 69, 52);
  PaintRectangle(&image, 45, 50, 83, 91);
  PaintRectangle(&image, 35, 57, 48, 82);
  PaintRectangle(&image, 80, 57, 101, 79);
  PaintRectangle(&image, 49, 88, 59, 117);
  PaintRectangle(&image, 69, 88, 79, 117);
  PaintRectangle(&image, 42, 113, 59, 120);
  PaintRectangle(&image, 69, 113, 88, 120);
  // A short silhouette spur exercises terminal-branch pruning.
  PaintRectangle(&image, 83, 67, 88, 70);
  return image;
}

TEST(ProfileSilhouetteTest, ExtractsDeterministicConnectedMedialAxis) {
  const RgbaImage image = ProfileMouse();
  const ProfileSilhouetteConfig config{
      .working_size = 128,
      .minimum_branch_length = 5,
  };

  const absl::StatusOr<ProfileSilhouette> first = ExtractProfileSilhouette(image, config);
  const absl::StatusOr<ProfileSilhouette> second = ExtractProfileSilhouette(image, config);

  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_TRUE(first->IsValid());
  EXPECT_EQ(first->source_scale, 1);
  EXPECT_GT(first->silhouette_pixels, 4000);
  EXPECT_GT(first->medial_axis_pixels, 100);
  EXPECT_EQ(first->component_count, 1);
  EXPECT_GE(first->endpoint_count, 4);
  EXPECT_EQ(first->silhouette, second->silhouette);
  EXPECT_EQ(first->medial_axis, second->medial_axis);
}

TEST(ProfileSilhouetteTest, PruningNeverAddsBranchesOrPixels) {
  const RgbaImage image = ProfileMouse();
  const absl::StatusOr<ProfileSilhouette> raw = ExtractProfileSilhouette(
      image, ProfileSilhouetteConfig{.working_size = 128, .minimum_branch_length = 0});
  const absl::StatusOr<ProfileSilhouette> pruned = ExtractProfileSilhouette(
      image, ProfileSilhouetteConfig{.working_size = 128, .minimum_branch_length = 8});

  ASSERT_TRUE(raw.ok()) << raw.status();
  ASSERT_TRUE(pruned.ok()) << pruned.status();
  EXPECT_LE(pruned->medial_axis_pixels, raw->medial_axis_pixels);
  EXPECT_LE(pruned->endpoint_count, raw->endpoint_count);
  EXPECT_EQ(pruned->silhouette, raw->silhouette);
}

TEST(ProfileSilhouetteTest, RendersEveryAxisPixelInRed) {
  const absl::StatusOr<ProfileSilhouette> profile =
      ExtractProfileSilhouette(ProfileMouse(), ProfileSilhouetteConfig{.working_size = 128});
  ASSERT_TRUE(profile.ok()) << profile.status();

  const absl::StatusOr<RgbaImage> evidence = RenderProfileSilhouetteEvidence(*profile);

  ASSERT_TRUE(evidence.ok()) << evidence.status();
  int red_pixels = 0;
  for (size_t pixel = 0; pixel < evidence->pixels.size() / 4; ++pixel) {
    if (evidence->pixels[pixel * 4] == 255 && evidence->pixels[pixel * 4 + 1] == 70 &&
        evidence->pixels[pixel * 4 + 2] == 70) {
      ++red_pixels;
    }
  }
  EXPECT_EQ(red_pixels, profile->medial_axis_pixels);
}

TEST(ProfileSilhouetteTest, RendersBinaryNeutralControl) {
  const absl::StatusOr<ProfileSilhouette> profile =
      ExtractProfileSilhouette(ProfileMouse(), ProfileSilhouetteConfig{.working_size = 128});
  ASSERT_TRUE(profile.ok()) << profile.status();

  const absl::StatusOr<RgbaImage> control = RenderProfileSilhouetteControl(*profile);

  ASSERT_TRUE(control.ok()) << control.status();
  int white_pixels = 0;
  for (size_t pixel = 0; pixel < control->pixels.size() / 4; ++pixel) {
    const uint8_t red = control->pixels[pixel * 4];
    EXPECT_TRUE(red == 0 || red == 255);
    EXPECT_EQ(control->pixels[pixel * 4 + 1], red);
    EXPECT_EQ(control->pixels[pixel * 4 + 2], red);
    EXPECT_EQ(control->pixels[pixel * 4 + 3], 255);
    white_pixels += red == 255 ? 1 : 0;
  }
  EXPECT_GT(white_pixels, profile->medial_axis_pixels);
  EXPECT_LT(white_pixels, profile->silhouette_pixels);
}

TEST(ProfileSilhouetteTest, RendersBinarySemanticPoseControl) {
  const absl::StatusOr<ProfileSilhouette> profile =
      ExtractProfileSilhouette(ProfileMouse(), ProfileSilhouetteConfig{.working_size = 128});
  ASSERT_TRUE(profile.ok()) << profile.status();
  const std::vector<ProfileControlPoint> joints = {
      {.x = 64.0, .y = 45.0},
      {.x = 64.0, .y = 70.0},
      {.x = 45.0, .y = 100.0},
  };
  const std::vector<ProfileControlBone> bones = {
      {.start_joint = 0, .end_joint = 1},
      {.start_joint = 1, .end_joint = 2},
  };

  const absl::StatusOr<RgbaImage> control =
      RenderProfilePoseControl(profile->silhouette, profile->width, profile->height, joints, bones);

  ASSERT_TRUE(control.ok()) << control.status();
  const size_t shoulder = (static_cast<size_t>(70) * control->width + 64) * 4;
  EXPECT_EQ(control->pixels[shoulder], 255);
  for (size_t pixel = 0; pixel < control->pixels.size() / 4; ++pixel) {
    const uint8_t red = control->pixels[pixel * 4];
    EXPECT_TRUE(red == 0 || red == 255);
    EXPECT_EQ(control->pixels[pixel * 4 + 1], red);
    EXPECT_EQ(control->pixels[pixel * 4 + 2], red);
    EXPECT_EQ(control->pixels[pixel * 4 + 3], 255);
  }
}

TEST(ProfileSilhouetteTest, RejectsPoseControlWithUnknownJoint) {
  const std::vector<uint8_t> silhouette(16 * 16, 1);
  const std::vector<ProfileControlPoint> joints = {{.x = 4.0, .y = 4.0}};
  const std::vector<ProfileControlBone> bones = {
      {.start_joint = 0, .end_joint = 1},
  };

  const absl::Status status = RenderProfilePoseControl(silhouette, 16, 16, joints, bones).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("unknown joint"), std::string::npos);
}

TEST(ProfileSilhouetteTest, RejectsGeometryThatWouldChangeRegistration) {
  RgbaImage non_square{
      .width = 128,
      .height = 64,
      .pixels = std::vector<uint8_t>(128 * 64 * 4, 255),
  };

  const absl::Status status =
      ExtractProfileSilhouette(non_square, ProfileSilhouetteConfig{.working_size = 64}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("square"), std::string::npos);
}

}  // namespace
}  // namespace zebes
