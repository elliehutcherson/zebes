#include "artwork/skeleton_rig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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

constexpr int kSupportedVersion = 2;
constexpr double kJointRadius = 3.0;

absl::StatusOr<SkeletonRigJoint> ParseJoint(const nlohmann::json& json, std::string_view context) {
  if (!json.is_array() || json.size() != 2) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must contain x and y"));
  }
  SkeletonRigJoint joint;
  try {
    joint.x = json.at(0).get<double>();
    joint.y = json.at(1).get<double>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is invalid: ", error.what()));
  }
  if (!std::isfinite(joint.x) || !std::isfinite(joint.y)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be finite"));
  }
  return joint;
}

absl::StatusOr<SkeletonRigPoint> ParsePoint(const nlohmann::json& json, size_t index) {
  const std::string context = absl::StrCat("skeleton rig point ", index);
  RETURN_IF_ERROR(RequireExactObject(json, {"chain", "name"}, context));
  SkeletonRigPoint point;
  ASSIGN_OR_RETURN(point.chain, Required<std::string>(json, "chain", context));
  ASSIGN_OR_RETURN(point.name, Required<std::string>(json, "name", context));
  if (point.chain.empty() || point.name.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " needs non-empty names"));
  }
  return point;
}

absl::StatusOr<SkeletonRigBone> ParseBone(const nlohmann::json& json, size_t index) {
  const std::string context = absl::StrCat("skeleton rig bone ", index);
  RETURN_IF_ERROR(RequireExactObject(json, {"end", "start"}, context));
  SkeletonRigBone bone;
  ASSIGN_OR_RETURN(bone.end, Required<std::string>(json, "end", context));
  ASSIGN_OR_RETURN(bone.start, Required<std::string>(json, "start", context));
  if (bone.start.empty() || bone.end.empty() || bone.start == bone.end) {
    return absl::InvalidArgumentError(absl::StrCat(context, " needs two distinct points"));
  }
  return bone;
}

absl::StatusOr<SkeletonRigFrame> ParseFrame(const nlohmann::json& json, size_t index,
                                            std::string_view clip_id) {
  const std::string context = absl::StrCat("skeleton rig clip '", clip_id, "' frame ", index);
  RETURN_IF_ERROR(RequireExactObject(json, {"label", "pose", "underlay"}, context));
  SkeletonRigFrame frame;
  ASSIGN_OR_RETURN(frame.label, Required<std::string>(json, "label", context));
  ASSIGN_OR_RETURN(frame.underlay, Required<std::string>(json, "underlay", context));
  ASSIGN_OR_RETURN(const nlohmann::json pose, Required<nlohmann::json>(json, "pose", context));
  if (frame.label.empty() || !pose.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " needs a label and pose object"));
  }
  for (const auto& [name, encoded_joint] : pose.items()) {
    ASSIGN_OR_RETURN(SkeletonRigJoint joint,
                     ParseJoint(encoded_joint, absl::StrCat(context, " point '", name, "'")));
    if (!frame.pose.emplace(name, joint).second) {
      return absl::InvalidArgumentError(absl::StrCat(context, " repeats point '", name, "'"));
    }
  }
  return frame;
}

absl::StatusOr<SkeletonRigClip> ParseClip(const nlohmann::json& json, std::string id) {
  const std::string context = absl::StrCat("skeleton rig clip '", id, "'");
  RETURN_IF_ERROR(RequireExactObject(json, {"fps", "frames", "name"}, context));
  SkeletonRigClip clip;
  clip.id = std::move(id);
  ASSIGN_OR_RETURN(clip.fps, Required<int>(json, "fps", context));
  ASSIGN_OR_RETURN(clip.name, Required<std::string>(json, "name", context));
  ASSIGN_OR_RETURN(const nlohmann::json frames, Required<nlohmann::json>(json, "frames", context));
  if (clip.fps <= 0 || clip.name.empty() || !frames.is_array() || frames.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " needs a positive fps, name, and frames"));
  }
  clip.frames.reserve(frames.size());
  for (size_t index = 0; index < frames.size(); ++index) {
    ASSIGN_OR_RETURN(SkeletonRigFrame frame, ParseFrame(frames.at(index), index, clip.id));
    clip.frames.push_back(std::move(frame));
  }
  return clip;
}

