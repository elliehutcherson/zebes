#include "artwork/layered_puppet_editor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/layered_puppet.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "layered_puppet_editor_html.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

nlohmann::json PointJson(const ProfileControlPoint& point) {
  return nlohmann::json::array({point.x, point.y});
}

constexpr std::array<std::pair<std::string_view, size_t>, 13> kReferenceJointMapping{{
    {"elbow_r", 0},
    {"elbow_l", 1},
    {"toe_l", 2},
    {"toe_r", 3},
    {"paw_r", 4},
    {"paw_l", 5},
    {"head_top", 6},
    {"hip_c", 7},
    {"knee_l", 8},
    {"knee_r", 9},
    {"neck", 10},
    {"shoulder_r", 11},
    {"shoulder_l", 12},
}};

int ReferenceDeformationJoint(std::string_view name) {
  const auto found = std::find_if(kReferenceJointMapping.begin(), kReferenceJointMapping.end(),
                                  [name](const auto& mapping) { return mapping.first == name; });
  if (found == kReferenceJointMapping.end()) return -1;
  return static_cast<int>(found->second);
}

absl::Status ValidateReferenceRig(const LayeredPuppet& puppet, const SkeletonRig* rig,
                                  const SkeletonRigClip* clip) {
  if (rig == nullptr && clip == nullptr) return absl::OkStatus();
  if (rig == nullptr || clip == nullptr || clip->frames.size() != puppet.poses.size()) {
    return absl::InvalidArgumentError(
        "layered puppet editor reference rig and clip must match every puppet pose");
  }
  for (size_t frame_index = 0; frame_index < clip->frames.size(); ++frame_index) {
    if (clip->frames[frame_index].label != puppet.poses[frame_index].name) {
      return absl::InvalidArgumentError(
          "layered puppet editor reference rig frame order differs from puppet poses");
    }
    for (const SkeletonRigPoint& point : rig->points) {
      const int mapped_joint = ReferenceDeformationJoint(point.name);
      if (mapped_joint >= 0 && static_cast<size_t>(mapped_joint) >= puppet.source_joints.size()) {
        return absl::InvalidArgumentError(
            "layered puppet editor reference joint mapping is outside the deformation rig");
      }
      const auto joint = clip->frames[frame_index].pose.find(point.name);
      if (joint == clip->frames[frame_index].pose.end() || joint->second.x < 0.0 ||
          joint->second.y < 0.0 || joint->second.x >= puppet.width ||
          joint->second.y >= puppet.height) {
        return absl::InvalidArgumentError(
            "layered puppet editor reference joint is missing or outside the canvas");
      }
    }
  }
  return absl::OkStatus();
}

nlohmann::json ReferenceRigData(const SkeletonRig* rig, const SkeletonRigClip* clip) {
  if (rig == nullptr) return nullptr;
  absl::flat_hash_map<std::string, size_t> point_indices;
  nlohmann::json points = nlohmann::json::array();
  for (size_t index = 0; index < rig->points.size(); ++index) {
    point_indices.emplace(rig->points[index].name, index);
    const int deformation_joint = ReferenceDeformationJoint(rig->points[index].name);
    points.push_back({
        {"name", rig->points[index].name},
        {"chain", rig->points[index].chain},
        {"deformation_joint",
         deformation_joint >= 0 ? nlohmann::json(deformation_joint) : nlohmann::json(nullptr)},
    });
  }
  nlohmann::json bones = nlohmann::json::array();
  for (const SkeletonRigBone& bone : rig->bones) {
    bones.push_back({{"start", point_indices.at(bone.start)}, {"end", point_indices.at(bone.end)}});
  }
  nlohmann::json frames = nlohmann::json::array();
  for (const SkeletonRigFrame& frame : clip->frames) {
    nlohmann::json joints = nlohmann::json::array();
    for (const SkeletonRigPoint& point : rig->points) {
      const SkeletonRigJoint& joint = frame.pose.at(point.name);
      joints.push_back({joint.x, joint.y});
    }
    frames.push_back({{"name", frame.label}, {"joints", std::move(joints)}});
  }
  return {
      {"points", std::move(points)},
      {"bones", std::move(bones)},
      {"frames", std::move(frames)},
  };
}

nlohmann::json EditorData(const LayeredPuppet& puppet, const SkeletonRig* reference_rig,
                          const SkeletonRigClip* reference_clip) {
  nlohmann::json joints = nlohmann::json::array();
  for (const ProfileControlPoint& point : puppet.source_joints) joints.push_back(PointJson(point));

  nlohmann::json bones = nlohmann::json::array();
  for (const ProfileControlBone& bone : puppet.bones) {
    bones.push_back({{"start", bone.start_joint}, {"end", bone.end_joint}});
  }

  nlohmann::json parts = nlohmann::json::array();
  for (const LayeredPuppetPart& part : puppet.parts) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const LayeredPuppetMeshVertex& vertex : part.mesh.vertices) {
      vertices.push_back(
          {{"point", PointJson(vertex.source)}, {"first_bone_weight", vertex.first_bone_weight}});
    }
    nlohmann::json triangles = nlohmann::json::array();
    for (const LayeredPuppetMeshTriangle& triangle : part.mesh.triangles) {
      triangles.push_back(triangle.vertices);
    }
    parts.push_back({
        {"name", part.name},
        {"bones", part.bone_indices},
        {"mesh", {{"vertices", std::move(vertices)}, {"triangles", std::move(triangles)}}},
    });
  }

  nlohmann::json poses = nlohmann::json::array();
  for (const LayeredPuppetPose& pose : puppet.poses) {
    nlohmann::json pose_joints = nlohmann::json::array();
    for (const ProfileControlPoint& point : pose.joints) pose_joints.push_back(PointJson(point));
    poses.push_back({
        {"name", pose.name},
        {"joints", std::move(pose_joints)},
        {"draw_order", pose.draw_order},
    });
  }

  return {
      {"width", puppet.width},
      {"height", puppet.height},
      {"source_joints", std::move(joints)},
      {"bones", std::move(bones)},
      {"parts", std::move(parts)},
      {"poses", std::move(poses)},
      {"reference_rig", ReferenceRigData(reference_rig, reference_clip)},
  };
}

