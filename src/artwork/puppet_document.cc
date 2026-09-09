#include "artwork/puppet_document.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "artwork/layered_puppet.h"
#include "artwork/skeleton_rig.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

using puppet_edit::Command;

PuppetDocumentPart* FindPart(PuppetDocument& document, const std::string& name) {
  const auto found =
      std::find_if(document.parts.begin(), document.parts.end(),
                   [&name](const PuppetDocumentPart& part) { return part.name == name; });
  return found == document.parts.end() ? nullptr : &*found;
}

PuppetDocumentFrame* FindFrame(PuppetDocument& document, const std::string& name) {
  const auto found =
      std::find_if(document.frames.begin(), document.frames.end(),
                   [&name](const PuppetDocumentFrame& frame) { return frame.name == name; });
  return found == document.frames.end() ? nullptr : &*found;
}

bool HasBone(const PuppetDocument& document, const std::string& name) {
  return std::any_of(document.bones.begin(), document.bones.end(),
                     [&name](const PuppetDocumentBone& bone) { return bone.name == name; });
}

const PuppetDocumentBone* FindBone(const PuppetDocument& document, const std::string& name) {
  const auto found =
      std::find_if(document.bones.begin(), document.bones.end(),
                   [&name](const PuppetDocumentBone& bone) { return bone.name == name; });
  return found == document.bones.end() ? nullptr : &*found;
}

