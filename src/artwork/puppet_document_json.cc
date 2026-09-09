#include "artwork/puppet_document_json.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/layered_puppet.h"
#include "artwork/profile_silhouette.h"
#include "artwork/puppet_document.h"
#include "artwork/skeleton_rig.h"
#include "common/json_schema.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

using json_schema::Required;
using json_schema::RequireExactObject;

constexpr int kDocumentVersion = 1;

absl::StatusOr<ProfileControlPoint> ParsePoint(const nlohmann::json& value,
                                               std::string_view context) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be a two-number point"));
  }
  return ProfileControlPoint{.x = value[0].get<double>(), .y = value[1].get<double>()};
}

nlohmann::json PointJson(const ProfileControlPoint& point) {
  return nlohmann::json::array({point.x, point.y});
}

absl::StatusOr<PuppetPose> ParsePose(const nlohmann::json& value, std::string_view context) {
  if (!value.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object of joints"));
  }
  PuppetPose pose;
  for (const auto& [name, coordinates] : value.items()) {
    ASSIGN_OR_RETURN(const ProfileControlPoint point,
                     ParsePoint(coordinates, absl::StrCat(context, ".", name)));
    pose.emplace(name, point);
  }
  return pose;
}

absl::StatusOr<PuppetJointChains> ParseJointChains(const nlohmann::json& value,
                                                   std::string_view context) {
  if (!value.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object of joints"));
  }
  PuppetJointChains chains;
  for (const auto& [name, chain] : value.items()) {
    if (!chain.is_string()) {
      return absl::InvalidArgumentError(absl::StrCat(context, ".", name, " must be a string"));
    }
    chains.emplace(name, chain.get<std::string>());
  }
  return chains;
}

nlohmann::json PoseJson(const PuppetPose& pose) {
  nlohmann::json encoded = nlohmann::json::object();
  for (const auto& [name, point] : pose) encoded[name] = PointJson(point);
  return encoded;
}

absl::StatusOr<std::vector<LayeredPuppetPolygon>> ParseOutlines(const nlohmann::json& value,
                                                                std::string_view context) {
  if (!value.is_array()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an array of outlines"));
  }
  std::vector<LayeredPuppetPolygon> outlines;
  outlines.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    const nlohmann::json& polygon_json = value[index];
    if (!polygon_json.is_array()) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, "[", index, "] must be an array of points"));
    }
    LayeredPuppetPolygon polygon;
    polygon.points.reserve(polygon_json.size());
    for (size_t point_index = 0; point_index < polygon_json.size(); ++point_index) {
      ASSIGN_OR_RETURN(const ProfileControlPoint point,
                       ParsePoint(polygon_json[point_index],
                                  absl::StrCat(context, "[", index, "][", point_index, "]")));
      polygon.points.push_back(point);
    }
    outlines.push_back(std::move(polygon));
  }
  return outlines;
}

nlohmann::json OutlinesJson(const std::vector<LayeredPuppetPolygon>& outlines) {
  nlohmann::json encoded = nlohmann::json::array();
  for (const LayeredPuppetPolygon& polygon : outlines) {
    nlohmann::json points = nlohmann::json::array();
    for (const ProfileControlPoint& point : polygon.points) points.push_back(PointJson(point));
    encoded.push_back(std::move(points));
  }
  return encoded;
}

absl::StatusOr<PuppetDocumentFill> ParseFill(const nlohmann::json& value,
                                             std::string_view context) {
  RETURN_IF_ERROR(RequireExactObject(value, {"polygon", "color"}, context));
  ASSIGN_OR_RETURN(std::vector<LayeredPuppetPolygon> polygons,
                   ParseOutlines(nlohmann::json::array({value.at("polygon")}),
                                 absl::StrCat(context, ".polygon")));
  const nlohmann::json& color_json = value.at("color");
  if (!color_json.is_array() || color_json.size() != 4) {
    return absl::InvalidArgumentError(absl::StrCat(context, ".color must be four RGBA8 numbers"));
  }
  PuppetDocumentFill fill{.polygon = std::move(polygons.front())};
  for (size_t channel = 0; channel < fill.color.size(); ++channel) {
    if (!color_json[channel].is_number_integer()) {
      return absl::InvalidArgumentError(absl::StrCat(context, ".color must contain integers"));
    }
    const int number = color_json[channel].get<int>();
    if (number < 0 || number > 255) {
      return absl::InvalidArgumentError(absl::StrCat(context, ".color is outside RGBA8"));
    }
    fill.color[channel] = static_cast<uint8_t>(number);
  }
  return fill;
}

