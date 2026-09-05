#include "artwork/skeleton_rig_review.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr int kSupportedVersion = 2;
constexpr double kJointRadius = 3.0;

absl::Status RequireExactObject(const nlohmann::json& json, std::initializer_list<const char*> keys,
                                std::string_view context) {
  if (!json.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object"));
  }
  std::set<std::string> expected;
  for (const char* key : keys) expected.emplace(key);
  for (const auto& [key, unused_value] : json.items()) {
    static_cast<void>(unused_value);
    if (!expected.contains(key)) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, " contains unknown field '", key, "'"));
    }
  }
  for (const std::string& key : expected) {
    if (!json.contains(key)) {
      return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
    }
  }
  return absl::OkStatus();
}

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key, std::string_view context) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", key, "' is invalid: ", error.what()));
  }
}

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

std::string EscapeHtml(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '&':
        absl::StrAppend(&result, "&amp;");
        break;
      case '<':
        absl::StrAppend(&result, "&lt;");
        break;
      case '>':
        absl::StrAppend(&result, "&gt;");
        break;
      case '"':
        absl::StrAppend(&result, "&quot;");
        break;
      case '\'':
        absl::StrAppend(&result, "&#39;");
        break;
      default:
        result.push_back(character);
        break;
    }
  }
  return result;
}

std::string_view ChainColor(std::string_view chain) {
  if (chain == "head") return "#e85dde";
  if (chain == "spine") return "#ed493d";
  if (chain == "arm_l") return "#f29a24";
  if (chain == "arm_r") return "#3498e8";
  if (chain == "leg_l") return "#41bd6a";
  if (chain == "leg_r") return "#9680e8";
  if (chain == "tail") return "#c8a967";
  return "#b7bec8";
}

absl::flat_hash_map<std::string, std::string> PointChains(const SkeletonRig& rig) {
  absl::flat_hash_map<std::string, std::string> chains;
  chains.reserve(rig.points.size());
  for (const SkeletonRigPoint& point : rig.points) chains.emplace(point.name, point.chain);
  return chains;
}

