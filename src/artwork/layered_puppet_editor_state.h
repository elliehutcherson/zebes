#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/profile_silhouette.h"

namespace zebes {

struct LayeredPuppetEditorState {
  int version = 1;
  std::string source_rgba_digest;
  std::string puppet_contract_digest;
  std::string anchor_frame;
  std::vector<ProfileControlPoint> source_joints;
  absl::flat_hash_map<std::string, std::vector<size_t>> painted_meshes;
  absl::flat_hash_map<std::string, std::vector<ProfileControlPoint>> frame_overrides;
};

struct LayeredPuppetEditorStateContract {
  std::string source_rgba_digest;
  std::string puppet_contract_digest;
  std::vector<std::string> frame_names;
  size_t joint_count = 0;
  absl::flat_hash_map<std::string, size_t> part_triangle_counts;
};

// Parses strict schema-version-1 authored editor state. Mesh indices must be
// sorted and unique; all joints and offsets must be finite.
absl::StatusOr<LayeredPuppetEditorState> ParseLayeredPuppetEditorState(std::string_view encoded);

// Produces canonical schema-version-1 JSON with deterministic key ordering.
std::string LayeredPuppetEditorStateToJson(const LayeredPuppetEditorState& state);

// Parses the immutable editor contract generated beside editor.html.
absl::StatusOr<LayeredPuppetEditorStateContract> ParseLayeredPuppetEditorStateContract(
    std::string_view encoded);

// Produces canonical schema-version-1 contract JSON.
std::string LayeredPuppetEditorStateContractToJson(
    const LayeredPuppetEditorStateContract& contract);

// Ensures state belongs to the exact source/spec/topology contract served by
// the editor. Missing painted-mesh entries mean "use the traced seed"; missing
// frame overrides mean zero adjustment.
absl::Status ValidateLayeredPuppetEditorState(const LayeredPuppetEditorState& state,
                                              const LayeredPuppetEditorStateContract& contract);

// Applies canonical per-joint motion relative to the selected anchor, then adds
// optional per-frame offsets. The anchor result equals source_joints plus its
// explicit override.
absl::StatusOr<std::vector<std::vector<ProfileControlPoint>>> DeriveLayeredPuppetEditorPoses(
    const LayeredPuppetEditorState& state, const std::vector<std::string>& canonical_frame_names,
    const std::vector<std::vector<ProfileControlPoint>>& canonical_poses);

}  // namespace zebes
