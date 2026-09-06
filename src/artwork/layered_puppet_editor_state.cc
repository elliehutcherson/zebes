#include "artwork/layered_puppet_editor_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/json_schema.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

using json_schema::Required;
using json_schema::RequireExactObject;

constexpr int kStateVersion = 1;

absl::StatusOr<ProfileControlPoint> ParsePoint(const nlohmann::json& value,
                                               std::string_view context) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must contain x and y"));
  }
  const ProfileControlPoint point{.x = value[0].get<double>(), .y = value[1].get<double>()};
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be finite"));
  }
  return point;
}

absl::StatusOr<std::vector<ProfileControlPoint>> ParsePoints(const nlohmann::json& value,
                                                             std::string_view context) {
  if (!value.is_array()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an array"));
  }
  std::vector<ProfileControlPoint> points;
  points.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    ASSIGN_OR_RETURN(ProfileControlPoint point,
                     ParsePoint(value[index], absl::StrCat(context, "[", index, "]")));
    points.push_back(point);
  }
  return points;
}

bool IsDigest(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(),
                     [](const unsigned char character) { return std::isxdigit(character) != 0; });
}

nlohmann::json PointsJson(const std::vector<ProfileControlPoint>& points) {
  nlohmann::json result = nlohmann::json::array();
  for (const ProfileControlPoint point : points) result.push_back({point.x, point.y});
  return result;
}

