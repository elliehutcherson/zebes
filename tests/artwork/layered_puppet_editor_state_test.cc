#include "artwork/layered_puppet_editor_state.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "artwork/profile_silhouette.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr char kDigest[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

LayeredPuppetEditorState State() {
  return {
      .version = 1,
      .source_rgba_digest = kDigest,
      .puppet_contract_digest = kDigest,
      .anchor_frame = "frame_b",
      .source_joints = {{.x = 10, .y = 20}, {.x = 30, .y = 40}},
      .painted_meshes = {{"front_arm", {1, 3, 7}}},
      .frame_overrides = {{"frame_a", {{.x = 2, .y = -1}, {.x = 0, .y = 3}}}},
  };
}

LayeredPuppetEditorStateContract Contract() {
  return {
      .source_rgba_digest = kDigest,
      .puppet_contract_digest = kDigest,
      .frame_names = {"frame_a", "frame_b"},
      .joint_count = 2,
      .part_triangle_counts = {{"front_arm", 8}},
  };
}

TEST(LayeredPuppetEditorStateTest, RoundTripsValidatedAuthoredState) {
  const LayeredPuppetEditorState expected = State();

  ASSERT_OK_AND_ASSIGN(const LayeredPuppetEditorState parsed,
                       ParseLayeredPuppetEditorState(LayeredPuppetEditorStateToJson(expected)));

  ASSERT_OK(ValidateLayeredPuppetEditorState(parsed, Contract()));
  EXPECT_EQ(parsed.anchor_frame, expected.anchor_frame);
  EXPECT_EQ(parsed.source_joints.size(), 2);
  EXPECT_EQ(parsed.painted_meshes.at("front_arm"), (std::vector<size_t>{1, 3, 7}));
  EXPECT_EQ(parsed.frame_overrides.at("frame_a").size(), 2);
}

TEST(LayeredPuppetEditorStateTest, RoundTripsImmutableEditorContract) {
  const LayeredPuppetEditorStateContract expected = Contract();

  ASSERT_OK_AND_ASSIGN(
      const LayeredPuppetEditorStateContract parsed,
      ParseLayeredPuppetEditorStateContract(LayeredPuppetEditorStateContractToJson(expected)));

  EXPECT_EQ(parsed.source_rgba_digest, expected.source_rgba_digest);
  EXPECT_EQ(parsed.puppet_contract_digest, expected.puppet_contract_digest);
  EXPECT_EQ(parsed.frame_names, expected.frame_names);
  EXPECT_EQ(parsed.part_triangle_counts, expected.part_triangle_counts);
}

TEST(LayeredPuppetEditorStateTest, DerivesEveryFrameFromSelectedAnchorAndOverride) {
  const LayeredPuppetEditorState state = State();
  const std::vector<std::string> names{"frame_a", "frame_b"};
  const std::vector<std::vector<ProfileControlPoint>> canonical{
      {{.x = 4, .y = 8}, {.x = 10, .y = 12}},
      {{.x = 7, .y = 6}, {.x = 14, .y = 15}},
  };

  ASSERT_OK_AND_ASSIGN(const std::vector<std::vector<ProfileControlPoint>> poses,
                       DeriveLayeredPuppetEditorPoses(state, names, canonical));

  EXPECT_DOUBLE_EQ(poses[1][0].x, 10);
  EXPECT_DOUBLE_EQ(poses[1][0].y, 20);
  EXPECT_DOUBLE_EQ(poses[0][0].x, 9);
  EXPECT_DOUBLE_EQ(poses[0][0].y, 21);
  EXPECT_DOUBLE_EQ(poses[0][1].x, 26);
  EXPECT_DOUBLE_EQ(poses[0][1].y, 40);
}

TEST(LayeredPuppetEditorStateTest, RejectsMeshOutsideCurrentTopology) {
  LayeredPuppetEditorState state = State();
  state.painted_meshes.at("front_arm").push_back(8);

  const absl::Status status = ValidateLayeredPuppetEditorState(state, Contract());

  EXPECT_TRUE(absl::IsInvalidArgument(status));
}

}  // namespace
}  // namespace zebes
