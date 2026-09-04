#include "artwork/layered_puppet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

constexpr int kSize = 12;

size_t Offset(int x, int y) { return (static_cast<size_t>(y) * kSize + x) * 4; }

void SetPixel(RgbaImage& image, int x, int y, std::array<uint8_t, 4> color) {
  const size_t offset = Offset(x, y);
  for (size_t channel = 0; channel < color.size(); ++channel) {
    image.pixels[offset + channel] = color[channel];
  }
}

RgbaImage SourceImage() {
  RgbaImage source{
      .width = kSize,
      .height = kSize,
      .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0),
  };
  for (int y = 2; y < 10; ++y) {
    for (int x = 4; x < 6; ++x) SetPixel(source, x, y, {48, 144, 80, 255});
  }
  for (int y = 4; y < 6; ++y) {
    for (int x = 5; x < 10; ++x) SetPixel(source, x, y, {208, 48, 48, 255});
  }
  return source;
}

LayeredPuppetPolygon Rectangle(double left, double top, double right, double bottom) {
  return LayeredPuppetPolygon{.points = {
                                  {.x = left, .y = top},
                                  {.x = right, .y = top},
                                  {.x = right, .y = bottom},
                                  {.x = left, .y = bottom},
                              }};
}

absl::StatusOr<LayeredPuppet> TestPuppet() {
  const RgbaImage source = SourceImage();
  const LayeredPuppetFill torso_fill{
      .polygon = Rectangle(4, 2, 6, 10),
      .color = {48, 144, 80, 255},
  };
  const LayeredPuppetPolygon arm_polygon = Rectangle(5, 4, 10, 6);
  absl::StatusOr<RgbaImage> torso = BuildLayeredPuppetPartArtwork(source, {}, {&torso_fill, 1});
  if (!torso.ok()) return torso.status();
  absl::StatusOr<RgbaImage> arm = BuildLayeredPuppetPartArtwork(source, {&arm_polygon, 1}, {});
  if (!arm.ok()) return arm.status();

  LayeredPuppet puppet{
      .width = kSize,
      .height = kSize,
      .source_joints = {{.x = 5, .y = 5}, {.x = 5, .y = 9}, {.x = 9, .y = 5}},
      .bones = {{.start_joint = 0, .end_joint = 1}, {.start_joint = 0, .end_joint = 2}},
      .parts =
          {
              {.name = "torso", .bone_index = 0, .artwork = std::move(*torso)},
              {.name = "arm", .bone_index = 1, .artwork = std::move(*arm)},
          },
      .poses =
          {
              {.name = "neutral",
               .joints = {{.x = 5, .y = 5}, {.x = 5, .y = 9}, {.x = 9, .y = 5}},
               .draw_order = {0, 1}},
              {.name = "contact",
               .joints = {{.x = 5, .y = 5}, {.x = 5, .y = 9}, {.x = 8, .y = 8}},
               .draw_order = {0, 1}},
          },
  };
  return puppet;
}

TEST(LayeredPuppetTest, SeparatedArmMovesWithoutWarpingTorso) {
  const absl::StatusOr<LayeredPuppet> puppet = TestPuppet();
  ASSERT_TRUE(puppet.ok()) << puppet.status();

  const absl::StatusOr<RgbaImage> neutral = RenderLayeredPuppetPose(*puppet, puppet->poses[0]);
  const absl::StatusOr<RgbaImage> contact = RenderLayeredPuppetPose(*puppet, puppet->poses[1]);

  ASSERT_TRUE(neutral.ok()) << neutral.status();
  ASSERT_TRUE(contact.ok()) << contact.status();
  EXPECT_EQ(std::vector<uint8_t>(neutral->pixels.begin() + Offset(9, 5),
                                 neutral->pixels.begin() + Offset(9, 5) + 4),
            (std::vector<uint8_t>{208, 48, 48, 255}));
  EXPECT_EQ(contact->pixels[Offset(9, 5) + 3], 0);
  EXPECT_EQ(std::vector<uint8_t>(contact->pixels.begin() + Offset(8, 8),
                                 contact->pixels.begin() + Offset(8, 8) + 4),
            (std::vector<uint8_t>{208, 48, 48, 255}));
  EXPECT_EQ(std::vector<uint8_t>(contact->pixels.begin() + Offset(5, 8),
                                 contact->pixels.begin() + Offset(5, 8) + 4),
            (std::vector<uint8_t>{48, 144, 80, 255}));
}

TEST(LayeredPuppetTest, RejectsIncompleteDrawOrder) {
  absl::StatusOr<LayeredPuppet> puppet = TestPuppet();
  ASSERT_TRUE(puppet.ok()) << puppet.status();
  puppet->poses[0].draw_order = {0, 0};

  const absl::Status status = ValidateLayeredPuppet(*puppet);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("every part exactly once"), std::string::npos);
}

TEST(LayeredPuppetTest, DownsampleAndPackKeepFrameContract) {
  const absl::StatusOr<LayeredPuppet> puppet = TestPuppet();
  ASSERT_TRUE(puppet.ok()) << puppet.status();
  const absl::StatusOr<RgbaImage> neutral = RenderLayeredPuppetPose(*puppet, puppet->poses[0]);
  const absl::StatusOr<RgbaImage> contact = RenderLayeredPuppetPose(*puppet, puppet->poses[1]);
  ASSERT_TRUE(neutral.ok()) << neutral.status();
  ASSERT_TRUE(contact.ok()) << contact.status();
  const absl::StatusOr<RgbaImage> neutral_frame = DownsampleLayeredPuppetFrame(*neutral, 6);
  const absl::StatusOr<RgbaImage> contact_frame = DownsampleLayeredPuppetFrame(*contact, 6);
  ASSERT_TRUE(neutral_frame.ok()) << neutral_frame.status();
  ASSERT_TRUE(contact_frame.ok()) << contact_frame.status();

  const std::array<RgbaImage, 2> frames = {*neutral_frame, *contact_frame};
  const absl::StatusOr<RgbaImage> packed = PackLayeredPuppetFrames(frames);
  const absl::StatusOr<RgbaImage> zoomed = ZoomLayeredPuppetEvidence(*packed, 3);

  ASSERT_TRUE(packed.ok()) << packed.status();
  ASSERT_TRUE(zoomed.ok()) << zoomed.status();
  EXPECT_EQ(packed->width, 12);
  EXPECT_EQ(packed->height, 6);
  EXPECT_EQ(zoomed->width, 36);
  EXPECT_EQ(zoomed->height, 18);
}

}  // namespace
}  // namespace zebes
