#include "artwork/skeleton_rig_review.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

struct PixelColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

struct ChainStyle {
  std::string_view chain;
  std::string_view html;
  PixelColor raster;
};

constexpr std::array<ChainStyle, 7> kChainStyles{{
    {.chain = "head", .html = "#e66ee6", .raster = {0xE6, 0x6E, 0xE6, 0xFF}},
    {.chain = "spine", .html = "#eb4637", .raster = {0xEB, 0x46, 0x37, 0xFF}},
    {.chain = "arm_l", .html = "#faa532", .raster = {0xFA, 0xA5, 0x32, 0xFF}},
    {.chain = "arm_r", .html = "#3ca5fa", .raster = {0x3C, 0xA5, 0xFA, 0xFF}},
    {.chain = "leg_l", .html = "#46c864", .raster = {0x46, 0xC8, 0x64, 0xFF}},
    {.chain = "leg_r", .html = "#a582f5", .raster = {0xA5, 0x82, 0xF5, 0xFF}},
    {.chain = "tail", .html = "#beaa6e", .raster = {0xBE, 0xAA, 0x6E, 0xFF}},
}};
constexpr PixelColor kUnknownChainColor{0xB7, 0xBE, 0xC8, 0xFF};
constexpr PixelColor kCanvasColor{0xFF, 0xFF, 0xFF, 0xFF};
constexpr PixelColor kJointOutlineColor{0x14, 0x14, 0x14, 0xFF};
constexpr PixelColor kFloorColor{0x78, 0x78, 0x78, 0xFF};
constexpr int64_t kMaximumRasterPixels = 64 * 1024 * 1024;

std::string_view ChainColor(std::string_view chain) {
  for (const ChainStyle& style : kChainStyles) {
    if (style.chain == chain) return style.html;
  }
  return "#b7bec8";
}

PixelColor ChainRasterColor(std::string_view chain) {
  for (const ChainStyle& style : kChainStyles) {
    if (style.chain == chain) return style.raster;
  }
  return kUnknownChainColor;
}

absl::flat_hash_map<std::string, std::string> PointChains(const SkeletonRig& rig) {
  absl::flat_hash_map<std::string, std::string> chains;
  chains.reserve(rig.points.size());
  for (const SkeletonRigPoint& point : rig.points) chains.emplace(point.name, point.chain);
  return chains;
}

void SetPixel(RgbaImage& image, int x, int y, PixelColor color) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  image.pixels[offset + 0] = color.r;
  image.pixels[offset + 1] = color.g;
  image.pixels[offset + 2] = color.b;
  image.pixels[offset + 3] = color.a;
}

void FillCircle(RgbaImage& image, int center_x, int center_y, int radius, PixelColor color) {
  const int radius_squared = radius * radius;
  for (int y = center_y - radius; y <= center_y + radius; ++y) {
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
      const int delta_x = x - center_x;
      const int delta_y = y - center_y;
      if (delta_x * delta_x + delta_y * delta_y <= radius_squared) {
        SetPixel(image, x, y, color);
      }
    }
  }
}

void DrawLine(RgbaImage& image, int start_x, int start_y, int end_x, int end_y, int radius,
              PixelColor color) {
  const int delta_x = std::abs(end_x - start_x);
  const int step_x = start_x < end_x ? 1 : -1;
  const int delta_y = -std::abs(end_y - start_y);
  const int step_y = start_y < end_y ? 1 : -1;
  int error = delta_x + delta_y;
  while (true) {
    FillCircle(image, start_x, start_y, radius, color);
    if (start_x == end_x && start_y == end_y) return;
    const int doubled_error = error * 2;
    if (doubled_error >= delta_y) {
      error += delta_y;
      start_x += step_x;
    }
    if (doubled_error <= delta_x) {
      error += delta_x;
      start_y += step_y;
    }
  }
}

void FillRectangle(RgbaImage& image, int left, int top, int width, int height, PixelColor color) {
  for (int y = top; y < top + height; ++y) {
    for (int x = left; x < left + width; ++x) SetPixel(image, x, y, color);
  }
}

absl::Status ValidateRasterGeometry(const SkeletonRigFrame& frame, int canvas_width,
                                    int canvas_height, int render_scale) {
  if (canvas_width <= 0 || canvas_height <= 0 || render_scale <= 0) {
    return absl::InvalidArgumentError("skeleton raster dimensions and scale must be positive");
  }
  const int64_t width = static_cast<int64_t>(canvas_width) * render_scale;
  const int64_t height = static_cast<int64_t>(canvas_height) * render_scale;
  if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
      width > kMaximumRasterPixels / height) {
    return absl::ResourceExhaustedError("skeleton raster exceeds its decoded-pixel limit");
  }
  for (const auto& [name, joint] : frame.pose) {
    if (joint.x < 0.0 || joint.y < 0.0 || joint.x >= canvas_width || joint.y >= canvas_height) {
      return absl::InvalidArgumentError(
          absl::StrCat("skeleton raster point '", name, "' lies outside the canvas"));
    }
  }
  return absl::OkStatus();
}