absl::StatusOr<std::vector<PuppetDocumentFill>> ParseFills(const nlohmann::json& value,
                                                           std::string_view context) {
  if (!value.is_array()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an array of fills"));
  }
  std::vector<PuppetDocumentFill> fills;
  fills.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    ASSIGN_OR_RETURN(PuppetDocumentFill fill,
                     ParseFill(value[index], absl::StrCat(context, "[", index, "]")));
    fills.push_back(std::move(fill));
  }
  return fills;
}

nlohmann::json FillsJson(const std::vector<PuppetDocumentFill>& fills) {
  nlohmann::json encoded = nlohmann::json::array();
  for (const PuppetDocumentFill& fill : fills) {
    nlohmann::json points = nlohmann::json::array();
    for (const ProfileControlPoint& point : fill.polygon.points) points.push_back(PointJson(point));
    encoded.push_back({{"polygon", std::move(points)}, {"color", fill.color}});
  }
  return encoded;
}

absl::StatusOr<std::vector<std::string>> ParseNames(const nlohmann::json& value,
                                                    std::string_view context) {
  if (!value.is_array()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an array of names"));
  }
  std::vector<std::string> names;
  names.reserve(value.size());
  for (const nlohmann::json& entry : value) {
    if (!entry.is_string()) {
      return absl::InvalidArgumentError(absl::StrCat(context, " must contain names"));
    }
    names.push_back(entry.get<std::string>());
  }
  return names;
}

absl::StatusOr<PuppetDocumentBone> ParseBone(const nlohmann::json& value,
                                             std::string_view context) {
  RETURN_IF_ERROR(
      RequireExactObject(value, {"name", "start_joint", "end_joint", "may_stretch"}, context));
  ASSIGN_OR_RETURN(std::string name, Required<std::string>(value, "name", context));
  ASSIGN_OR_RETURN(std::string start, Required<std::string>(value, "start_joint", context));
  ASSIGN_OR_RETURN(std::string end, Required<std::string>(value, "end_joint", context));
  ASSIGN_OR_RETURN(const bool may_stretch, Required<bool>(value, "may_stretch", context));
  return PuppetDocumentBone{.name = std::move(name),
                            .start_joint = std::move(start),
                            .end_joint = std::move(end),
                            .may_stretch = may_stretch};
}

absl::StatusOr<PuppetDocumentPart> ParsePart(const nlohmann::json& value,
                                             std::string_view context) {
  RETURN_IF_ERROR(
      RequireExactObject(value,
                         {"name", "bones", "outlines", "exclude_outlines", "exclude_parts", "fills",
                          "mesh_spacing", "joint_blend_radius", "joint_blend_lateral_scale"},
                         context));
  ASSIGN_OR_RETURN(std::string name, Required<std::string>(value, "name", context));
  ASSIGN_OR_RETURN(std::vector<std::string> bones,
                   ParseNames(value.at("bones"), absl::StrCat(context, ".bones")));
  ASSIGN_OR_RETURN(std::vector<LayeredPuppetPolygon> outlines,
                   ParseOutlines(value.at("outlines"), absl::StrCat(context, ".outlines")));
  ASSIGN_OR_RETURN(
      std::vector<LayeredPuppetPolygon> exclude_outlines,
      ParseOutlines(value.at("exclude_outlines"), absl::StrCat(context, ".exclude_outlines")));
  ASSIGN_OR_RETURN(std::vector<std::string> exclude_parts,
                   ParseNames(value.at("exclude_parts"), absl::StrCat(context, ".exclude_parts")));
  ASSIGN_OR_RETURN(std::vector<PuppetDocumentFill> fills,
                   ParseFills(value.at("fills"), absl::StrCat(context, ".fills")));
  ASSIGN_OR_RETURN(const int spacing, Required<int>(value, "mesh_spacing", context));
  ASSIGN_OR_RETURN(const double radius, Required<double>(value, "joint_blend_radius", context));
  ASSIGN_OR_RETURN(const double lateral,
                   Required<double>(value, "joint_blend_lateral_scale", context));
  return PuppetDocumentPart{.name = std::move(name),
                            .bones = std::move(bones),
                            .outlines = std::move(outlines),
                            .exclude_outlines = std::move(exclude_outlines),
                            .exclude_parts = std::move(exclude_parts),
                            .fills = std::move(fills),
                            .mesh_spacing = spacing,
                            .joint_blend_radius = radius,
                            .joint_blend_lateral_scale = lateral};
}