std::string RenderFrameSvg(const SkeletonRig& rig, const SkeletonRigFrame& frame,
                           const absl::flat_hash_map<std::string, std::string>& chains,
                           int canvas_width, int canvas_height, std::string_view css_class) {
  std::string svg = absl::StrFormat(
      "<svg class=\"%s\" viewBox=\"0 0 %d %d\" role=\"img\" "
      "aria-label=\"%s skeleton\"><rect width=\"100%%\" height=\"100%%\" "
      "fill=\"#f8f7f4\"/><line x1=\"0\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" "
      "stroke=\"#8b9098\" stroke-width=\"1\" stroke-dasharray=\"7 7\"/>",
      css_class, canvas_width, canvas_height, EscapeHtml(frame.label), rig.floor_y, canvas_width,
      rig.floor_y);
  for (const SkeletonRigBone& bone : rig.bones) {
    const SkeletonRigJoint& start = frame.pose.at(bone.start);
    const SkeletonRigJoint& end = frame.pose.at(bone.end);
    absl::StrAppendFormat(&svg,
                          "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                          "stroke=\"%s\" stroke-width=\"4\" stroke-linecap=\"round\"/>",
                          start.x, start.y, end.x, end.y, ChainColor(chains.at(bone.end)));
  }
  for (const SkeletonRigPoint& point : rig.points) {
    const SkeletonRigJoint& joint = frame.pose.at(point.name);
    absl::StrAppendFormat(
        &svg,
        "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.1f\" fill=\"%s\" stroke=\"#17202a\" "
        "stroke-width=\"1\"><title>%s</title></circle>",
        joint.x, joint.y, kJointRadius, ChainColor(point.chain), EscapeHtml(point.name));
  }
  absl::StrAppend(&svg, "</svg>");
  return svg;
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

absl::StatusOr<std::string> RenderSkeletonRigReviewHtml(const SkeletonRig& rig,
                                                        const SkeletonRigClip& clip,
                                                        int canvas_width, int canvas_height) {
  if (canvas_width <= 0 || canvas_height <= 0) {
    return absl::InvalidArgumentError("skeleton review canvas must be positive");
  }
  ASSIGN_OR_RETURN(const SkeletonRigClipMetrics metrics, MeasureSkeletonRigClip(rig, clip));
  const absl::flat_hash_map<std::string, std::string> chains = PointChains(rig);
  std::string html = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Skeleton Rig Review</title>
<style>
:root { color-scheme: light; font-family: ui-sans-serif, system-ui, sans-serif; background: #171a1f; color: #edf0f4; }
* { box-sizing: border-box; }
body { margin: 0; min-width: 320px; }
header { display: flex; flex-wrap: wrap; align-items: center; gap: 16px; padding: 16px 20px; background: #222730; position: sticky; top: 0; z-index: 2; box-shadow: 0 3px 12px #0008; }
h1 { margin: 0; font-size: 19px; }
button { border: 1px solid #667080; border-radius: 5px; background: #303743; color: #fff; padding: 8px 14px; cursor: pointer; }
button:hover { background: #3b4554; }
.metrics { display: flex; flex-wrap: wrap; gap: 8px; font-size: 13px; }
.metric { border-radius: 999px; padding: 5px 9px; background: #343b47; }
.metric.pass { background: #174d34; }
.metric.warn { background: #6d4213; }
main { padding: 20px; }
.hero { display: grid; grid-template-columns: minmax(280px, 520px) minmax(240px, 1fr); gap: 20px; align-items: start; max-width: 1120px; margin: 0 auto 24px; }
.stage { position: relative; aspect-ratio: 1; border-radius: 8px; overflow: hidden; background: #f8f7f4; box-shadow: 0 10px 30px #0007; }
.stage-frame { display: none; width: 100%; height: 100%; }
.stage-frame.active { display: block; }
.details { background: #222730; border: 1px solid #343b47; border-radius: 8px; padding: 16px; }
.details h2 { margin: 0 0 8px; font-size: 17px; }
.legend { display: grid; grid-template-columns: repeat(2, minmax(100px, 1fr)); gap: 8px; margin-top: 18px; }
.swatch { display: inline-block; width: 13px; height: 13px; border-radius: 50%; margin-right: 7px; vertical-align: -1px; }
.grid { display: grid; grid-template-columns: repeat(6, minmax(150px, 1fr)); gap: 12px; max-width: 1600px; margin: 0 auto; }
.card { border: 2px solid transparent; border-radius: 7px; overflow: hidden; background: #222730; cursor: pointer; color: inherit; padding: 0; text-align: left; }
.card.active { border-color: #fff; }
.card svg { display: block; width: 100%; background: #f8f7f4; }
.card span { display: block; padding: 8px 10px; font-size: 12px; }
@media (max-width: 1100px) { .grid { grid-template-columns: repeat(3, minmax(150px, 1fr)); } }
@media (max-width: 720px) { .hero { grid-template-columns: 1fr; } .grid { grid-template-columns: repeat(2, minmax(130px, 1fr)); } }
</style>
</head>
<body>
<header>
<h1>)html";
  absl::StrAppendFormat(&html, "%s — %d-pose skeleton review</h1>", EscapeHtml(clip.name),
                        clip.frames.size());
  absl::StrAppendFormat(&html, "<button id=\"play\" type=\"button\">Play at %d FPS</button>",
                        clip.fps);
  absl::StrAppend(&html, "<div class=\"metrics\">");
  absl::StrAppendFormat(&html, "<span class=\"metric pass\">Hip oscillation %.1f px</span>",
                        metrics.hip_oscillation);
  absl::StrAppendFormat(&html, "<span class=\"metric %s\">Bone drift %.2f px</span>",
                        metrics.maximum_bone_length_drift <= 1.5 ? "pass" : "warn",
                        metrics.maximum_bone_length_drift);
  absl::StrAppendFormat(
      &html, "<span class=\"metric %s\">Lead feet %s</span>",
      metrics.left_foot_leads && metrics.right_foot_leads ? "pass" : "warn",
      metrics.left_foot_leads && metrics.right_foot_leads ? "alternate" : "do not alternate");
  absl::StrAppend(&html, "</div></header><main><section class=\"hero\"><div class=\"stage\">");
  for (size_t index = 0; index < clip.frames.size(); ++index) {
    absl::StrAppend(
        &html, "<div class=\"stage-frame", index == 0 ? " active" : "", "\" data-frame=\"", index,
        "\">", RenderFrameSvg(rig, clip.frames[index], chains, canvas_width, canvas_height, ""),
        "</div>");
  }
  absl::StrAppend(&html,
                  "</div><aside class=\"details\"><h2 id=\"active-label\"></h2>"
                  "<p>Frames follow the uploaded reference left-to-right across the top row, then "
                  "left-to-right across the bottom row. Click a card to inspect it.</p>"
                  "<div class=\"legend\">");
  const std::vector<std::pair<std::string_view, std::string_view>> legend = {
      {"head", "Head / ears"}, {"spine", "Spine"},   {"arm_l", "Near arm"}, {"arm_r", "Far arm"},
      {"leg_l", "Near leg"},   {"leg_r", "Far leg"}, {"tail", "Tail"},
  };
  for (const auto& [chain, label] : legend) {
    absl::StrAppend(&html, "<div><span class=\"swatch\" style=\"background:", ChainColor(chain),
                    "\"></span>", label, "</div>");
  }
  absl::StrAppend(&html, "</div></aside></section><section class=\"grid\">");
  for (size_t index = 0; index < clip.frames.size(); ++index) {
    const SkeletonRigFrame& frame = clip.frames[index];
    absl::StrAppend(&html, "<button class=\"card", index == 0 ? " active" : "",
                    "\" type=\"button\" data-select=\"", index, "\">",
                    RenderFrameSvg(rig, frame, chains, canvas_width, canvas_height, "thumbnail"),
                    "<span>", EscapeHtml(frame.label), "</span></button>");
  }
  nlohmann::json labels = nlohmann::json::array();
  for (const SkeletonRigFrame& frame : clip.frames) labels.push_back(frame.label);
  absl::StrAppendFormat(&html, R"html(</section></main>
<script>
const labels = %s;
const frames = [...document.querySelectorAll('[data-frame]')];
const cards = [...document.querySelectorAll('[data-select]')];
const label = document.querySelector('#active-label');
const play = document.querySelector('#play');
let active = 0;
let timer = null;
function select(index) {
  active = index;
  frames.forEach((frame, i) => frame.classList.toggle('active', i === index));
  cards.forEach((card, i) => card.classList.toggle('active', i === index));
  label.textContent = `Frame ${index + 1} of ${frames.length}: ${labels[index]}`;
}
function stop() {
  if (timer !== null) window.clearInterval(timer);
  timer = null;
  play.textContent = 'Play at %d FPS';
}
cards.forEach((card, index) => card.addEventListener('click', () => { stop(); select(index); }));
play.addEventListener('click', () => {
  if (timer !== null) { stop(); return; }
  play.textContent = 'Pause';
  timer = window.setInterval(() => select((active + 1) %% frames.length), %d);
});
select(0);
</script>
</body>
</html>
)html",
                        labels.dump(), clip.fps, 1000 / clip.fps);
  return html;
}

}  // namespace zebes
