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
              {.name = "torso", .bone_indices = {0}, .artwork = std::move(*torso)},
              {.name = "arm", .bone_indices = {1}, .artwork = std::move(*arm)},
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

TEST(LayeredPuppetTest, CompleteArmMeshBendsWithoutNeutralReconstructionChanges) {
  RgbaImage artwork{
      .width = kSize,
      .height = kSize,
      .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0),
  };
  for (int y = 4; y < 7; ++y) {
    for (int x = 4; x < 10; ++x) SetPixel(artwork, x, y, {208, 48, 48, 255});
  }
  const std::vector<ProfileControlPoint> source_joints = {
      {.x = 5, .y = 5}, {.x = 7, .y = 5}, {.x = 9, .y = 5}};
  const std::vector<ProfileControlBone> bones = {{.start_joint = 0, .end_joint = 1},
                                                 {.start_joint = 1, .end_joint = 2}};
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<LayeredPuppetMesh> mesh =
      BuildLayeredPuppetMesh(artwork, bone_indices, bones, source_joints, 1, 1.0);
  ASSERT_TRUE(mesh.ok()) << mesh.status();
  const RgbaImage expected = artwork;
  const LayeredPuppet puppet{
      .width = kSize,
      .height = kSize,
      .source_joints = source_joints,
      .bones = bones,
      .parts =
          {{.name = "arm", .bone_indices = {0, 1}, .artwork = std::move(artwork), .mesh = *mesh}},
      .poses =
          {
              {.name = "neutral", .joints = source_joints, .draw_order = {0}},
              {.name = "bend",
               .joints = {{.x = 5, .y = 5}, {.x = 7, .y = 5}, {.x = 7, .y = 7}},
               .draw_order = {0}},
          },
  };

  const absl::StatusOr<RgbaImage> neutral = RenderLayeredPuppetPose(puppet, puppet.poses[0]);
  const absl::StatusOr<RgbaImage> bend = RenderLayeredPuppetPose(puppet, puppet.poses[1]);

  ASSERT_TRUE(neutral.ok()) << neutral.status();
  ASSERT_TRUE(bend.ok()) << bend.status();
  EXPECT_EQ(neutral->pixels, expected.pixels);
  EXPECT_NE(bend->pixels, expected.pixels);
  EXPECT_NE(bend->pixels[Offset(5, 5) + 3], 0);
  EXPECT_NE(bend->pixels[Offset(6, 5) + 3], 0);
  EXPECT_NE(bend->pixels[Offset(7, 5) + 3], 0);
  EXPECT_NE(bend->pixels[Offset(7, 6) + 3], 0);
  EXPECT_NE(bend->pixels[Offset(7, 7) + 3], 0);
}

// A thin horizontal bar in the middle of a wide canvas: a bounding-box grid
// would cover the whole canvas, a trimmed one only the bar and its collar.
struct BarMesh {
  RgbaImage artwork;
  std::vector<ProfileControlPoint> source_joints;
  std::vector<ProfileControlBone> bones;
};

BarMesh MakeBar() {
  BarMesh bar{
      .artwork = {.width = kSize,
                  .height = kSize,
                  .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0)},
      .source_joints = {{.x = 1, .y = 5}, {.x = 6, .y = 5}, {.x = 11, .y = 5}},
      .bones = {{.start_joint = 0, .end_joint = 1}, {.start_joint = 1, .end_joint = 2}},
  };
  for (int y = 4; y < 7; ++y) {
    for (int x = 1; x < 12; ++x) SetPixel(bar.artwork, x, y, {208, 48, 48, 255});
  }
  return bar;
}