absl::StatusOr<PuppetDocumentFrame> ParseFrame(const nlohmann::json& value,
                                               std::string_view context) {
  RETURN_IF_ERROR(RequireExactObject(value, {"name", "pose", "draw_order"}, context));
  ASSIGN_OR_RETURN(std::string name, Required<std::string>(value, "name", context));
  ASSIGN_OR_RETURN(PuppetPose pose, ParsePose(value.at("pose"), absl::StrCat(context, ".pose")));
  ASSIGN_OR_RETURN(std::vector<std::string> draw_order,
                   ParseNames(value.at("draw_order"), absl::StrCat(context, ".draw_order")));
  return PuppetDocumentFrame{
      .name = std::move(name), .pose = std::move(pose), .draw_order = std::move(draw_order)};
}

absl::StatusOr<PuppetDocument> ParseDocument(const nlohmann::json& document) {
  constexpr std::string_view kContext = "puppet document";
  RETURN_IF_ERROR(
      RequireExactObject(document,
                         {"version", "source_image", "guide_image", "width", "height",
                          "require_single_component", "allow_overlap", "fps", "skeleton_source",
                          "anchor_frame", "rest_pose", "joint_chains", "bones", "parts", "frames"},
                         kContext));
  ASSIGN_OR_RETURN(const int version, Required<int>(document, "version", kContext));
  if (version != kDocumentVersion) {
    return absl::InvalidArgumentError(
        absl::StrCat("puppet document version must be ", kDocumentVersion));
  }

  PuppetDocument parsed;
  ASSIGN_OR_RETURN(parsed.source_image, Required<std::string>(document, "source_image", kContext));
  ASSIGN_OR_RETURN(parsed.guide_image, Required<std::string>(document, "guide_image", kContext));
  ASSIGN_OR_RETURN(parsed.require_single_component,
                   Required<bool>(document, "require_single_component", kContext));
  ASSIGN_OR_RETURN(parsed.width, Required<int>(document, "width", kContext));
  ASSIGN_OR_RETURN(parsed.height, Required<int>(document, "height", kContext));
  ASSIGN_OR_RETURN(parsed.fps, Required<int>(document, "fps", kContext));
  ASSIGN_OR_RETURN(parsed.allow_overlap, Required<bool>(document, "allow_overlap", kContext));
  ASSIGN_OR_RETURN(parsed.anchor_frame, Required<std::string>(document, "anchor_frame", kContext));

  constexpr std::string_view kSkeletonContext = "puppet document skeleton_source";
  const nlohmann::json& skeleton = document.at("skeleton_source");
  RETURN_IF_ERROR(RequireExactObject(skeleton, {"rig_path", "clip_id"}, kSkeletonContext));
  ASSIGN_OR_RETURN(parsed.skeleton_source.rig_path,
                   Required<std::string>(skeleton, "rig_path", kSkeletonContext));
  ASSIGN_OR_RETURN(parsed.skeleton_source.clip_id,
                   Required<std::string>(skeleton, "clip_id", kSkeletonContext));

  ASSIGN_OR_RETURN(parsed.rest_pose,
                   ParsePose(document.at("rest_pose"), "puppet document rest_pose"));
  ASSIGN_OR_RETURN(parsed.joint_chains,
                   ParseJointChains(document.at("joint_chains"), "puppet document joint_chains"));
  if (!document.at("bones").is_array() || !document.at("parts").is_array() ||
      !document.at("frames").is_array()) {
    return absl::InvalidArgumentError("puppet document bones, parts, and frames must be arrays");
  }
  for (size_t index = 0; index < document.at("bones").size(); ++index) {
    ASSIGN_OR_RETURN(
        PuppetDocumentBone bone,
        ParseBone(document.at("bones")[index], absl::StrCat("puppet document bones[", index, "]")));
    parsed.bones.push_back(std::move(bone));
  }
  for (size_t index = 0; index < document.at("parts").size(); ++index) {
    ASSIGN_OR_RETURN(
        PuppetDocumentPart part,
        ParsePart(document.at("parts")[index], absl::StrCat("puppet document parts[", index, "]")));
    parsed.parts.push_back(std::move(part));
  }
  for (size_t index = 0; index < document.at("frames").size(); ++index) {
    ASSIGN_OR_RETURN(PuppetDocumentFrame frame,
                     ParseFrame(document.at("frames")[index],
                                absl::StrCat("puppet document frames[", index, "]")));
    parsed.frames.push_back(std::move(frame));
  }
  RETURN_IF_ERROR(ValidatePuppetDocument(parsed));
  return parsed;
}

