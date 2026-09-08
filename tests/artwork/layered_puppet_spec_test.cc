#include "artwork/layered_puppet_spec.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr int kSize = 32;

// A vertical bar the torso owns and a horizontal bar the arm owns, so a spec
// can give each part a polygon that selects real opaque pixels.
RgbaImage SourceImage() {
  RgbaImage source{
      .width = kSize,
      .height = kSize,
      .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0),
  };
  const auto paint = [&source](int left, int top, int right, int bottom, uint8_t red) {
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const size_t offset = (static_cast<size_t>(y) * kSize + x) * 4;
        source.pixels[offset] = red;
        source.pixels[offset + 1] = 120;
        source.pixels[offset + 2] = 80;
        source.pixels[offset + 3] = 255;
      }
    }
  };
  paint(14, 4, 20, 24, 60);
  paint(18, 10, 30, 16, 200);
  return source;
}

std::string MinimalSpec() {
  return R"({
    "version": 1,
    "width": 32,
    "height": 32,
    "joints": {
      "neck": [17, 5],
      "hip": [17, 23],
      "shoulder": [19, 12],
      "elbow": [25, 13],
      "wrist": [29, 13]
    },
    "bones": [
      {"name": "torso", "start": "neck", "end": "hip", "may_stretch": false},
      {"name": "upper_arm", "start": "shoulder", "end": "elbow", "may_stretch": false},
      {"name": "forearm", "start": "elbow", "end": "wrist", "may_stretch": false}
    ],
    "pose_order": ["rest", "reach"],
    "poses": {
      "rest": {
        "neck": [17, 5], "hip": [17, 23], "shoulder": [19, 12],
        "elbow": [25, 13], "wrist": [29, 13]
      },
      "reach": {
        "neck": [17, 5], "hip": [17, 23], "shoulder": [19, 12],
        "elbow": [24, 9], "wrist": [27, 6]
      }
    },
    "parts": [
      {
        "name": "arm",
        "bones": ["upper_arm", "forearm"],
        "source_polygons": [[[18, 10], [30, 10], [30, 16], [18, 16]]],
        "mesh_spacing": 4,
        "joint_blend_radius": 4.0
      },
      {
        "name": "body",
        "bone": "torso",
        "source_polygons": [[[14, 4], [20, 4], [20, 24], [14, 24]]]
      }
    ],
    "draw_order": {
      "rest": ["body", "arm"],
      "reach": ["body", "arm"]
    }
  })";
}

// Returns MinimalSpec with one exact substring swapped, so each failure test
// changes only the field it is about.
std::string SpecWith(const std::string& original, const std::string& replacement) {
  std::string spec = MinimalSpec();
  const size_t position = spec.find(original);
  EXPECT_NE(position, std::string::npos) << "test fixture no longer contains: " << original;
  spec.replace(position, original.size(), replacement);
  return spec;
}

TEST(LayeredPuppetSpecTest, BuildsEveryJointBonePartAndPose) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(), MinimalSpec(), nullptr);
  ASSERT_OK(build);

  const LayeredPuppet& puppet = build->puppet;
  EXPECT_EQ(puppet.width, kSize);
  EXPECT_EQ(puppet.height, kSize);
  EXPECT_EQ(puppet.source_joints.size(), 5u);
  EXPECT_EQ(puppet.bones.size(), 3u);
  ASSERT_EQ(puppet.parts.size(), 2u);
  ASSERT_EQ(puppet.poses.size(), 2u);
  EXPECT_EQ(puppet.poses[0].name, "rest");
  EXPECT_EQ(puppet.poses[1].name, "reach");
  EXPECT_TRUE(build->stretch_reports.empty());
}

TEST(LayeredPuppetSpecTest, PoseOrderDrivesFrameOrderAndDrawOrder) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(), MinimalSpec(), nullptr);
  ASSERT_OK(build);

  const LayeredPuppet& puppet = build->puppet;
  const size_t arm = 0;
  const size_t body = 1;
  EXPECT_EQ(puppet.parts[arm].name, "arm");
  EXPECT_EQ(puppet.parts[body].name, "body");
  for (const LayeredPuppetPose& pose : puppet.poses) {
    ASSERT_EQ(pose.draw_order.size(), 2u);
    EXPECT_EQ(pose.draw_order[0], body);
    EXPECT_EQ(pose.draw_order[1], arm);
  }
}

TEST(LayeredPuppetSpecTest, TwoBonePartGetsAMeshAndOneBonePartDoesNot) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(), MinimalSpec(), nullptr);
  ASSERT_OK(build);

  EXPECT_FALSE(build->puppet.parts[0].mesh.triangles.empty());
  EXPECT_TRUE(build->puppet.parts[1].mesh.triangles.empty());
}

TEST(LayeredPuppetSpecTest, RejectsMalformedJsonWithoutThrowing) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(), "{\"version\": 1,", nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kDataLoss);
}

TEST(LayeredPuppetSpecTest, RejectsMissingRequiredFieldWithoutThrowing) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(), "{\"version\": 1, \"width\": 32}", nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kDataLoss);
}

TEST(LayeredPuppetSpecTest, RejectsInvalidSource) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(RgbaImage{}, MinimalSpec(), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsUnsupportedVersion) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"version\": 1", "\"version\": 2"), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsDimensionsThatDisagreeWithTheSource) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"width\": 32", "\"width\": 48"), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsBoneNamingAnUnknownJoint) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(),
      SpecWith("\"start\": \"neck\", \"end\": \"hip\"", "\"start\": \"tail\", \"end\": \"hip\""),
      nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsDuplicateBoneName) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"name\": \"forearm\"", "\"name\": \"upper_arm\""), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsPartNamingAnUnknownBone) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"bone\": \"torso\"", "\"bone\": \"spine\""), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsPartNameThatCannotBeAFileName) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"name\": \"body\"", "\"name\": \"../body\""), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsBoneChainLongerThanTwo) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build =
      BuildLayeredPuppetFromSpec(SourceImage(),
                                 SpecWith("\"bones\": [\"upper_arm\", \"forearm\"]",
                                          "\"bones\": [\"upper_arm\", \"forearm\", \"torso\"]"),
                                 nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsPolygonWithFewerThanThreePoints) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("[[14, 4], [20, 4], [20, 24], [14, 24]]", "[[14, 4], [20, 4]]"),
      nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsPoseOrderNamingAnUnknownPose) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("[\"rest\", \"reach\"]", "[\"rest\", \"sprint\"]"), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsDuplicatePoseOrderEntry) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("[\"rest\", \"reach\"]", "[\"rest\", \"rest\"]"), nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsDrawOrderNamingAnUnknownPart) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(), SpecWith("\"rest\": [\"body\", \"arm\"]", "\"rest\": [\"body\", \"tail\"]"),
      nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsSemanticPartWithoutSemanticSource) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(),
      SpecWith("\"bone\": \"torso\"", "\"bone\": \"torso\", \"semantic_tag\": \"topwear\""),
      nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LayeredPuppetSpecTest, RejectsSourceExclusionsWithoutOwnershipPolygons) {
  const absl::StatusOr<LayeredPuppetSpecBuild> build = BuildLayeredPuppetFromSpec(
      SourceImage(),
      SpecWith("\"source_polygons\": [[[14, 4], [20, 4], [20, 24], [14, 24]]]",
               "\"source_exclude_polygons\": [[[14, 4], [20, 4], [20, 24], [14, 24]]]"),
      nullptr);
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(build.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