constexpr int kPaintMeshSpacing = 4;

nlohmann::json ContractData(const LayeredPuppetEditorStateContract& contract) {
  nlohmann::json counts = nlohmann::json::object();
  for (const auto& [name, count] : contract.part_triangle_counts) counts[name] = count;
  return {
      {"version", 1},
      {"source_rgba_digest", contract.source_rgba_digest},
      {"puppet_contract_digest", contract.puppet_contract_digest},
      {"frame_names", contract.frame_names},
      {"joint_count", contract.joint_count},
      {"part_triangle_counts", std::move(counts)},
  };
}

// Replaces one placeholder in the editor template. A missing placeholder means
// the template and this file have drifted apart, which would otherwise ship a
// page whose script references an undefined name.
absl::Status SubstituteToken(std::string& html, std::string_view token, std::string_view value) {
  const size_t position = html.find(token);
  if (position == std::string::npos) {
    return absl::InternalError(absl::StrCat("layered puppet editor template is missing ", token));
  }
  html.replace(position, token.size(), value);
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<LayeredPuppetEditorStateContract> BuildLayeredPuppetEditorStateContract(
    const LayeredPuppet& puppet, std::string_view source_rgba_digest,
    const SkeletonRig* reference_rig, const SkeletonRigClip* reference_clip) {
  RETURN_IF_ERROR(ValidateLayeredPuppet(puppet));
  RETURN_IF_ERROR(ValidateReferenceRig(puppet, reference_rig, reference_clip));
  if (!IsLowercaseSha256Digest(source_rgba_digest)) {
    return absl::InvalidArgumentError("layered puppet editor source digest is invalid");
  }
  LayeredPuppetEditorStateContract contract{
      .source_rgba_digest = std::string(source_rgba_digest),
      .puppet_contract_digest =
          Sha256Digest(EditorData(puppet, reference_rig, reference_clip).dump()),
      .joint_count = puppet.source_joints.size(),
  };
  contract.frame_names.reserve(puppet.poses.size());
  const size_t columns =
      (static_cast<size_t>(puppet.width) + kPaintMeshSpacing - 1) / kPaintMeshSpacing;
  const size_t rows =
      (static_cast<size_t>(puppet.height) + kPaintMeshSpacing - 1) / kPaintMeshSpacing;
  for (const LayeredPuppetPose& pose : puppet.poses) contract.frame_names.push_back(pose.name);
  for (const LayeredPuppetPart& part : puppet.parts) {
    contract.part_triangle_counts.emplace(part.name,
                                          part.mesh.triangles.empty() ? 0 : columns * rows * 2);
  }
  return contract;
}

absl::StatusOr<std::string> RenderLayeredPuppetEditorHtml(
    const LayeredPuppet& puppet, size_t initial_pose_index,
    const LayeredPuppetEditorStateContract& contract, const SkeletonRig* reference_rig,
    const SkeletonRigClip* reference_clip) {
  ASSIGN_OR_RETURN(const LayeredPuppetEditorStateContract expected,
                   BuildLayeredPuppetEditorStateContract(puppet, contract.source_rgba_digest,
                                                         reference_rig, reference_clip));
  if (contract.puppet_contract_digest != expected.puppet_contract_digest ||
      contract.frame_names != expected.frame_names ||
      contract.joint_count != expected.joint_count ||
      contract.part_triangle_counts != expected.part_triangle_counts) {
    return absl::FailedPreconditionError(
        "layered puppet editor state contract does not match the puppet");
  }
  if (initial_pose_index >= puppet.poses.size()) {
    return absl::InvalidArgumentError("layered puppet editor initial pose is out of range");
  }

  // The page itself is layered_puppet_editor.html, embedded at build time so the
  // tool stays a single binary. Only these three values come from C++.
  std::string html = kLayeredPuppetEditorHtml;
  RETURN_IF_ERROR(SubstituteToken(html, "@ZEBES_PUPPET@",
                                  EditorData(puppet, reference_rig, reference_clip).dump()));
  RETURN_IF_ERROR(SubstituteToken(html, "@ZEBES_STATE_CONTRACT@", ContractData(contract).dump()));
  RETURN_IF_ERROR(SubstituteToken(html, "@ZEBES_INITIAL_POSE@", absl::StrCat(initial_pose_index)));
  return html;
}

}  // namespace zebes