absl::Status ValidateCanonicalPoses(const std::vector<std::string>& names,
                                    const std::vector<std::vector<ProfileControlPoint>>& poses,
                                    size_t joint_count) {
  if (names.empty() || names.size() != poses.size()) {
    return absl::InvalidArgumentError("canonical editor poses and names must be non-empty peers");
  }
  absl::flat_hash_set<std::string> unique_names;
  for (size_t index = 0; index < names.size(); ++index) {
    if (names[index].empty() || !unique_names.insert(names[index]).second ||
        poses[index].size() != joint_count) {
      return absl::InvalidArgumentError(
          "canonical editor poses contain duplicate names or wrong joint counts");
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<LayeredPuppetEditorState> ParseLayeredPuppetEditorState(std::string_view encoded) {
  nlohmann::json value;
  try {
    value = nlohmann::json::parse(encoded);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError(absl::StrCat("invalid layered puppet editor state: ", error.what()));
  }
  RETURN_IF_ERROR(
      RequireExactObject(value,
                         {"version", "source_rgba_digest", "puppet_contract_digest", "anchor_frame",
                          "source_joints", "painted_meshes", "frame_overrides"},
                         "layered puppet editor state"));

  LayeredPuppetEditorState state;
  ASSIGN_OR_RETURN(state.version, Required<int>(value, "version", "layered puppet editor state"));
  ASSIGN_OR_RETURN(state.source_rgba_digest, Required<std::string>(value, "source_rgba_digest",
                                                                   "layered puppet editor state"));
  ASSIGN_OR_RETURN(
      state.puppet_contract_digest,
      Required<std::string>(value, "puppet_contract_digest", "layered puppet editor state"));
  ASSIGN_OR_RETURN(state.anchor_frame,
                   Required<std::string>(value, "anchor_frame", "layered puppet editor state"));
  ASSIGN_OR_RETURN(state.source_joints,
                   ParsePoints(value.at("source_joints"), "layered puppet editor source_joints"));
  if (state.version != kStateVersion || !IsDigest(state.source_rgba_digest) ||
      !IsDigest(state.puppet_contract_digest) || state.anchor_frame.empty() ||
      state.source_joints.empty()) {
    return absl::InvalidArgumentError(
        "layered puppet editor state has an invalid version, digest, anchor, or joints");
  }

  const nlohmann::json& meshes = value.at("painted_meshes");
  if (!meshes.is_object()) {
    return absl::InvalidArgumentError("layered puppet editor painted_meshes must be an object");
  }
  for (const auto& [name, indices_json] : meshes.items()) {
    if (name.empty() || !indices_json.is_array()) {
      return absl::InvalidArgumentError("layered puppet editor painted mesh is invalid");
    }
    std::vector<size_t> indices;
    indices.reserve(indices_json.size());
    for (const nlohmann::json& index_json : indices_json) {
      if (!index_json.is_number_unsigned() && !index_json.is_number_integer()) {
        return absl::InvalidArgumentError("layered puppet editor mesh index must be an integer");
      }
      const int64_t index = index_json.get<int64_t>();
      if (index < 0 || (!indices.empty() && static_cast<size_t>(index) <= indices.back())) {
        return absl::InvalidArgumentError(
            "layered puppet editor mesh indices must be nonnegative, sorted, and unique");
      }
      indices.push_back(static_cast<size_t>(index));
    }
    state.painted_meshes.emplace(name, std::move(indices));
  }

  const nlohmann::json& overrides = value.at("frame_overrides");
  if (!overrides.is_object()) {
    return absl::InvalidArgumentError("layered puppet editor frame_overrides must be an object");
  }
  for (const auto& [name, points_json] : overrides.items()) {
    ASSIGN_OR_RETURN(
        std::vector<ProfileControlPoint> points,
        ParsePoints(points_json, absl::StrCat("layered puppet editor frame_overrides.", name)));
    state.frame_overrides.emplace(name, std::move(points));
  }
  return state;
}

std::string LayeredPuppetEditorStateToJson(const LayeredPuppetEditorState& state) {
  nlohmann::json meshes = nlohmann::json::object();
  for (const auto& [name, indices] : state.painted_meshes) meshes[name] = indices;
  nlohmann::json overrides = nlohmann::json::object();
  for (const auto& [name, points] : state.frame_overrides) {
    overrides[name] = PointsJson(points);
  }
  return nlohmann::json({
                            {"version", state.version},
                            {"source_rgba_digest", state.source_rgba_digest},
                            {"puppet_contract_digest", state.puppet_contract_digest},
                            {"anchor_frame", state.anchor_frame},
                            {"source_joints", PointsJson(state.source_joints)},
                            {"painted_meshes", std::move(meshes)},
                            {"frame_overrides", std::move(overrides)},
                        })
             .dump(2) +
         "\n";
}

absl::StatusOr<LayeredPuppetEditorStateContract> ParseLayeredPuppetEditorStateContract(
    std::string_view encoded) {
  nlohmann::json value;
  try {
    value = nlohmann::json::parse(encoded);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError(
        absl::StrCat("invalid layered puppet editor contract: ", error.what()));
  }
  RETURN_IF_ERROR(RequireExactObject(value,
                                     {"version", "source_rgba_digest", "puppet_contract_digest",
                                      "frame_names", "joint_count", "part_triangle_counts"},
                                     "layered puppet editor contract"));
  ASSIGN_OR_RETURN(const int version,
                   Required<int>(value, "version", "layered puppet editor contract"));
  LayeredPuppetEditorStateContract contract;
  ASSIGN_OR_RETURN(
      contract.source_rgba_digest,
      Required<std::string>(value, "source_rgba_digest", "layered puppet editor contract"));
  ASSIGN_OR_RETURN(
      contract.puppet_contract_digest,
      Required<std::string>(value, "puppet_contract_digest", "layered puppet editor contract"));
  ASSIGN_OR_RETURN(
      contract.frame_names,
      Required<std::vector<std::string>>(value, "frame_names", "layered puppet editor contract"));
  ASSIGN_OR_RETURN(contract.joint_count,
                   Required<size_t>(value, "joint_count", "layered puppet editor contract"));
  if (version != kStateVersion || !IsDigest(contract.source_rgba_digest) ||
      !IsDigest(contract.puppet_contract_digest) || contract.frame_names.empty() ||
      contract.joint_count == 0) {
    return absl::InvalidArgumentError(
        "layered puppet editor contract has an invalid version, digest, frames, or joints");
  }
  absl::flat_hash_set<std::string> frame_names;
  for (const std::string& name : contract.frame_names) {
    if (name.empty() || !frame_names.insert(name).second) {
      return absl::InvalidArgumentError(
          "layered puppet editor contract frame names must be non-empty and unique");
    }
  }
  const nlohmann::json& counts = value.at("part_triangle_counts");
  if (!counts.is_object() || counts.empty()) {
    return absl::InvalidArgumentError(
        "layered puppet editor contract part_triangle_counts must be an object");
  }
  for (const auto& [name, count] : counts.items()) {
    if (name.empty() || !count.is_number_unsigned()) {
      return absl::InvalidArgumentError(
          "layered puppet editor contract part triangle count is invalid");
    }
    contract.part_triangle_counts.emplace(name, count.get<size_t>());
  }
  return contract;
}

std::string LayeredPuppetEditorStateContractToJson(
    const LayeredPuppetEditorStateContract& contract) {
  nlohmann::json counts = nlohmann::json::object();
  for (const auto& [name, count] : contract.part_triangle_counts) counts[name] = count;
  return nlohmann::json({
                            {"version", kStateVersion},
                            {"source_rgba_digest", contract.source_rgba_digest},
                            {"puppet_contract_digest", contract.puppet_contract_digest},
                            {"frame_names", contract.frame_names},
                            {"joint_count", contract.joint_count},
                            {"part_triangle_counts", std::move(counts)},
                        })
             .dump(2) +
         "\n";
}

absl::Status ValidateLayeredPuppetEditorState(const LayeredPuppetEditorState& state,
                                              const LayeredPuppetEditorStateContract& contract) {
  if (state.version != kStateVersion || state.source_rgba_digest != contract.source_rgba_digest ||
      state.puppet_contract_digest != contract.puppet_contract_digest ||
      state.source_joints.size() != contract.joint_count) {
    return absl::FailedPreconditionError(
        "layered puppet editor state does not match the source/spec/topology contract");
  }
  const absl::flat_hash_set<std::string> frames(contract.frame_names.begin(),
                                                contract.frame_names.end());
  if (!frames.contains(state.anchor_frame)) {
    return absl::InvalidArgumentError("layered puppet editor anchor frame is unknown");
  }
  for (const auto& [name, indices] : state.painted_meshes) {
    const auto part = contract.part_triangle_counts.find(name);
    if (part == contract.part_triangle_counts.end() ||
        (!indices.empty() && indices.back() >= part->second)) {
      return absl::InvalidArgumentError(
          "layered puppet editor painted mesh references an unknown triangle");
    }
  }
  for (const auto& [name, offsets] : state.frame_overrides) {
    if (!frames.contains(name) || offsets.size() != contract.joint_count) {
      return absl::InvalidArgumentError(
          "layered puppet editor override references an unknown frame or wrong joint count");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::vector<ProfileControlPoint>>> DeriveLayeredPuppetEditorPoses(
    const LayeredPuppetEditorState& state, const std::vector<std::string>& canonical_frame_names,
    const std::vector<std::vector<ProfileControlPoint>>& canonical_poses) {
  RETURN_IF_ERROR(
      ValidateCanonicalPoses(canonical_frame_names, canonical_poses, state.source_joints.size()));
  const auto anchor =
      std::find(canonical_frame_names.begin(), canonical_frame_names.end(), state.anchor_frame);
  if (anchor == canonical_frame_names.end()) {
    return absl::InvalidArgumentError("layered puppet editor anchor frame is not canonical");
  }
  const size_t anchor_index = static_cast<size_t>(anchor - canonical_frame_names.begin());
  std::vector<std::vector<ProfileControlPoint>> result;
  result.reserve(canonical_poses.size());
  for (size_t frame_index = 0; frame_index < canonical_poses.size(); ++frame_index) {
    const auto override = state.frame_overrides.find(canonical_frame_names[frame_index]);
    if (override != state.frame_overrides.end() &&
        override->second.size() != state.source_joints.size()) {
      return absl::InvalidArgumentError("layered puppet editor override has wrong joint count");
    }
    std::vector<ProfileControlPoint> pose;
    pose.reserve(state.source_joints.size());
    for (size_t joint = 0; joint < state.source_joints.size(); ++joint) {
      const ProfileControlPoint offset =
          override == state.frame_overrides.end() ? ProfileControlPoint{} : override->second[joint];
      pose.push_back({
          .x = state.source_joints[joint].x + canonical_poses[frame_index][joint].x -
               canonical_poses[anchor_index][joint].x + offset.x,
          .y = state.source_joints[joint].y + canonical_poses[frame_index][joint].y -
               canonical_poses[anchor_index][joint].y + offset.y,
      });
    }
    result.push_back(std::move(pose));
  }
  return result;
}

}  // namespace zebes