bool IsFinitePoint(const ProfileControlPoint& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

// Two bones drive one skinned part only when they meet, because the mesh blends
// the pair around their shared joint.
bool BonesShareAJoint(const PuppetDocumentBone& first, const PuppetDocumentBone& second) {
  return first.start_joint == second.start_joint || first.start_joint == second.end_joint ||
         first.end_joint == second.start_joint || first.end_joint == second.end_joint;
}

absl::Status CheckPartBones(const PuppetDocument& document, const std::vector<std::string>& bones) {
  if (bones.empty() || bones.size() > 2) {
    return absl::InvalidArgumentError("a puppet part needs one or two bones");
  }
  for (const std::string& name : bones) {
    if (!HasBone(document, name)) {
      return absl::InvalidArgumentError(absl::StrCat("unknown bone '", name, "'"));
    }
  }
  if (bones.size() == 2 && bones[0] == bones[1]) {
    return absl::InvalidArgumentError("a puppet part cannot list the same bone twice");
  }
  if (bones.size() == 2 &&
      !BonesShareAJoint(*FindBone(document, bones[0]), *FindBone(document, bones[1]))) {
    return absl::InvalidArgumentError(
        absl::StrCat("bones '", bones[0], "' and '", bones[1], "' do not meet at a joint"));
  }
  return absl::OkStatus();
}

absl::Status CheckOutlines(const std::vector<LayeredPuppetPolygon>& outlines) {
  for (const LayeredPuppetPolygon& polygon : outlines) {
    if (polygon.points.size() < 3) {
      return absl::InvalidArgumentError("a puppet outline needs at least three points");
    }
    for (const ProfileControlPoint& point : polygon.points) {
      if (!IsFinitePoint(point)) {
        return absl::InvalidArgumentError("a puppet outline point is not finite");
      }
    }
  }
  return absl::OkStatus();
}

// Ownership is settled in document order, so a part can only give up pixels to
// one that has already claimed them.
absl::Status CheckExcludedParts(const PuppetDocument& document, const std::string& part,
                                const std::vector<std::string>& excluded) {
  const auto owner =
      std::find_if(document.parts.begin(), document.parts.end(),
                   [&part](const PuppetDocumentPart& candidate) { return candidate.name == part; });
  for (const std::string& name : excluded) {
    if (name == part) {
      return absl::InvalidArgumentError(absl::StrCat("part '", part, "' cannot exclude itself"));
    }
    const auto found = std::find_if(
        document.parts.begin(), owner,
        [&name](const PuppetDocumentPart& candidate) { return candidate.name == name; });
    if (found == owner) {
      return absl::InvalidArgumentError(
          absl::StrCat("part '", part, "' excludes '", name, "', which is not declared before it"));
    }
  }
  return absl::OkStatus();
}

absl::Status CheckFills(const std::vector<PuppetDocumentFill>& fills) {
  for (const PuppetDocumentFill& fill : fills) {
    if (fill.polygon.points.size() < 3) {
      return absl::InvalidArgumentError("a puppet fill needs at least three points");
    }
    for (const ProfileControlPoint& point : fill.polygon.points) {
      if (!IsFinitePoint(point)) {
        return absl::InvalidArgumentError("a puppet fill point is not finite");
      }
    }
  }
  return absl::OkStatus();
}

std::vector<std::string> PartNames(const PuppetDocument& document) {
  std::vector<std::string> names;
  names.reserve(document.parts.size());
  for (const PuppetDocumentPart& part : document.parts) names.push_back(part.name);
  return names;
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetSourceImage& command) {
  if (command.path.empty()) {
    return absl::InvalidArgumentError("a puppet source image needs a path");
  }
  if (command.width <= 0 || command.height <= 0) {
    return absl::InvalidArgumentError("a puppet source image needs positive dimensions");
  }
  document.source_image = command.path;
  document.width = command.width;
  document.height = command.height;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::ScaleToSize& command) {
  if (command.width <= 0 || command.height <= 0) {
    return absl::InvalidArgumentError("a puppet canvas needs positive dimensions");
  }
  if (document.width <= 0 || document.height <= 0) {
    return absl::FailedPreconditionError("choose a source image before stretching the puppet");
  }
  const double scale_x = static_cast<double>(command.width) / document.width;
  const double scale_y = static_cast<double>(command.height) / document.height;
  const auto scaled = [scale_x, scale_y](const ProfileControlPoint& point) {
    return ProfileControlPoint{.x = point.x * scale_x, .y = point.y * scale_y};
  };

  for (auto& [name, rest] : document.rest_pose) rest = scaled(rest);
  for (PuppetDocumentFrame& frame : document.frames) {
    for (auto& [name, point] : frame.pose) point = scaled(point);
  }
  for (PuppetDocumentPart& part : document.parts) {
    for (LayeredPuppetPolygon& polygon : part.outlines) {
      for (ProfileControlPoint& point : polygon.points) point = scaled(point);
    }
    for (LayeredPuppetPolygon& polygon : part.exclude_outlines) {
      for (ProfileControlPoint& point : polygon.points) point = scaled(point);
    }
    for (PuppetDocumentFill& fill : part.fills) {
      for (ProfileControlPoint& point : fill.polygon.points) point = scaled(point);
    }
    // Mesh spacing and blend radius are distances in the same pixels, so they
    // ride the stretch too and the deformation keeps the shape it had. A
    // non-square stretch has no one ratio, so they take the geometric mean.
    // joint_blend_lateral_scale multiplies a distance rather than being one, so
    // it stays as it is.
    const double mesh_scale = std::sqrt(scale_x * scale_y);
    part.mesh_spacing = std::max(1, static_cast<int>(std::lround(part.mesh_spacing * mesh_scale)));
    part.joint_blend_radius *= mesh_scale;
  }
  document.width = command.width;
  document.height = command.height;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetGuideImage& command) {
  document.guide_image = command.path;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::ImportSkeleton& command) {
  if (!document.parts.empty()) {
    return absl::FailedPreconditionError(
        "remove every part before importing a skeleton; parts name bones this would replace");
  }
  ASSIGN_OR_RETURN(const SkeletonRigClip* const clip,
                   FindSkeletonRigClip(command.rig, command.clip_id));
  if (clip->frames.empty()) {
    return absl::InvalidArgumentError(absl::StrCat("clip '", command.clip_id, "' has no frames"));
  }

  PuppetPose rest;
  PuppetJointChains chains;
  for (const SkeletonRigPoint& point : command.rig.points) {
    const auto joint = clip->frames.front().pose.find(point.name);
    if (joint == clip->frames.front().pose.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("clip '", command.clip_id, "' does not pose joint '", point.name, "'"));
    }
    rest.emplace(point.name, ProfileControlPoint{.x = joint->second.x, .y = joint->second.y});
    chains.emplace(point.name, point.chain);
  }

  std::vector<PuppetDocumentBone> bones;
  bones.reserve(command.rig.bones.size());
  for (const SkeletonRigBone& bone : command.rig.bones) {
    bones.push_back({.name = absl::StrCat(bone.start, "-", bone.end),
                     .start_joint = bone.start,
                     .end_joint = bone.end});
  }

  std::vector<PuppetDocumentFrame> frames;
  if (command.import_frames) {
    frames.reserve(clip->frames.size());
    for (const SkeletonRigFrame& frame : clip->frames) {
      PuppetDocumentFrame imported{.name = frame.label};
      for (const auto& [name, point] : rest) {
        const auto joint = frame.pose.find(name);
        if (joint == frame.pose.end()) {
          return absl::InvalidArgumentError(
              absl::StrCat("frame '", frame.label, "' does not pose joint '", name, "'"));
        }
        imported.pose.emplace(name,
                              ProfileControlPoint{.x = joint->second.x, .y = joint->second.y});
      }
      frames.push_back(std::move(imported));
    }
  } else {
    // Existing frames keep their names and draw orders but must carry exactly
    // the imported joint set, so each one is re-posed from the new rest pose.
    frames = document.frames;
    for (PuppetDocumentFrame& frame : frames) frame.pose = rest;
  }

  document.rest_pose = std::move(rest);
  document.joint_chains = std::move(chains);
  document.bones = std::move(bones);
  document.frames = std::move(frames);
  if (clip->fps > 0) document.fps = clip->fps;
  document.skeleton_source = {.rig_path = command.rig_path, .clip_id = command.clip_id};
  if (document.frames.empty()) {
    document.anchor_frame.clear();
  } else if (FindFrame(document, document.anchor_frame) == nullptr) {
    document.anchor_frame = document.frames.front().name;
  }
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::AddJoint& command) {
  if (command.name.empty()) return absl::InvalidArgumentError("a joint needs a name");
  if (command.chain.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("joint '", command.name, "' needs a chain, such as 'arm_l' or 'spine'"));
  }
  if (!IsFinitePoint(command.rest)) {
    return absl::InvalidArgumentError("a joint rest position must be finite");
  }
  if (document.rest_pose.contains(command.name)) {
    return absl::AlreadyExistsError(absl::StrCat("joint '", command.name, "' already exists"));
  }
  document.rest_pose.emplace(command.name, command.rest);
  document.joint_chains.emplace(command.name, command.chain);
  for (PuppetDocumentFrame& frame : document.frames) frame.pose.emplace(command.name, command.rest);
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetJointChain& command) {
  const auto chain = document.joint_chains.find(command.name);
  if (chain == document.joint_chains.end()) {
    return absl::NotFoundError(absl::StrCat("unknown joint '", command.name, "'"));
  }
  if (command.chain.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("joint '", command.name, "' needs a chain, such as 'arm_l' or 'spine'"));
  }
  chain->second = command.chain;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetAllowOverlap& command) {
  document.allow_overlap = command.allowed;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetBoneStretch& command) {
  const auto bone = std::find_if(
      document.bones.begin(), document.bones.end(),
      [&command](const PuppetDocumentBone& candidate) { return candidate.name == command.name; });
  if (bone == document.bones.end()) {
    return absl::NotFoundError(absl::StrCat("unknown bone '", command.name, "'"));
  }
  bone->may_stretch = command.may_stretch;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetFrameRate& command) {
  if (command.fps <= 0 || command.fps > 240) {
    return absl::InvalidArgumentError("frames per second must be between 1 and 240");
  }
  document.fps = command.fps;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RemoveJoint& command) {
  if (!document.rest_pose.contains(command.name)) {
    return absl::NotFoundError(absl::StrCat("unknown joint '", command.name, "'"));
  }
  for (const PuppetDocumentBone& bone : document.bones) {
    if (bone.start_joint == command.name || bone.end_joint == command.name) {
      return absl::FailedPreconditionError(
          absl::StrCat("bone '", bone.name, "' still uses joint '", command.name, "'"));
    }
  }
  document.rest_pose.erase(command.name);
  document.joint_chains.erase(command.name);
  for (PuppetDocumentFrame& frame : document.frames) frame.pose.erase(command.name);
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::MoveRestJoint& command) {
  const auto joint = document.rest_pose.find(command.name);
  if (joint == document.rest_pose.end()) {
    return absl::NotFoundError(absl::StrCat("unknown joint '", command.name, "'"));
  }
  if (!IsFinitePoint(command.rest)) {
    return absl::InvalidArgumentError("a joint rest position must be finite");
  }
  joint->second = command.rest;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::AddBone& command) {
  if (command.name.empty()) return absl::InvalidArgumentError("a bone needs a name");
  if (HasBone(document, command.name)) {
    return absl::AlreadyExistsError(absl::StrCat("bone '", command.name, "' already exists"));
  }
  if (!document.rest_pose.contains(command.start_joint) ||
      !document.rest_pose.contains(command.end_joint)) {
    return absl::InvalidArgumentError(
        absl::StrCat("bone '", command.name, "' references an unknown joint"));
  }
  if (command.start_joint == command.end_joint) {
    return absl::InvalidArgumentError(
        absl::StrCat("bone '", command.name, "' starts and ends at the same joint"));
  }
  document.bones.push_back(
      {.name = command.name, .start_joint = command.start_joint, .end_joint = command.end_joint});
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RemoveBone& command) {
  if (!HasBone(document, command.name)) {
    return absl::NotFoundError(absl::StrCat("unknown bone '", command.name, "'"));
  }
  for (const PuppetDocumentPart& part : document.parts) {
    if (std::find(part.bones.begin(), part.bones.end(), command.name) != part.bones.end()) {
      return absl::FailedPreconditionError(
          absl::StrCat("part '", part.name, "' still uses bone '", command.name, "'"));
    }
  }
  std::erase_if(document.bones,
                [&command](const PuppetDocumentBone& bone) { return bone.name == command.name; });
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::AddFrames& command) {
  if (command.name_prefix.empty()) {
    return absl::InvalidArgumentError("new frames need a name prefix");
  }
  if (command.count == 0) return absl::InvalidArgumentError("add at least one frame");

  const PuppetDocumentFrame* source = nullptr;
  if (!command.copy_from.empty()) {
    source = FindFrame(document, command.copy_from);
    if (source == nullptr) {
      return absl::NotFoundError(absl::StrCat("unknown frame '", command.copy_from, "'"));
    }
  }
  const PuppetPose pose = source == nullptr ? document.rest_pose : source->pose;
  const std::vector<std::string> draw_order =
      source == nullptr ? PartNames(document) : source->draw_order;

  size_t next = 1;
  for (size_t added = 0; added < command.count; ++added) {
    std::string name;
    do {
      name = absl::StrFormat("%s_%02d", command.name_prefix, next);
      ++next;
    } while (FindFrame(document, name) != nullptr);
    document.frames.push_back({.name = name, .pose = pose, .draw_order = draw_order});
  }
  if (document.anchor_frame.empty()) document.anchor_frame = document.frames.front().name;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RemoveFrame& command) {
  if (FindFrame(document, command.name) == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.name, "'"));
  }
  std::erase_if(document.frames, [&command](const PuppetDocumentFrame& frame) {
    return frame.name == command.name;
  });
  if (document.anchor_frame != command.name) return absl::OkStatus();
  document.anchor_frame = document.frames.empty() ? "" : document.frames.front().name;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::ReorderFrames& command) {
  std::vector<std::string> wanted = command.order;
  std::vector<std::string> present;
  present.reserve(document.frames.size());
  for (const PuppetDocumentFrame& frame : document.frames) present.push_back(frame.name);
  std::vector<std::string> sorted = wanted;
  std::sort(sorted.begin(), sorted.end());
  std::sort(present.begin(), present.end());
  if (sorted != present) {
    return absl::InvalidArgumentError("a frame order must name every frame exactly once");
  }

  std::vector<PuppetDocumentFrame> reordered;
  reordered.reserve(wanted.size());
  for (const std::string& name : wanted) {
    reordered.push_back(*FindFrame(document, name));
  }
  document.frames = std::move(reordered);
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RenameFrame& command) {
  PuppetDocumentFrame* const frame = FindFrame(document, command.name);
  if (frame == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.name, "'"));
  }
  if (command.new_name.empty()) {
    return absl::InvalidArgumentError("a frame needs a name");
  }
  if (command.new_name != command.name && FindFrame(document, command.new_name) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat("frame '", command.new_name, "' already exists"));
  }
  frame->name = command.new_name;
  if (document.anchor_frame == command.name) document.anchor_frame = command.new_name;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RebaseFrames& command) {
  const PuppetDocumentFrame* const from = FindFrame(document, command.from_frame);
  if (from == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.from_frame, "'"));
  }
  if (document.rest_pose.empty()) return absl::OkStatus();

  // The slide is measured between the two poses' centres rather than from one
  // joint, so no single joint's placement decides where the whole clip lands.
  ProfileControlPoint rest_centre{};
  ProfileControlPoint frame_centre{};
  for (const auto& [name, rest] : document.rest_pose) {
    const auto posed = from->pose.find(name);
    if (posed == from->pose.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("frame '", command.from_frame, "' does not pose joint '", name, "'"));
    }
    rest_centre = {.x = rest_centre.x + rest.x, .y = rest_centre.y + rest.y};
    frame_centre = {.x = frame_centre.x + posed->second.x, .y = frame_centre.y + posed->second.y};
  }
  const double count = static_cast<double>(document.rest_pose.size());
  const ProfileControlPoint slide{.x = (rest_centre.x - frame_centre.x) / count,
                                  .y = (rest_centre.y - frame_centre.y) / count};

  for (PuppetDocumentFrame& frame : document.frames) {
    for (auto& [name, point] : frame.pose) {
      point = {.x = point.x + slide.x, .y = point.y + slide.y};
    }
  }
  return absl::OkStatus();
}

