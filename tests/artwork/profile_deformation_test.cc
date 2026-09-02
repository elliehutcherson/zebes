#include "artwork/profile_deformation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

RgbaImage TwoLayerSource(int width, int height, std::vector<uint8_t>* layers) {
  RgbaImage source{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0),
  };
  layers->assign(static_cast<size_t>(width) * height, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t index = static_cast<size_t>(y) * width + x;
      const uint8_t layer = x < width / 2 ? 1 : 2;
      (*layers)[index] = layer;
      const size_t pixel = index * 4;
      source.pixels[pixel] = layer == 1 ? 220 : 30;
      source.pixels[pixel + 1] = 40;
      source.pixels[pixel + 2] = layer == 2 ? 220 : 30;
      source.pixels[pixel + 3] = 255;
    }
  }
  return source;
}

TEST(ProfileDeformationTest, NeutralPoseReproducesSourceExactly) {
  std::vector<uint8_t> layers;
  const RgbaImage source = TwoLayerSource(16, 16, &layers);
  const std::vector<ProfileControlPoint> joints = {
      {.x = 2.0, .y = 8.0},
      {.x = 8.0, .y = 8.0},
      {.x = 13.0, .y = 8.0},
  };
  const std::vector<ProfileControlBone> bones = {
      {.start_joint = 0, .end_joint = 1},
      {.start_joint = 1, .end_joint = 2},
  };

  const absl::StatusOr<ProfileDeformationResult> result = DeformProfileArtwork(
      source, layers, layers, joints, joints, bones, ProfileDeformationConfig{});

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->image.pixels, source.pixels);
  EXPECT_EQ(result->mapped_pixels, 16 * 16);
  EXPECT_EQ(result->unmapped_pixels, 0);
}

TEST(ProfileDeformationTest, RotatedBoneSamplesItsOriginalLayer) {
  constexpr int kSize = 16;
  RgbaImage source{
      .width = kSize,
      .height = kSize,
      .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0),
  };
  std::vector<uint8_t> source_layers(kSize * kSize, 0);
  for (int x = 8; x <= 13; ++x) {
    const size_t index = static_cast<size_t>(8) * kSize + x;
    source_layers[index] = 1;
    source.pixels[index * 4] = 30;
    source.pixels[index * 4 + 1] = 80;
    source.pixels[index * 4 + 2] = 220;
    source.pixels[index * 4 + 3] = 255;
  }
  std::vector<uint8_t> target_layers(kSize * kSize, 0);
  for (int y = 8; y <= 13; ++y) {
    target_layers[static_cast<size_t>(y) * kSize + 8] = 1;
  }
  const std::vector<ProfileControlPoint> source_joints = {
      {.x = 8.0, .y = 8.0},
      {.x = 13.0, .y = 8.0},
  };
  const std::vector<ProfileControlPoint> target_joints = {
      {.x = 8.0, .y = 8.0},
      {.x = 8.0, .y = 13.0},
  };
  const std::vector<ProfileControlBone> bones = {
      {.start_joint = 0, .end_joint = 1},
  };

  const absl::StatusOr<ProfileDeformationResult> result =
      DeformProfileArtwork(source, source_layers, target_layers, source_joints, target_joints,
                           bones, ProfileDeformationConfig{.joint_blend_radius = 2.0});

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->mapped_pixels, 6);
  EXPECT_EQ(result->unmapped_pixels, 0);
  const size_t rotated_tip = (static_cast<size_t>(13) * kSize + 8) * 4;
  EXPECT_EQ(result->image.pixels[rotated_tip], 30);
  EXPECT_EQ(result->image.pixels[rotated_tip + 2], 220);
  EXPECT_EQ(result->image.pixels[rotated_tip + 3], 255);
}

TEST(ProfileDeformationTest, RejectsUnknownTargetLayer) {
  std::vector<uint8_t> source_layers;
  const RgbaImage source = TwoLayerSource(4, 4, &source_layers);
  std::vector<uint8_t> target_layers(16, 3);
  const std::vector<ProfileControlPoint> joints = {
      {.x = 0.0, .y = 0.0},
      {.x = 3.0, .y = 0.0},
  };
  const std::vector<ProfileControlBone> bones = {
      {.start_joint = 0, .end_joint = 1},
  };

  const absl::Status status = DeformProfileArtwork(source, source_layers, target_layers, joints,
                                                   joints, bones, ProfileDeformationConfig{})
                                  .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("unknown bone layer"), std::string::npos);
}

}  // namespace
}  // namespace zebes