RgbaImage BlankImage(int width, int height) {
  RgbaImage image{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    image.pixels[offset + 0] = kCanvasColor.r;
    image.pixels[offset + 1] = kCanvasColor.g;
    image.pixels[offset + 2] = kCanvasColor.b;
    image.pixels[offset + 3] = kCanvasColor.a;
  }
  return image;
}

void RenderFrameIntoImage(const SkeletonRig& rig, const SkeletonRigFrame& frame,
                          const absl::flat_hash_map<std::string, std::string>& chains,
                          int canvas_width, int canvas_height, int render_scale, int offset_x,
                          int offset_y, RgbaImage& image) {
  const int floor_y = offset_y + static_cast<int>(std::lround(rig.floor_y * render_scale));
  const int rendered_width = canvas_width * render_scale;
  const int dash_width = 5 * render_scale;
  for (int x = 0; x < rendered_width; ++x) {
    if ((x / dash_width) % 2 == 0) SetPixel(image, offset_x + x, floor_y, kFloorColor);
  }

  const int bone_radius = std::max(1, render_scale + 1);
  for (const SkeletonRigBone& bone : rig.bones) {
    const SkeletonRigJoint& start = frame.pose.at(bone.start);
    const SkeletonRigJoint& end = frame.pose.at(bone.end);
    DrawLine(image, offset_x + static_cast<int>(std::lround(start.x * render_scale)),
             offset_y + static_cast<int>(std::lround(start.y * render_scale)),
             offset_x + static_cast<int>(std::lround(end.x * render_scale)),
             offset_y + static_cast<int>(std::lround(end.y * render_scale)), bone_radius,
             ChainRasterColor(chains.at(bone.end)));
  }

  const int joint_radius = render_scale + 1;
  for (const SkeletonRigPoint& point : rig.points) {
    const SkeletonRigJoint& joint = frame.pose.at(point.name);
    const int center_x = offset_x + static_cast<int>(std::lround(joint.x * render_scale));
    const int center_y = offset_y + static_cast<int>(std::lround(joint.y * render_scale));
    FillRectangle(image, center_x - joint_radius - 1, center_y - joint_radius - 1,
                  joint_radius * 2 + 3, joint_radius * 2 + 3, kJointOutlineColor);
    FillRectangle(image, center_x - joint_radius, center_y - joint_radius, joint_radius * 2 + 1,
                  joint_radius * 2 + 1, ChainRasterColor(point.chain));
  }
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

absl::StatusOr<RgbaImage> RenderSkeletonRigFrameImage(const SkeletonRig& rig,
                                                      const SkeletonRigFrame& frame,
                                                      int canvas_width, int canvas_height,
                                                      int render_scale) {
  RETURN_IF_ERROR(ValidateRasterGeometry(frame, canvas_width, canvas_height, render_scale));
  if (rig.floor_y < 0.0 || rig.floor_y >= canvas_height) {
    return absl::InvalidArgumentError("skeleton raster floor lies outside the canvas");
  }
  const int output_width = canvas_width * render_scale;
  const int output_height = canvas_height * render_scale;
  RgbaImage image = BlankImage(output_width, output_height);
  RenderFrameIntoImage(rig, frame, PointChains(rig), canvas_width, canvas_height, render_scale, 0,
                       0, image);
  return image;
}

absl::StatusOr<RgbaImage> RenderSkeletonRigSheetImage(const SkeletonRig& rig,
                                                      const SkeletonRigClip& clip, int canvas_width,
                                                      int canvas_height, int render_scale,
                                                      int columns) {
  if (clip.frames.empty() || columns <= 0) {
    return absl::InvalidArgumentError("skeleton raster sheet needs frames and positive columns");
  }
  if (rig.floor_y < 0.0 || rig.floor_y >= canvas_height) {
    return absl::InvalidArgumentError("skeleton raster floor lies outside the canvas");
  }
  for (const SkeletonRigFrame& frame : clip.frames) {
    RETURN_IF_ERROR(ValidateRasterGeometry(frame, canvas_width, canvas_height, render_scale));
  }
  if (clip.frames.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return absl::ResourceExhaustedError("skeleton raster sheet has too many frames");
  }

  const int frame_count = static_cast<int>(clip.frames.size());
  const int64_t rows = (static_cast<int64_t>(frame_count) + columns - 1) / columns;
  const int cell_width = canvas_width * render_scale;
  const int cell_height = canvas_height * render_scale;
  const int64_t output_width = static_cast<int64_t>(cell_width) * columns;
  const int64_t output_height = static_cast<int64_t>(cell_height) * rows;
  if (output_width > std::numeric_limits<int>::max() ||
      output_height > std::numeric_limits<int>::max() ||
      output_width > kMaximumRasterPixels / output_height) {
    return absl::ResourceExhaustedError("skeleton raster sheet exceeds its decoded-pixel limit");
  }

  RgbaImage image = BlankImage(static_cast<int>(output_width), static_cast<int>(output_height));
  const absl::flat_hash_map<std::string, std::string> chains = PointChains(rig);
  for (size_t index = 0; index < clip.frames.size(); ++index) {
    const int column = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    RenderFrameIntoImage(rig, clip.frames[index], chains, canvas_width, canvas_height, render_scale,
                         column * cell_width, row * cell_height, image);
  }
  return image;
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
