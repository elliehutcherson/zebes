#include "artwork/skeleton_rig_review.h"

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr std::string_view kValidRig = R"json({
  "bones": [
    {"end": "toe_l", "start": "hip_c"},
    {"end": "toe_r", "start": "hip_c"}
  ],
  "clips": {
    "run": {
      "fps": 8,
      "frames": [
        {
          "label": "first",
          "pose": {"hip_c": [5, 5], "toe_l": [9, 9], "toe_r": [1, 9]},
          "underlay": ""
        },
        {
          "label": "second",
          "pose": {"hip_c": [5, 7], "toe_l": [1, 11], "toe_r": [9, 11]},
          "underlay": ""
        }
      ],
      "name": "run"
    }
  },
  "floor_y": 12,
  "points": [
    {"chain": "spine", "name": "hip_c"},
    {"chain": "leg_l", "name": "toe_l"},
    {"chain": "leg_r", "name": "toe_r"}
  ],
  "updated_at": "2026-09-05T00:00:00Z",
  "version": 2
})json";

TEST(SkeletonRigReviewTest, ParsesAndMeasuresAlternatingCycle) {
  ASSERT_OK_AND_ASSIGN(const SkeletonRig rig, ParseSkeletonRig(kValidRig));
  ASSERT_OK_AND_ASSIGN(const SkeletonRigClip* clip, FindSkeletonRigClip(rig, "run"));
  ASSERT_OK_AND_ASSIGN(const SkeletonRigClipMetrics metrics, MeasureSkeletonRigClip(rig, *clip));
  ASSERT_OK_AND_ASSIGN(const std::string html, RenderSkeletonRigReviewHtml(rig, *clip, 16, 16));
  ASSERT_OK_AND_ASSIGN(const RgbaImage frame_image,
                       RenderSkeletonRigFrameImage(rig, clip->frames.front(), 16, 16, 2));
  ASSERT_OK_AND_ASSIGN(const RgbaImage sheet_image,
                       RenderSkeletonRigSheetImage(rig, *clip, 16, 16, 2, 2));

  EXPECT_EQ(clip->frames.size(), 2);
  EXPECT_DOUBLE_EQ(metrics.hip_oscillation, 2.0);
  EXPECT_TRUE(metrics.left_foot_leads);
  EXPECT_TRUE(metrics.right_foot_leads);
  EXPECT_DOUBLE_EQ(metrics.maximum_bone_length_drift, 0.0);
  EXPECT_NE(html.find("const labels = [\"first\",\"second\"];"), std::string::npos);
  ASSERT_TRUE(frame_image.IsValid());
  EXPECT_EQ(frame_image.width, 32);
  EXPECT_EQ(frame_image.height, 32);
  const size_t hip_pixel = (static_cast<size_t>(10) * frame_image.width + 10) * 4;
  EXPECT_EQ(frame_image.pixels[hip_pixel + 0], 0xEB);
  EXPECT_EQ(frame_image.pixels[hip_pixel + 1], 0x46);
  EXPECT_EQ(sheet_image.width, 64);
  EXPECT_EQ(sheet_image.height, 32);
}

TEST(SkeletonRigReviewTest, RejectsIncompletePose) {
  std::string invalid(kValidRig);
  const std::string complete = "\"toe_l\": [1, 11], \"toe_r\": [9, 11]";
  const size_t position = invalid.find(complete);
  ASSERT_NE(position, std::string::npos);
  invalid.replace(position, complete.size(), "\"toe_l\": [1, 11]");

  const absl::Status status = ParseSkeletonRig(invalid).status();

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_NE(status.message().find("does not contain every point"), std::string_view::npos);
}

TEST(SkeletonRigReviewTest, CheckedInRunOwnsTwelveStableReferencePoses) {
  const std::filesystem::path path =
      std::filesystem::path(ZEBES_SOURCE_DIR) /
      "experiments/character_binding/out/codex-pose-conditioning-v1/rig-bench.json";
  ASSERT_OK_AND_ASSIGN(const SkeletonRig rig, LoadSkeletonRig(path));
  ASSERT_OK_AND_ASSIGN(const SkeletonRigClip* clip, FindSkeletonRigClip(rig, "run"));
  ASSERT_OK_AND_ASSIGN(const SkeletonRigClipMetrics metrics, MeasureSkeletonRigClip(rig, *clip));

  EXPECT_EQ(rig.points.size(), 23);
  EXPECT_EQ(rig.bones.size(), 22);
  ASSERT_EQ(clip->frames.size(), 12);
  EXPECT_EQ(clip->frames.front().label, "reference_01");
  EXPECT_EQ(clip->frames.back().label, "reference_12");
  EXPECT_GE(metrics.hip_oscillation, 12.0);
  EXPECT_LE(metrics.maximum_bone_length_drift, 1.5);
  EXPECT_TRUE(metrics.left_foot_leads);
  EXPECT_TRUE(metrics.right_foot_leads);
  for (const SkeletonRigFrame& frame : clip->frames) {
    EXPECT_FALSE(frame.pose.contains("hip_l"));
    EXPECT_FALSE(frame.pose.contains("hip_r"));
    EXPECT_FALSE(frame.pose.contains("heel_l"));
    EXPECT_FALSE(frame.pose.contains("heel_r"));
  }
}

}  // namespace
}  // namespace zebes