absl::StatusOr<puppet_edit::Command> ParseCommand(const nlohmann::json& value,
                                                  std::string_view context,
                                                  const std::filesystem::path& rig_base_directory) {
  ASSIGN_OR_RETURN(const std::string name, Required<std::string>(value, "command", context));
  const std::string label = absl::StrCat(context, " '", name, "'");

  if (name == "set_source_image") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "path", "width", "height"}, label));
    ASSIGN_OR_RETURN(std::string path, Required<std::string>(value, "path", label));
    ASSIGN_OR_RETURN(const int width, Required<int>(value, "width", label));
    ASSIGN_OR_RETURN(const int height, Required<int>(value, "height", label));
    return puppet_edit::SetSourceImage{.path = std::move(path), .width = width, .height = height};
  }
  if (name == "scale_to_size") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "width", "height"}, label));
    ASSIGN_OR_RETURN(const int width, Required<int>(value, "width", label));
    ASSIGN_OR_RETURN(const int height, Required<int>(value, "height", label));
    return puppet_edit::ScaleToSize{.width = width, .height = height};
  }
  if (name == "set_guide_image") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "path"}, label));
    ASSIGN_OR_RETURN(std::string path, Required<std::string>(value, "path", label));
    return puppet_edit::SetGuideImage{.path = std::move(path)};
  }
  if (name == "import_skeleton") {
    RETURN_IF_ERROR(
        RequireExactObject(value, {"command", "rig_path", "clip_id", "import_frames"}, label));
    ASSIGN_OR_RETURN(std::string rig_path, Required<std::string>(value, "rig_path", label));
    ASSIGN_OR_RETURN(std::string clip_id, Required<std::string>(value, "clip_id", label));
    ASSIGN_OR_RETURN(const bool import_frames, Required<bool>(value, "import_frames", label));
    ASSIGN_OR_RETURN(SkeletonRig rig, LoadSkeletonRig(rig_base_directory / rig_path));
    return puppet_edit::ImportSkeleton{.rig = std::move(rig),
                                       .rig_path = std::move(rig_path),
                                       .clip_id = std::move(clip_id),
                                       .import_frames = import_frames};
  }
  if (name == "add_joint") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "rest", "chain"}, label));
    ASSIGN_OR_RETURN(std::string joint, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::string chain, Required<std::string>(value, "chain", label));
    ASSIGN_OR_RETURN(const ProfileControlPoint rest,
                     ParsePoint(value.at("rest"), absl::StrCat(label, ".rest")));
    return puppet_edit::AddJoint{.name = std::move(joint), .rest = rest, .chain = std::move(chain)};
  }
  if (name == "move_rest_joint") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "rest"}, label));
    ASSIGN_OR_RETURN(std::string joint, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(const ProfileControlPoint rest,
                     ParsePoint(value.at("rest"), absl::StrCat(label, ".rest")));
    return puppet_edit::MoveRestJoint{.name = std::move(joint), .rest = rest};
  }
  if (name == "remove_joint") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name"}, label));
    ASSIGN_OR_RETURN(std::string joint, Required<std::string>(value, "name", label));
    return puppet_edit::RemoveJoint{.name = std::move(joint)};
  }
  if (name == "set_joint_chain") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "chain"}, label));
    ASSIGN_OR_RETURN(std::string joint, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::string chain, Required<std::string>(value, "chain", label));
    return puppet_edit::SetJointChain{.name = std::move(joint), .chain = std::move(chain)};
  }
  if (name == "set_frame_rate") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "fps"}, label));
    ASSIGN_OR_RETURN(const int fps, Required<int>(value, "fps", label));
    return puppet_edit::SetFrameRate{.fps = fps};
  }
  if (name == "set_allow_overlap") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "allowed"}, label));
    ASSIGN_OR_RETURN(const bool allowed, Required<bool>(value, "allowed", label));
    return puppet_edit::SetAllowOverlap{.allowed = allowed};
  }
  if (name == "set_bone_stretch") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "may_stretch"}, label));
    ASSIGN_OR_RETURN(std::string bone, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(const bool may_stretch, Required<bool>(value, "may_stretch", label));
    return puppet_edit::SetBoneStretch{.name = std::move(bone), .may_stretch = may_stretch};
  }
  if (name == "add_bone") {
    RETURN_IF_ERROR(
        RequireExactObject(value, {"command", "name", "start_joint", "end_joint"}, label));
    ASSIGN_OR_RETURN(std::string bone, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::string start, Required<std::string>(value, "start_joint", label));
    ASSIGN_OR_RETURN(std::string end, Required<std::string>(value, "end_joint", label));
    return puppet_edit::AddBone{
        .name = std::move(bone), .start_joint = std::move(start), .end_joint = std::move(end)};
  }
  if (name == "remove_bone") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name"}, label));
    ASSIGN_OR_RETURN(std::string bone, Required<std::string>(value, "name", label));
    return puppet_edit::RemoveBone{.name = std::move(bone)};
  }
  if (name == "add_frames") {
    RETURN_IF_ERROR(
        RequireExactObject(value, {"command", "name_prefix", "count", "copy_from"}, label));
    ASSIGN_OR_RETURN(std::string prefix, Required<std::string>(value, "name_prefix", label));
    ASSIGN_OR_RETURN(const int count, Required<int>(value, "count", label));
    ASSIGN_OR_RETURN(std::string copy_from, Required<std::string>(value, "copy_from", label));
    if (count < 1) {
      return absl::InvalidArgumentError(absl::StrCat(label, " count must be positive"));
    }
    return puppet_edit::AddFrames{.name_prefix = std::move(prefix),
                                  .count = static_cast<size_t>(count),
                                  .copy_from = std::move(copy_from)};
  }
  if (name == "remove_frame") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "name", label));
    return puppet_edit::RemoveFrame{.name = std::move(frame)};
  }
  if (name == "reorder_frames") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "order"}, label));
    ASSIGN_OR_RETURN(std::vector<std::string> order,
                     ParseNames(value.at("order"), absl::StrCat(label, ".order")));
    return puppet_edit::ReorderFrames{.order = std::move(order)};
  }
  if (name == "rename_frame") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "new_name"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::string renamed, Required<std::string>(value, "new_name", label));
    return puppet_edit::RenameFrame{.name = std::move(frame), .new_name = std::move(renamed)};
  }
  if (name == "rebase_frames") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "from_frame"}, label));
    ASSIGN_OR_RETURN(std::string from, Required<std::string>(value, "from_frame", label));
    return puppet_edit::RebaseFrames{.from_frame = std::move(from)};
  }
  if (name == "fit_bone_lengths") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "frame"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "frame", label));
    return puppet_edit::FitBoneLengths{.frame = std::move(frame)};
  }
  if (name == "retarget_frames") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "from_frame"}, label));
    ASSIGN_OR_RETURN(std::string from, Required<std::string>(value, "from_frame", label));
    return puppet_edit::RetargetFrames{.from_frame = std::move(from)};
  }
  if (name == "set_anchor_frame") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "name", label));
    return puppet_edit::SetAnchorFrame{.name = std::move(frame)};
  }
  if (name == "pose_joint") {
    RETURN_IF_ERROR(
        RequireExactObject(value, {"command", "frame", "joint", "point", "scope"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "frame", label));
    ASSIGN_OR_RETURN(std::string joint, Required<std::string>(value, "joint", label));
    ASSIGN_OR_RETURN(const ProfileControlPoint point,
                     ParsePoint(value.at("point"), absl::StrCat(label, ".point")));
    ASSIGN_OR_RETURN(const std::string scope, Required<std::string>(value, "scope", label));
    if (scope != "frame" && scope != "all_frames") {
      return absl::InvalidArgumentError(
          absl::StrCat(label, " scope must be 'frame' or 'all_frames'"));
    }
    return puppet_edit::PoseJoint{
        .frame = std::move(frame),
        .joint = std::move(joint),
        .point = point,
        .scope = scope == "frame" ? PoseJointScope::kFrame : PoseJointScope::kAllFrames};
  }
  if (name == "add_part") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "bones"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::vector<std::string> bones,
                     ParseNames(value.at("bones"), absl::StrCat(label, ".bones")));
    return puppet_edit::AddPart{.name = std::move(part), .bones = std::move(bones)};
  }
  if (name == "remove_part") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "name", label));
    return puppet_edit::RemovePart{.name = std::move(part)};
  }
  if (name == "rename_part") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "name", "new_name"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "name", label));
    ASSIGN_OR_RETURN(std::string renamed, Required<std::string>(value, "new_name", label));
    return puppet_edit::RenamePart{.name = std::move(part), .new_name = std::move(renamed)};
  }
  if (name == "set_part_bones") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "part", "bones"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(std::vector<std::string> bones,
                     ParseNames(value.at("bones"), absl::StrCat(label, ".bones")));
    return puppet_edit::SetPartBones{.part = std::move(part), .bones = std::move(bones)};
  }
  if (name == "set_part_outline") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "part", "outlines"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(std::vector<LayeredPuppetPolygon> outlines,
                     ParseOutlines(value.at("outlines"), absl::StrCat(label, ".outlines")));
    return puppet_edit::SetPartOutline{.part = std::move(part), .outlines = std::move(outlines)};
  }
  if (name == "set_part_exclude_outlines") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "part", "outlines"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(std::vector<LayeredPuppetPolygon> outlines,
                     ParseOutlines(value.at("outlines"), absl::StrCat(label, ".outlines")));
    return puppet_edit::SetPartExcludeOutlines{.part = std::move(part),
                                               .outlines = std::move(outlines)};
  }
  if (name == "set_part_exclude_parts") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "part", "excluded"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(std::vector<std::string> excluded,
                     ParseNames(value.at("excluded"), absl::StrCat(label, ".excluded")));
    return puppet_edit::SetPartExcludeParts{.part = std::move(part),
                                            .excluded = std::move(excluded)};
  }
  if (name == "set_part_fills") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "part", "fills"}, label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(std::vector<PuppetDocumentFill> fills,
                     ParseFills(value.at("fills"), absl::StrCat(label, ".fills")));
    return puppet_edit::SetPartFills{.part = std::move(part), .fills = std::move(fills)};
  }
  if (name == "set_part_mesh") {
    RETURN_IF_ERROR(RequireExactObject(
        value, {"command", "part", "spacing", "joint_blend_radius", "joint_blend_lateral_scale"},
        label));
    ASSIGN_OR_RETURN(std::string part, Required<std::string>(value, "part", label));
    ASSIGN_OR_RETURN(const int spacing, Required<int>(value, "spacing", label));
    ASSIGN_OR_RETURN(const double radius, Required<double>(value, "joint_blend_radius", label));
    ASSIGN_OR_RETURN(const double lateral,
                     Required<double>(value, "joint_blend_lateral_scale", label));
    return puppet_edit::SetPartMesh{.part = std::move(part),
                                    .spacing = spacing,
                                    .joint_blend_radius = radius,
                                    .joint_blend_lateral_scale = lateral};
  }
  if (name == "set_draw_order") {
    RETURN_IF_ERROR(RequireExactObject(value, {"command", "frame", "order"}, label));
    ASSIGN_OR_RETURN(std::string frame, Required<std::string>(value, "frame", label));
    ASSIGN_OR_RETURN(std::vector<std::string> order,
                     ParseNames(value.at("order"), absl::StrCat(label, ".order")));
    return puppet_edit::SetDrawOrder{.frame = std::move(frame), .order = std::move(order)};
  }
  return absl::InvalidArgumentError(absl::StrCat(context, " names unknown command '", name, "'"));
}