absl::Status ValidateTopology(const SkeletonRig& rig,
                              const absl::flat_hash_set<std::string>& point_names) {
  absl::flat_hash_set<std::string> edges;
  absl::flat_hash_map<std::string, int> indegree;
  absl::flat_hash_map<std::string, std::vector<std::string>> children;
  for (const std::string& name : point_names) indegree.emplace(name, 0);
  for (const SkeletonRigBone& bone : rig.bones) {
    if (!point_names.contains(bone.start) || !point_names.contains(bone.end)) {
      return absl::InvalidArgumentError(absl::StrCat("skeleton rig bone ", bone.start, " -> ",
                                                     bone.end, " references an unknown point"));
    }
    const std::string edge = absl::StrCat(bone.start, "\n", bone.end);
    if (!edges.insert(edge).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("skeleton rig repeats bone ", bone.start, " -> ", bone.end));
    }
    ++indegree.at(bone.end);
    children[bone.start].push_back(bone.end);
  }

  std::vector<std::string> ready;
  for (const auto& [name, degree] : indegree) {
    if (degree == 0) ready.push_back(name);
  }
  size_t visited = 0;
  while (!ready.empty()) {
    const std::string name = std::move(ready.back());
    ready.pop_back();
    ++visited;
    for (const std::string& child : children[name]) {
      --indegree.at(child);
      if (indegree.at(child) == 0) ready.push_back(child);
    }
  }
  if (visited != point_names.size()) {
    return absl::InvalidArgumentError("skeleton rig bones contain a cycle");
  }
  return absl::OkStatus();
}