// The bone graph read as a tree: each joint's single parent, and the children
// hanging off it. A joint that ends two bones has no single parent and is left
// out, so it is treated as a root that nothing corrects.
struct SkeletonTree {
  absl::flat_hash_map<std::string, std::string> parents;
  absl::flat_hash_map<std::string, std::vector<std::string>> children;
};

SkeletonTree ReadSkeletonTree(const PuppetDocument& document) {
  SkeletonTree tree;
  absl::flat_hash_set<std::string> ambiguous;
  for (const PuppetDocumentBone& bone : document.bones) {
    if (!tree.parents.emplace(bone.end_joint, bone.start_joint).second) {
      ambiguous.insert(bone.end_joint);
    }
  }
  for (const std::string& joint : ambiguous) tree.parents.erase(joint);
  for (const auto& [child, parent] : tree.parents) tree.children[parent].push_back(child);
  return tree;
}

absl::StatusOr<PuppetPose> RetargetPose(
    const PuppetDocument& document, const PuppetPose& reference, const PuppetPose& original,
    const absl::flat_hash_map<std::string, const PuppetDocumentBone*>& incoming,
    const SkeletonTree& tree) {
  PuppetPose posed;
  std::vector<std::string> pending;
  for (const auto& [name, rest] : document.rest_pose) {
    if (incoming.contains(name)) continue;
    const ProfileControlPoint& point = original.at(name);
    const ProfileControlPoint& anchor = reference.at(name);
    posed.emplace(name, ProfileControlPoint{.x = rest.x + point.x - anchor.x,
                                            .y = rest.y + point.y - anchor.y});
    pending.push_back(name);
  }
  while (!pending.empty()) {
    const std::string parent = pending.back();
    pending.pop_back();
    const auto children = tree.children.find(parent);
    if (children == tree.children.end()) continue;
    for (const std::string& child : children->second) {
      const ProfileControlPoint rest{
          .x = document.rest_pose.at(child).x - document.rest_pose.at(parent).x,
          .y = document.rest_pose.at(child).y - document.rest_pose.at(parent).y};
      const ProfileControlPoint anchor{.x = reference.at(child).x - reference.at(parent).x,
                                       .y = reference.at(child).y - reference.at(parent).y};
      const ProfileControlPoint motion{.x = original.at(child).x - original.at(parent).x,
                                       .y = original.at(child).y - original.at(parent).y};
      const double rest_length = std::hypot(rest.x, rest.y);
      const double anchor_length = std::hypot(anchor.x, anchor.y);
      const double motion_length = std::hypot(motion.x, motion.y);
      if (rest_length <= 1e-6 || anchor_length <= 1e-6 || motion_length <= 1e-6) {
        return absl::InvalidArgumentError(
            absl::StrCat("cannot retarget collapsed bone '", incoming.at(child)->name, "'"));
      }
      const double angle = std::atan2(rest.y, rest.x) - std::atan2(anchor.y, anchor.x);
      const double scale =
          rest_length / (incoming.at(child)->may_stretch ? anchor_length : motion_length);
      const ProfileControlPoint point{
          .x = posed.at(parent).x +
               scale * (motion.x * std::cos(angle) - motion.y * std::sin(angle)),
          .y = posed.at(parent).y +
               scale * (motion.x * std::sin(angle) + motion.y * std::cos(angle))};
      posed.emplace(child, point);
      pending.push_back(child);
    }
  }
  if (posed.size() != document.rest_pose.size()) {
    return absl::InvalidArgumentError("retargeting requires an acyclic skeleton");
  }
  return posed;
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RetargetFrames& command) {
  const PuppetDocumentFrame* const from = FindFrame(document, command.from_frame);
  if (from == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.from_frame, "'"));
  }
  absl::flat_hash_map<std::string, const PuppetDocumentBone*> incoming;
  for (const PuppetDocumentBone& bone : document.bones) {
    if (!incoming.emplace(bone.end_joint, &bone).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("retargeting needs one parent for joint '", bone.end_joint, "'"));
    }
  }
  const SkeletonTree tree = ReadSkeletonTree(document);
  const PuppetPose reference = from->pose;
  for (PuppetDocumentFrame& frame : document.frames) {
    ASSIGN_OR_RETURN(PuppetPose posed,
                     RetargetPose(document, reference, frame.pose, incoming, tree));
    frame.pose = frame.name == command.from_frame ? document.rest_pose : std::move(posed);
  }
  document.anchor_frame = command.from_frame;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::FitBoneLengths& command) {
  PuppetDocumentFrame* const frame = FindFrame(document, command.frame);
  if (frame == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.frame, "'"));
  }

  const SkeletonTree tree = ReadSkeletonTree(document);
  const PuppetPose original = frame->pose;
  // Joints are corrected outward from the roots, so a joint is only moved once
  // its parent has already landed and the direction is measured from the frame
  // rather than from a half-corrected pose.
  std::vector<std::string> pending;
  absl::flat_hash_set<std::string> seen;
  for (const auto& [name, point] : frame->pose) {
    if (!tree.parents.contains(name)) {
      pending.push_back(name);
      seen.insert(name);
    }
  }
  while (!pending.empty()) {
    const std::string joint = pending.back();
    pending.pop_back();
    for (const std::string& child :
         tree.children.contains(joint) ? tree.children.at(joint) : std::vector<std::string>{}) {
      if (!seen.insert(child).second) continue;
      pending.push_back(child);

      const ProfileControlPoint& rest_parent = document.rest_pose.at(joint);
      const ProfileControlPoint& rest_child = document.rest_pose.at(child);
      const double length = std::hypot(rest_child.x - rest_parent.x, rest_child.y - rest_parent.y);
      const ProfileControlPoint& was_parent = original.at(joint);
      const ProfileControlPoint& was_child = original.at(child);
      const double reach = std::hypot(was_child.x - was_parent.x, was_child.y - was_parent.y);
      // A bone the frame collapsed to a point has no direction to keep, so it
      // falls back to the way the rest pose points it. Leaving the joint where
      // it was would strand it, because its parent has already moved.
      const double toward_x =
          reach == 0.0 ? rest_child.x - rest_parent.x : was_child.x - was_parent.x;
      const double toward_y =
          reach == 0.0 ? rest_child.y - rest_parent.y : was_child.y - was_parent.y;
      const double span = reach == 0.0 ? length : reach;
      if (span == 0.0) continue;
      const ProfileControlPoint& parent = frame->pose.at(joint);
      frame->pose.at(child) = {
          .x = parent.x + toward_x / span * length,
          .y = parent.y + toward_y / span * length,
      };
    }
  }
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetAnchorFrame& command) {
  if (!command.name.empty() && FindFrame(document, command.name) == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.name, "'"));
  }
  document.anchor_frame = command.name;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::PoseJoint& command) {
  if (!IsFinitePoint(command.point)) {
    return absl::InvalidArgumentError("a posed joint must be finite");
  }
  PuppetDocumentFrame* const frame = FindFrame(document, command.frame);
  if (frame == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.frame, "'"));
  }
  const auto joint = frame->pose.find(command.joint);
  if (joint == frame->pose.end()) {
    return absl::NotFoundError(absl::StrCat("unknown joint '", command.joint, "'"));
  }
  if (command.scope == PoseJointScope::kFrame) {
    joint->second = command.point;
    return absl::OkStatus();
  }
  const ProfileControlPoint offset{.x = command.point.x - joint->second.x,
                                   .y = command.point.y - joint->second.y};
  for (PuppetDocumentFrame& other : document.frames) {
    ProfileControlPoint& posed = other.pose.at(command.joint);
    posed = {.x = posed.x + offset.x, .y = posed.y + offset.y};
  }
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::AddPart& command) {
  if (!IsSafeLayeredPuppetPartName(command.name)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "part name '", command.name, "' must be letters, digits, underscore, or hyphen"));
  }
  if (FindPart(document, command.name) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat("part '", command.name, "' already exists"));
  }
  RETURN_IF_ERROR(CheckPartBones(document, command.bones));
  document.parts.push_back({.name = command.name, .bones = command.bones});
  for (PuppetDocumentFrame& frame : document.frames) frame.draw_order.push_back(command.name);
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RemovePart& command) {
  if (FindPart(document, command.name) == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.name, "'"));
  }
  std::erase_if(document.parts,
                [&command](const PuppetDocumentPart& part) { return part.name == command.name; });
  for (PuppetDocumentFrame& frame : document.frames) {
    std::erase(frame.draw_order, command.name);
  }
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::RenamePart& command) {
  PuppetDocumentPart* const part = FindPart(document, command.name);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.name, "'"));
  }
  if (!IsSafeLayeredPuppetPartName(command.new_name)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "part name '", command.new_name, "' must be letters, digits, underscore, or hyphen"));
  }
  if (command.new_name != command.name && FindPart(document, command.new_name) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat("part '", command.new_name, "' already exists"));
  }

  part->name = command.new_name;
  for (PuppetDocumentPart& other : document.parts) {
    std::replace(other.exclude_parts.begin(), other.exclude_parts.end(), command.name,
                 command.new_name);
  }
  for (PuppetDocumentFrame& frame : document.frames) {
    std::replace(frame.draw_order.begin(), frame.draw_order.end(), command.name, command.new_name);
  }
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartBones& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  RETURN_IF_ERROR(CheckPartBones(document, command.bones));
  part->bones = command.bones;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartOutline& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  RETURN_IF_ERROR(CheckOutlines(command.outlines));
  part->outlines = command.outlines;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartExcludeOutlines& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  RETURN_IF_ERROR(CheckOutlines(command.outlines));
  part->exclude_outlines = command.outlines;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartExcludeParts& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  RETURN_IF_ERROR(CheckExcludedParts(document, command.part, command.excluded));
  part->exclude_parts = command.excluded;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartFills& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  RETURN_IF_ERROR(CheckFills(command.fills));
  part->fills = command.fills;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetPartMesh& command) {
  PuppetDocumentPart* const part = FindPart(document, command.part);
  if (part == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown part '", command.part, "'"));
  }
  if (command.spacing < 1) {
    return absl::InvalidArgumentError("mesh spacing must be at least one pixel");
  }
  if (!std::isfinite(command.joint_blend_radius) || command.joint_blend_radius < 0.0 ||
      !std::isfinite(command.joint_blend_lateral_scale) ||
      command.joint_blend_lateral_scale < 0.0) {
    return absl::InvalidArgumentError("mesh blend settings must be finite and non-negative");
  }
  part->mesh_spacing = command.spacing;
  part->joint_blend_radius = command.joint_blend_radius;
  part->joint_blend_lateral_scale = command.joint_blend_lateral_scale;
  return absl::OkStatus();
}