absl::StatusOr<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open ", path.string()));
  }
  std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (!stream.good() && !stream.eof()) {
    return absl::DataLossError(absl::StrCat("could not read ", path.string()));
  }
  return contents;
}

}  // namespace

absl::StatusOr<PuppetDocument> ParsePuppetDocument(std::string_view encoded) {
  try {
    return ParseDocument(nlohmann::json::parse(encoded));
  } catch (const std::exception& error) {
    return absl::DataLossError(absl::StrCat("invalid puppet document: ", error.what()));
  }
}

std::string PuppetDocumentToJson(const PuppetDocument& document) {
  nlohmann::json bones = nlohmann::json::array();
  for (const PuppetDocumentBone& bone : document.bones) {
    bones.push_back({{"name", bone.name},
                     {"start_joint", bone.start_joint},
                     {"end_joint", bone.end_joint},
                     {"may_stretch", bone.may_stretch}});
  }
  nlohmann::json parts = nlohmann::json::array();
  for (const PuppetDocumentPart& part : document.parts) {
    parts.push_back({{"name", part.name},
                     {"bones", part.bones},
                     {"outlines", OutlinesJson(part.outlines)},
                     {"exclude_outlines", OutlinesJson(part.exclude_outlines)},
                     {"exclude_parts", part.exclude_parts},
                     {"fills", FillsJson(part.fills)},
                     {"mesh_spacing", part.mesh_spacing},
                     {"joint_blend_radius", part.joint_blend_radius},
                     {"joint_blend_lateral_scale", part.joint_blend_lateral_scale}});
  }
  nlohmann::json frames = nlohmann::json::array();
  for (const PuppetDocumentFrame& frame : document.frames) {
    frames.push_back(
        {{"name", frame.name}, {"pose", PoseJson(frame.pose)}, {"draw_order", frame.draw_order}});
  }
  const nlohmann::json encoded = {
      {"version", kDocumentVersion},
      {"source_image", document.source_image},
      {"guide_image", document.guide_image},
      {"require_single_component", document.require_single_component},
      {"allow_overlap", document.allow_overlap},
      {"width", document.width},
      {"height", document.height},
      {"fps", document.fps},
      {"skeleton_source",
       {{"rig_path", document.skeleton_source.rig_path},
        {"clip_id", document.skeleton_source.clip_id}}},
      {"anchor_frame", document.anchor_frame},
      {"rest_pose", PoseJson(document.rest_pose)},
      {"joint_chains", document.joint_chains},
      {"bones", std::move(bones)},
      {"parts", std::move(parts)},
      {"frames", std::move(frames)},
  };
  return absl::StrCat(encoded.dump(2), "\n");
}