TEST(LayeredPuppetTest, OwnershipTakesCandidatePixelsWithinReach) {
  const BarMesh bar = MakeBar();
  RgbaImage candidate = bar.artwork;
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<RgbaImage> mask =
      BuildLayeredPuppetOwnershipMask(candidate, bar.artwork, bone_indices, bar.bones,
                                      bar.source_joints, {.start = 4.0, .end = 4.0, .grow = 0});
  ASSERT_TRUE(mask.ok()) << mask.status();
  // The bar is the candidate and the source, and every bar pixel is within
  // four of the bone, so ownership takes all of it.
  size_t owned = 0;
  for (size_t offset = 3; offset < mask->pixels.size(); offset += 4) {
    if (mask->pixels[offset] != 0) ++owned;
  }
  EXPECT_EQ(owned, 33u);
}

TEST(LayeredPuppetTest, OwnershipRefusesCandidatePixelsBeyondReach) {
  const BarMesh bar = MakeBar();
  // A blob two rows below the bar, painted in the candidate but far from the
  // bone: a neighbouring body part the layer's alpha happens to cover.
  RgbaImage candidate = bar.artwork;
  for (int y = 9; y < 11; ++y) {
    for (int x = 8; x < 11; ++x) SetPixel(candidate, x, y, {120, 70, 40, 255});
  }
  RgbaImage source = candidate;
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<RgbaImage> mask =
      BuildLayeredPuppetOwnershipMask(candidate, source, bone_indices, bar.bones, bar.source_joints,
                                      {.start = 2.0, .end = 2.0, .grow = 0});
  ASSERT_TRUE(mask.ok()) << mask.status();
  for (int y = 9; y < 11; ++y) {
    for (int x = 8; x < 11; ++x) {
      EXPECT_EQ(mask->pixels[(static_cast<size_t>(y) * kSize + x) * 4 + 3], 0)
          << "blob pixel " << x << "," << y << " was claimed";
    }
  }
}

TEST(LayeredPuppetTest, OwnershipReachShrinksAlongTheChain) {
  const BarMesh bar = MakeBar();
  // Pixels three rows off the bone at both ends. Reach 3 at the start and 1 at
  // the end must keep the near-shoulder one and drop the near-hand one.
  RgbaImage candidate = bar.artwork;
  SetPixel(candidate, 2, 8, {120, 70, 40, 255});
  SetPixel(candidate, 10, 8, {120, 70, 40, 255});
  RgbaImage source = candidate;
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<RgbaImage> mask =
      BuildLayeredPuppetOwnershipMask(candidate, source, bone_indices, bar.bones, bar.source_joints,
                                      {.start = 3.5, .end = 1.0, .grow = 0});
  ASSERT_TRUE(mask.ok()) << mask.status();
  EXPECT_NE(mask->pixels[(static_cast<size_t>(8) * kSize + 2) * 4 + 3], 0);
  EXPECT_EQ(mask->pixels[(static_cast<size_t>(8) * kSize + 10) * 4 + 3], 0);
}

TEST(LayeredPuppetTest, OwnershipIgnoresCandidatePixelsOutsideTheSource) {
  const BarMesh bar = MakeBar();
  RgbaImage candidate = bar.artwork;
  SetPixel(candidate, 6, 8, {120, 70, 40, 255});
  const absl::StatusOr<RgbaImage> mask = BuildLayeredPuppetOwnershipMask(
      candidate, bar.artwork, std::array<size_t, 2>{0, 1}, bar.bones, bar.source_joints,
      {.start = 8.0, .end = 8.0, .grow = 0});
  ASSERT_TRUE(mask.ok()) << mask.status();
  EXPECT_EQ(mask->pixels[(static_cast<size_t>(8) * kSize + 6) * 4 + 3], 0);
}

TEST(LayeredPuppetTest, OwnershipRejectsNonPositiveReach) {
  const BarMesh bar = MakeBar();
  EXPECT_FALSE(BuildLayeredPuppetOwnershipMask(bar.artwork, bar.artwork,
                                               std::array<size_t, 2>{0, 1}, bar.bones,
                                               bar.source_joints, {.start = 0.0, .end = 4.0})
                   .ok());
}