absl::Status ValidatePoses(const SkeletonRig& rig,
                           const absl::flat_hash_set<std::string>& point_names) {
  absl::flat_hash_set<std::string> clip_ids;
  for (const SkeletonRigClip& clip : rig.clips) {
    if (!clip_ids.insert(clip.id).second) {
      return absl::InvalidArgumentError(absl::StrCat("skeleton rig repeats clip '", clip.id, "'"));
    }
    absl::flat_hash_set<std::string> labels;
    for (const SkeletonRigFrame& frame : clip.frames) {
      if (!labels.insert(frame.label).second) {
        return absl::InvalidArgumentError(absl::StrCat(
            "skeleton rig clip '", clip.id, "' repeats frame label '", frame.label, "'"));
      }
      if (frame.pose.size() != point_names.size()) {
        return absl::InvalidArgumentError(absl::StrCat("skeleton rig clip '", clip.id, "' frame '",
                                                       frame.label,
                                                       "' does not contain every point"));
      }
      for (const auto& [name, unused_joint] : frame.pose) {
        static_cast<void>(unused_joint);
        if (!point_names.contains(name)) {
          return absl::InvalidArgumentError(absl::StrCat("skeleton rig clip '", clip.id,
                                                         "' frame '", frame.label,
                                                         "' contains unknown point '", name, "'"));
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateRig(const SkeletonRig& rig) {
  if (rig.version != kSupportedVersion) {
    return absl::InvalidArgumentError(
        absl::StrCat("unsupported skeleton rig version ", rig.version));
  }
  if (!std::isfinite(rig.floor_y) || rig.points.empty() || rig.bones.empty() || rig.clips.empty()) {
    return absl::InvalidArgumentError(
        "skeleton rig needs a finite floor, points, bones, and clips");
  }
  absl::flat_hash_set<std::string> point_names;
  for (const SkeletonRigPoint& point : rig.points) {
    if (!point_names.insert(point.name).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("skeleton rig repeats point '", point.name, "'"));
    }
  }
  RETURN_IF_ERROR(ValidateTopology(rig, point_names));
  return ValidatePoses(rig, point_names);
}

double Distance(const SkeletonRigJoint& first, const SkeletonRigJoint& second) {
  return std::hypot(second.x - first.x, second.y - first.y);
}

}  // namespace

absl::StatusOr<SkeletonRig> ParseSkeletonRig(std::string_view encoded) {
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(encoded);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError(absl::StrCat("invalid skeleton rig JSON: ", error.what()));
  }
  RETURN_IF_ERROR(RequireExactObject(
      json, {"bones", "clips", "floor_y", "points", "updated_at", "version"}, "skeleton rig"));

  SkeletonRig rig;
  ASSIGN_OR_RETURN(rig.version, Required<int>(json, "version", "skeleton rig"));
  ASSIGN_OR_RETURN(rig.floor_y, Required<double>(json, "floor_y", "skeleton rig"));
  ASSIGN_OR_RETURN(const std::string updated_at,
                   Required<std::string>(json, "updated_at", "skeleton rig"));
  if (updated_at.empty()) {
    return absl::InvalidArgumentError("skeleton rig updated_at must not be empty");
  }

  ASSIGN_OR_RETURN(const nlohmann::json points,
                   Required<nlohmann::json>(json, "points", "skeleton rig"));
  ASSIGN_OR_RETURN(const nlohmann::json bones,
                   Required<nlohmann::json>(json, "bones", "skeleton rig"));
  ASSIGN_OR_RETURN(const nlohmann::json clips,
                   Required<nlohmann::json>(json, "clips", "skeleton rig"));
  if (!points.is_array() || !bones.is_array() || !clips.is_object()) {
    return absl::InvalidArgumentError("skeleton rig points, bones, or clips have invalid types");
  }

  rig.points.reserve(points.size());
  for (size_t index = 0; index < points.size(); ++index) {
    ASSIGN_OR_RETURN(SkeletonRigPoint point, ParsePoint(points.at(index), index));
    rig.points.push_back(std::move(point));
  }
  rig.bones.reserve(bones.size());
  for (size_t index = 0; index < bones.size(); ++index) {
    ASSIGN_OR_RETURN(SkeletonRigBone bone, ParseBone(bones.at(index), index));
    rig.bones.push_back(std::move(bone));
  }
  rig.clips.reserve(clips.size());
  for (const auto& [id, encoded_clip] : clips.items()) {
    ASSIGN_OR_RETURN(SkeletonRigClip clip, ParseClip(encoded_clip, id));
    rig.clips.push_back(std::move(clip));
  }
  RETURN_IF_ERROR(ValidateRig(rig));
  return rig;
}

absl::StatusOr<SkeletonRig> LoadSkeletonRig(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open skeleton rig: ", path.string()));
  }
  std::ostringstream encoded;
  encoded << stream.rdbuf();
  if (stream.bad()) {
    return absl::DataLossError(absl::StrCat("could not read skeleton rig: ", path.string()));
  }
  return ParseSkeletonRig(encoded.str());
}

absl::StatusOr<const SkeletonRigClip*> FindSkeletonRigClip(const SkeletonRig& rig,
                                                           std::string_view clip_id) {
  for (const SkeletonRigClip& clip : rig.clips) {
    if (clip.id == clip_id) return &clip;
  }
  return absl::NotFoundError(absl::StrCat("skeleton rig does not contain clip '", clip_id, "'"));
}

absl::StatusOr<SkeletonRigClipMetrics> MeasureSkeletonRigClip(const SkeletonRig& rig,
                                                              const SkeletonRigClip& clip) {
  if (clip.frames.empty()) {
    return absl::InvalidArgumentError("cannot measure an empty skeleton rig clip");
  }
  SkeletonRigClipMetrics metrics;
  double minimum_hip_y = std::numeric_limits<double>::infinity();
  double maximum_hip_y = -std::numeric_limits<double>::infinity();
  for (const SkeletonRigFrame& frame : clip.frames) {
    const auto hip = frame.pose.find("hip_c");
    const auto left_toe = frame.pose.find("toe_l");
    const auto right_toe = frame.pose.find("toe_r");
    if (hip == frame.pose.end() || left_toe == frame.pose.end() || right_toe == frame.pose.end()) {
      return absl::InvalidArgumentError(
          "cycle metrics require hip_c, toe_l, and toe_r in every frame");
    }
    minimum_hip_y = std::min(minimum_hip_y, hip->second.y);
    maximum_hip_y = std::max(maximum_hip_y, hip->second.y);
    metrics.left_foot_leads |= left_toe->second.x > right_toe->second.x;
    metrics.right_foot_leads |= right_toe->second.x > left_toe->second.x;
  }
  metrics.hip_oscillation = maximum_hip_y - minimum_hip_y;

  for (const SkeletonRigBone& bone : rig.bones) {
    double minimum_length = std::numeric_limits<double>::infinity();
    double maximum_length = 0.0;
    for (const SkeletonRigFrame& frame : clip.frames) {
      const double length = Distance(frame.pose.at(bone.start), frame.pose.at(bone.end));
      minimum_length = std::min(minimum_length, length);
      maximum_length = std::max(maximum_length, length);
    }
    metrics.maximum_bone_length_drift =
        std::max(metrics.maximum_bone_length_drift, maximum_length - minimum_length);
  }
  return metrics;
}

}  // namespace zebes