absl::StatusOr<PuppetDocument> LoadPuppetDocument(const std::filesystem::path& path) {
  ASSIGN_OR_RETURN(const std::string encoded, ReadFile(path));
  return ParsePuppetDocument(encoded);
}

absl::Status SavePuppetDocument(const std::filesystem::path& path, const PuppetDocument& document) {
  RETURN_IF_ERROR(ValidatePuppetDocument(document));
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not create ", path.string()));
  }
  const std::string encoded = PuppetDocumentToJson(document);
  stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write ", path.string()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<puppet_edit::Command>> ParsePuppetCommands(
    std::string_view encoded, const std::filesystem::path& rig_base_directory) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(encoded);
  } catch (const std::exception& error) {
    return absl::DataLossError(absl::StrCat("invalid puppet command list: ", error.what()));
  }
  if (!parsed.is_array()) {
    return absl::InvalidArgumentError("a puppet command list must be an array");
  }
  std::vector<puppet_edit::Command> commands;
  commands.reserve(parsed.size());
  for (size_t index = 0; index < parsed.size(); ++index) {
    ASSIGN_OR_RETURN(
        puppet_edit::Command command,
        ParseCommand(parsed[index], absl::StrCat("command[", index, "]"), rig_base_directory));
    commands.push_back(std::move(command));
  }
  return commands;
}