TEST(LayeredPuppetTest, MaskedArtworkAndSubtractionArePixelComplements) {
  const BarMesh bar = MakeBar();
  const absl::StatusOr<RgbaImage> mask = BuildLayeredPuppetOwnershipMask(
      bar.artwork, bar.artwork, std::array<size_t, 2>{0, 1}, bar.bones, bar.source_joints,
      {.start = 1.0, .end = 1.0, .grow = 0});
  ASSERT_TRUE(mask.ok()) << mask.status();
  const absl::StatusOr<RgbaImage> taken = BuildLayeredPuppetMaskedArtwork(bar.artwork, *mask);
  ASSERT_TRUE(taken.ok()) << taken.status();
  RgbaImage left = bar.artwork;
  ASSERT_TRUE(SubtractLayeredPuppetMask(left, *mask).ok());
  for (size_t offset = 3; offset < bar.artwork.pixels.size(); offset += 4) {
    const bool present = bar.artwork.pixels[offset] != 0;
    const bool in_taken = taken->pixels[offset] != 0;
    const bool in_left = left.pixels[offset] != 0;
    EXPECT_EQ(present, in_taken || in_left);
    EXPECT_FALSE(in_taken && in_left);
  }
}

TEST(LayeredPuppetTest, MeshKeepsOnlyCellsCarryingArtwork) {
  const BarMesh bar = MakeBar();
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<LayeredPuppetMesh> mesh =
      BuildLayeredPuppetMesh(bar.artwork, bone_indices, bar.bones, bar.source_joints, 1, 1.0);
  ASSERT_TRUE(mesh.ok()) << mesh.status();

  // The bar spans rows 4-6, so with one cell of collar no vertex may sit more
  // than one row outside rows 3-7.
  for (const LayeredPuppetMeshVertex& vertex : mesh->vertices) {
    EXPECT_GE(vertex.source.y, 2.0);
    EXPECT_LE(vertex.source.y, 8.0);
  }
  EXPECT_LT(mesh->triangles.size(), static_cast<size_t>((kSize - 1) * (kSize - 1) * 2));
}

TEST(LayeredPuppetTest, LateralScaleWidensTheBlendAwayFromTheBone) {
  const BarMesh bar = MakeBar();
  const std::array<size_t, 2> bone_indices = {0, 1};
  const absl::StatusOr<LayeredPuppetMesh> narrow =
      BuildLayeredPuppetMesh(bar.artwork, bone_indices, bar.bones, bar.source_joints, 1, 1.0, 0.0);
  const absl::StatusOr<LayeredPuppetMesh> widened =
      BuildLayeredPuppetMesh(bar.artwork, bone_indices, bar.bones, bar.source_joints, 1, 1.0, 2.0);
  ASSERT_TRUE(narrow.ok()) << narrow.status();
  ASSERT_TRUE(widened.ok()) << widened.status();
  ASSERT_EQ(narrow->vertices.size(), widened->vertices.size());

  // Two vertices one cell before the shared joint at (6,5): one on the bone,
  // one two rows off it. Widening must move only the off-bone weight toward
  // the midpoint, leaving the on-bone weight where it was.
  const auto weight_at = [](const LayeredPuppetMesh& mesh, double x, double y) {
    for (const LayeredPuppetMeshVertex& vertex : mesh.vertices) {
      if (vertex.source.x == x && vertex.source.y == y) return vertex.first_bone_weight;
    }
    ADD_FAILURE() << "mesh has no vertex at " << x << "," << y;
    return 0.0;
  };
  EXPECT_DOUBLE_EQ(weight_at(*narrow, 5, 5), weight_at(*widened, 5, 5));
  EXPECT_LT(weight_at(*widened, 5, 3), weight_at(*narrow, 5, 3));
  EXPECT_GT(weight_at(*widened, 5, 3), 0.5);
}

TEST(LayeredPuppetTest, MeshRejectsNegativeLateralScale) {
  const BarMesh bar = MakeBar();
  const std::array<size_t, 2> bone_indices = {0, 1};
  EXPECT_FALSE(
      BuildLayeredPuppetMesh(bar.artwork, bone_indices, bar.bones, bar.source_joints, 1, 1.0, -1.0)
          .ok());
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