absl::Status Apply(PuppetDocument& document, const puppet_edit::SetDrawOrder& command) {
  PuppetDocumentFrame* const frame = FindFrame(document, command.frame);
  if (frame == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown frame '", command.frame, "'"));
  }
  std::vector<std::string> sorted = command.order;
  std::vector<std::string> expected = PartNames(document);
  std::sort(sorted.begin(), sorted.end());
  std::sort(expected.begin(), expected.end());
  if (sorted != expected) {
    return absl::InvalidArgumentError("a draw order must name every part exactly once");
  }
  frame->draw_order = command.order;
  return absl::OkStatus();
}

}  // namespace

absl::Status ApplyPuppetCommand(PuppetDocument& document, const puppet_edit::Command& command) {
  // Every edit lands on a copy that must pass validation before it replaces the
  // document. A handler therefore never has to unwind its own partial work, and
  // a handler that forgets an invariant fails the command instead of leaving a
  // broken document behind.
  PuppetDocument draft = document;
  RETURN_IF_ERROR(std::visit([&draft](const auto& typed) { return Apply(draft, typed); }, command));
  RETURN_IF_ERROR(ValidatePuppetDocument(draft));
  document = std::move(draft);
  return absl::OkStatus();
}

absl::Status ValidatePuppetDocument(const PuppetDocument& document) {
  if (document.width < 0 || document.height < 0) {
    return absl::InvalidArgumentError("puppet document dimensions cannot be negative");
  }
  if (document.fps <= 0 || document.fps > 240) {
    return absl::InvalidArgumentError("puppet document frames per second must be 1 to 240");
  }
  if (document.joint_chains.size() != document.rest_pose.size()) {
    return absl::InvalidArgumentError("every puppet joint needs exactly one chain");
  }
  for (const auto& [name, point] : document.rest_pose) {
    if (name.empty()) return absl::InvalidArgumentError("puppet document has an unnamed joint");
    if (!IsFinitePoint(point)) {
      return absl::InvalidArgumentError(absl::StrCat("joint '", name, "' rest is not finite"));
    }
    const auto chain = document.joint_chains.find(name);
    if (chain == document.joint_chains.end() || chain->second.empty()) {
      return absl::InvalidArgumentError(absl::StrCat("joint '", name, "' has no chain"));
    }
  }

  absl::flat_hash_set<std::string> bone_names;
  for (const PuppetDocumentBone& bone : document.bones) {
    if (!bone_names.insert(bone.name).second) {
      return absl::InvalidArgumentError(absl::StrCat("duplicate bone '", bone.name, "'"));
    }
    if (!document.rest_pose.contains(bone.start_joint) ||
        !document.rest_pose.contains(bone.end_joint)) {
      return absl::InvalidArgumentError(
          absl::StrCat("bone '", bone.name, "' references an unknown joint"));
    }
    if (bone.start_joint == bone.end_joint) {
      return absl::InvalidArgumentError(
          absl::StrCat("bone '", bone.name, "' starts and ends at the same joint"));
    }
  }

  absl::flat_hash_set<std::string> part_names;
  for (const PuppetDocumentPart& part : document.parts) {
    if (!IsSafeLayeredPuppetPartName(part.name)) {
      return absl::InvalidArgumentError(absl::StrCat("unusable part name '", part.name, "'"));
    }
    if (!part_names.insert(part.name).second) {
      return absl::InvalidArgumentError(absl::StrCat("duplicate part '", part.name, "'"));
    }
    RETURN_IF_ERROR(CheckPartBones(document, part.bones));
    RETURN_IF_ERROR(CheckOutlines(part.outlines));
    RETURN_IF_ERROR(CheckOutlines(part.exclude_outlines));
    RETURN_IF_ERROR(CheckExcludedParts(document, part.name, part.exclude_parts));
    RETURN_IF_ERROR(CheckFills(part.fills));
    if (!part.exclude_outlines.empty() && part.outlines.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("part '", part.name, "' carves holes out of an outline it does not have"));
    }
    if (part.mesh_spacing < 1) {
      return absl::InvalidArgumentError(absl::StrCat("part '", part.name, "' has no mesh spacing"));
    }
  }

  std::vector<std::string> expected_order = PartNames(document);
  std::sort(expected_order.begin(), expected_order.end());
  absl::flat_hash_set<std::string> frame_names;
  for (const PuppetDocumentFrame& frame : document.frames) {
    if (frame.name.empty()) {
      return absl::InvalidArgumentError("puppet document has an unnamed frame");
    }
    if (!frame_names.insert(frame.name).second) {
      return absl::InvalidArgumentError(absl::StrCat("duplicate frame '", frame.name, "'"));
    }
    if (frame.pose.size() != document.rest_pose.size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("frame '", frame.name, "' does not pose every joint"));
    }
    for (const auto& [name, point] : frame.pose) {
      if (!document.rest_pose.contains(name)) {
        return absl::InvalidArgumentError(
            absl::StrCat("frame '", frame.name, "' poses unknown joint '", name, "'"));
      }
      if (!IsFinitePoint(point)) {
        return absl::InvalidArgumentError(
            absl::StrCat("frame '", frame.name, "' poses joint '", name, "' off the numbers"));
      }
    }
    std::vector<std::string> order = frame.draw_order;
    std::sort(order.begin(), order.end());
    if (order != expected_order) {
      return absl::InvalidArgumentError(
          absl::StrCat("frame '", frame.name, "' must draw every part exactly once"));
    }
  }

  if (!document.anchor_frame.empty() && !frame_names.contains(document.anchor_frame)) {
    return absl::InvalidArgumentError(
        absl::StrCat("anchor frame '", document.anchor_frame, "' does not exist"));
  }
  return absl::OkStatus();
}

absl::Status PuppetDocumentReadyToBuild(const PuppetDocument& document) {
  RETURN_IF_ERROR(ValidatePuppetDocument(document));
  if (document.source_image.empty() || document.width <= 0 || document.height <= 0) {
    return absl::FailedPreconditionError("choose a source image");
  }
  if (document.rest_pose.empty() || document.bones.empty()) {
    return absl::FailedPreconditionError("import or draw a skeleton");
  }
  if (document.parts.empty()) {
    return absl::FailedPreconditionError("add at least one part");
  }
  for (const PuppetDocumentPart& part : document.parts) {
    // A part with no outline is legal when it is pure underpaint: invented
    // hidden surface that owns no source pixel of its own. With neither, it
    // would draw nothing.
    if (part.outlines.empty() && part.fills.empty()) {
      return absl::FailedPreconditionError(absl::StrCat("outline or fill part '", part.name, "'"));
    }
  }
  if (document.frames.empty()) {
    return absl::FailedPreconditionError("add at least one frame");
  }
  if (document.anchor_frame.empty()) {
    return absl::FailedPreconditionError("choose the frame to open on");
  }
  return absl::OkStatus();
}

}  // namespace zebes