absl::StatusOr<std::string> PuppetDocumentSpecJson(const PuppetDocument& document) {
  RETURN_IF_ERROR(PuppetDocumentReadyToBuild(document));

  nlohmann::json bones = nlohmann::json::array();
  for (const PuppetDocumentBone& bone : document.bones) {
    bones.push_back({{"name", bone.name},
                     {"start", bone.start_joint},
                     {"end", bone.end_joint},
                     {"may_stretch", bone.may_stretch}});
  }
  nlohmann::json parts = nlohmann::json::array();
  for (const PuppetDocumentPart& part : document.parts) {
    nlohmann::json fills = nlohmann::json::array();
    for (const PuppetDocumentFill& fill : part.fills) {
      nlohmann::json points = nlohmann::json::array();
      for (const ProfileControlPoint& point : fill.polygon.points) {
        points.push_back(PointJson(point));
      }
      fills.push_back({{"points", std::move(points)}, {"color", fill.color}});
    }
    // With overlap off, ownership is settled by declaration order: this part
    // gives up whatever every part before it already claimed, on top of any
    // exclusions authored by hand. That is what makes a shape drawn across an
    // existing part keep only the free side, right up to the other part's edge.
    std::vector<std::string> excluded = part.exclude_parts;
    if (!document.allow_overlap) {
      for (const PuppetDocumentPart& earlier : document.parts) {
        if (earlier.name == part.name) break;
        if (std::find(excluded.begin(), excluded.end(), earlier.name) == excluded.end()) {
          excluded.push_back(earlier.name);
        }
      }
    }
    nlohmann::json encoded = {
        {"name", part.name},
        {"source_polygons", OutlinesJson(part.outlines)},
        {"source_exclude_polygons", OutlinesJson(part.exclude_outlines)},
        {"source_exclude_parts", std::move(excluded)},
        {"fill_polygons", std::move(fills)},
        {"mesh_spacing", part.mesh_spacing},
        {"joint_blend_radius", part.joint_blend_radius},
        {"joint_blend_lateral_scale", part.joint_blend_lateral_scale},
    };
    // The spec spells a rigid part and a skinned part differently, and the
    // builder picks the deformation path from which key is present.
    if (part.bones.size() == 1) {
      encoded["bone"] = part.bones.front();
    } else {
      encoded["bones"] = part.bones;
    }
    parts.push_back(std::move(encoded));
  }
  nlohmann::json pose_order = nlohmann::json::array();
  nlohmann::json poses = nlohmann::json::object();
  nlohmann::json draw_order = nlohmann::json::object();
  for (const PuppetDocumentFrame& frame : document.frames) {
    pose_order.push_back(frame.name);
    poses[frame.name] = PoseJson(frame.pose);
    draw_order[frame.name] = frame.draw_order;
  }
  const nlohmann::json spec = {
      {"version", 1},
      {"width", document.width},
      {"height", document.height},
      {"joints", PoseJson(document.rest_pose)},
      {"bones", std::move(bones)},
      {"parts", std::move(parts)},
      {"pose_order", std::move(pose_order)},
      {"poses", std::move(poses)},
      {"draw_order", std::move(draw_order)},
      {"editor_initial_pose", document.anchor_frame},
      {"require_single_component", document.require_single_component},
  };
  return spec.dump();
}

absl::StatusOr<std::string> PuppetDocumentRigJson(const PuppetDocument& document,
                                                  std::string_view clip_id) {
  RETURN_IF_ERROR(ValidatePuppetDocument(document));
  if (clip_id.empty()) return absl::InvalidArgumentError("a rig clip needs an id");
  if (document.rest_pose.empty() || document.bones.empty()) {
    return absl::FailedPreconditionError("a rig needs at least one joint and one bone");
  }

  nlohmann::json points = nlohmann::json::array();
  double floor_y = 0.0;
  for (const auto& [name, rest] : document.rest_pose) {
    points.push_back({{"name", name}, {"chain", document.joint_chains.at(name)}});
    floor_y = std::max(floor_y, rest.y);
  }
  nlohmann::json bones = nlohmann::json::array();
  for (const PuppetDocumentBone& bone : document.bones) {
    bones.push_back({{"start", bone.start_joint}, {"end", bone.end_joint}});
  }

  const auto frame_json = [](std::string_view label, const PuppetPose& pose) {
    nlohmann::json encoded = nlohmann::json::object();
    for (const auto& [name, point] : pose) encoded[name] = PointJson(point);
    return nlohmann::json{{"label", label}, {"underlay", ""}, {"pose", std::move(encoded)}};
  };
  nlohmann::json frames = nlohmann::json::array();
  if (document.frames.empty()) {
    frames.push_back(frame_json("rest", document.rest_pose));
  } else {
    for (const PuppetDocumentFrame& frame : document.frames) {
      frames.push_back(frame_json(frame.name, frame.pose));
    }
  }

  const nlohmann::json rig = {
      {"version", 2},
      {"floor_y", floor_y},
      {"updated_at", CurrentUtcTimestamp()},
      {"points", std::move(points)},
      {"bones", std::move(bones)},
      {"clips",
       {{std::string(clip_id),
         {{"name", std::string(clip_id)}, {"fps", document.fps}, {"frames", std::move(frames)}}}}},
  };
  const std::string encoded = absl::StrCat(rig.dump(2), "\n");
  // Written through the reader before it reaches disk. A rig this tool cannot
  // load is a rig nothing can load, and it is far cheaper to learn that here
  // than from a file someone saved an hour ago.
  RETURN_IF_ERROR(ParseSkeletonRig(encoded).status());
  return encoded;
}

}  // namespace zebes
