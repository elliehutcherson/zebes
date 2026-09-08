#include "artwork/layered_puppet_spec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "artwork/layered_puppet.h"
#include "artwork/semantic_layer_import.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

// Specs written before pose_order existed name these four poses implicitly.
constexpr std::array<const char*, 4> kLegacyPoseNames = {"neutral", "contact", "passing",
                                                         "airborne"};

// Named so ASSIGN_OR_RETURN can hold one: the macro splits on commas, and the
// template argument list has one.
using FillColor = std::array<uint8_t, 4>;

absl::StatusOr<ProfileControlPoint> ReadPoint(const nlohmann::json& value,
                                              const std::string& label) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
    return absl::InvalidArgumentError(label + " must be a two-number point");
  }
  const ProfileControlPoint point{
      .x = value[0].get<double>(),
      .y = value[1].get<double>(),
  };
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return absl::InvalidArgumentError(label + " must contain finite coordinates");
  }
  return point;
}

absl::StatusOr<LayeredPuppetPolygon> ReadPolygon(const nlohmann::json& value,
                                                 const std::string& label) {
  if (!value.is_array() || value.size() < 3) {
    return absl::InvalidArgumentError(label + " must contain at least three points");
  }
  LayeredPuppetPolygon polygon;
  polygon.points.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    ASSIGN_OR_RETURN(ProfileControlPoint point,
                     ReadPoint(value[index], label + "[" + std::to_string(index) + "]"));
    polygon.points.push_back(point);
  }
  return polygon;
}

absl::StatusOr<FillColor> ReadFillColor(const nlohmann::json& value, const RgbaImage& source,
                                        const std::string& label) {
  if (value.contains("sample")) {
    ASSIGN_OR_RETURN(const ProfileControlPoint sample,
                     ReadPoint(value.at("sample"), label + ".sample"));
    const int x = static_cast<int>(std::lround(sample.x));
    const int y = static_cast<int>(std::lround(sample.y));
    if (x < 0 || y < 0 || x >= source.width || y >= source.height) {
      return absl::InvalidArgumentError(label + ".sample is outside the source canvas");
    }
    const size_t offset = (static_cast<size_t>(y) * source.width + x) * 4;
    if (source.pixels[offset + 3] == 0) {
      return absl::InvalidArgumentError(label + ".sample selects a transparent pixel");
    }
    return FillColor{source.pixels[offset], source.pixels[offset + 1], source.pixels[offset + 2],
                     source.pixels[offset + 3]};
  }
  if (!value.contains("color") || !value.at("color").is_array() || value.at("color").size() != 4) {
    return absl::InvalidArgumentError(label + " needs a sample point or RGBA8 color");
  }
  FillColor color{};
  for (size_t channel = 0; channel < color.size(); ++channel) {
    const nlohmann::json& component = value.at("color")[channel];
    if (!component.is_number_integer()) {
      return absl::InvalidArgumentError(label + ".color must contain RGBA8 integers");
    }
    const int number = component.get<int>();
    if (number < 0 || number > 255) {
      return absl::InvalidArgumentError(label + ".color channel is outside RGBA8");
    }
    color[channel] = static_cast<uint8_t>(number);
  }
  return color;
}

RgbaImage EmptyCanvasLike(const RgbaImage& source) {
  return {
      .width = source.width,
      .height = source.height,
      .pixels = std::vector<uint8_t>(source.pixels.size(), 0),
  };
}

absl::Status ClearVisiblePolygons(RgbaImage& visible, const RgbaImage& source,
                                  absl::Span<const LayeredPuppetPolygon> exclusions) {
  if (exclusions.empty()) return absl::OkStatus();
  ASSIGN_OR_RETURN(RgbaImage excluded, BuildLayeredPuppetPartArtwork(source, exclusions, {}));
  for (size_t offset = 0; offset < visible.pixels.size(); offset += 4) {
    if (excluded.pixels[offset + 3] == 0) continue;
    std::fill_n(visible.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
  }
  return absl::OkStatus();
}

// Restores one semantic layer onto the source canvas. This is the candidate
// artwork before any ownership decision, so it can also drive the ownership
// mask.
absl::StatusOr<RgbaImage> LoadSemanticCandidate(const RgbaImage& source, const std::string& tag,
                                                const std::filesystem::path& root,
                                                const nlohmann::json& metadata,
                                                std::optional<size_t> component_index,
                                                bool clip_to_source_alpha) {
  if (!IsSafeLayeredPuppetPartName(tag) || root.empty()) {
    return absl::InvalidArgumentError("semantic puppet part tag or root is invalid");
  }
  const nlohmann::json& frame_size = metadata.at("frame_size");
  const nlohmann::json& part = metadata.at("parts").at(tag);
  const nlohmann::json& coordinates = part.at("xyxy");
  if (!frame_size.is_array() || frame_size.size() != 2 || !coordinates.is_array() ||
      coordinates.size() != 4) {
    return absl::InvalidArgumentError("semantic layer metadata dimensions are invalid");
  }
  // See-through has only ever emitted a square canvas, so which of these two is
  // height and which is width has never been exercised. Refuse a non-square
  // canvas rather than silently transposing every layer placement.
  const int canvas_height = frame_size.at(0).get<int>();
  const int canvas_width = frame_size.at(1).get<int>();
  if (canvas_width != canvas_height) {
    return absl::UnimplementedError(
        "semantic layer canvas is not square; confirm whether frame_size is "
        "[height, width] or [width, height] before trusting layer placement");
  }
  const int left = coordinates.at(0).get<int>();
  const int top = coordinates.at(1).get<int>();
  const int right = coordinates.at(2).get<int>();
  const int bottom = coordinates.at(3).get<int>();
  const absl::StatusOr<RgbaImage> cropped = ReadPng((root / (tag + ".png")).string());
  if (!cropped.ok()) return cropped.status();
  ASSIGN_OR_RETURN(
      RgbaImage restored,
      RestoreSemanticLayer(*cropped,
                           {.x = left, .y = top, .width = right - left, .height = bottom - top},
                           canvas_width, canvas_height));
  ASSIGN_OR_RETURN(RgbaImage candidate,
                   DownsampleSemanticLayer(restored, source.width, source.height));
  if (component_index.has_value()) {
    ASSIGN_OR_RETURN(std::vector<SemanticLayerComponent> components,
                     SplitSemanticLayerComponents(candidate));
    if (*component_index >= components.size()) {
      return absl::InvalidArgumentError("semantic puppet component index is out of range");
    }
    candidate = std::move(components[*component_index].artwork);
  }
  if (clip_to_source_alpha) {
    ASSIGN_OR_RETURN(candidate, ClipSemanticLayerToMask(candidate, source));
  }
  return candidate;
}

absl::StatusOr<LayeredPuppetSpecBuild> ParsePuppet(const RgbaImage& source,
                                                   const nlohmann::json& spec,
                                                   const LayeredPuppetSemanticSource* semantic,
                                                   const nlohmann::json* semantic_metadata) {
  if (spec.at("version").get<int>() != 1) {
    return absl::InvalidArgumentError("layered puppet spec version must be 1");
  }
  if (spec.at("width").get<int>() != source.width ||
      spec.at("height").get<int>() != source.height) {
    // Both sizes, because the fix depends on which one is wrong: stretch the
    // puppet onto the picture, or point it at a different picture.
    return absl::InvalidArgumentError(absl::StrFormat(
        "this puppet is written in %d x %d coordinates but its source picture is %d x %d; "
        "stretch the puppet to the picture's size",
        spec.at("width").get<int>(), spec.at("height").get<int>(), source.width, source.height));
  }

  const std::filesystem::path semantic_root =
      semantic == nullptr ? std::filesystem::path() : semantic->root;
  LayeredPuppetSpecBuild build;
  LayeredPuppet& puppet = build.puppet;
  puppet.width = source.width;
  puppet.height = source.height;

  absl::flat_hash_map<std::string, size_t> joint_indices;
  std::vector<std::string> joint_names;
  const nlohmann::json& joints = spec.at("joints");
  if (!joints.is_object() || joints.empty()) {
    return absl::InvalidArgumentError("puppet spec joints must be a non-empty object");
  }
  for (const auto& [name, coordinates] : joints.items()) {
    ASSIGN_OR_RETURN(ProfileControlPoint point, ReadPoint(coordinates, "spec.joints." + name));
    joint_indices.emplace(name, puppet.source_joints.size());
    joint_names.push_back(name);
    puppet.source_joints.push_back(point);
  }

  absl::flat_hash_map<std::string, size_t> bone_indices;
  for (const nlohmann::json& bone : spec.at("bones")) {
    const std::string name = bone.at("name").get<std::string>();
    const std::string start_name = bone.at("start").get<std::string>();
    const std::string end_name = bone.at("end").get<std::string>();
    const auto start = joint_indices.find(start_name);
    const auto end = joint_indices.find(end_name);
    if (name.empty() || start == joint_indices.end() || end == joint_indices.end() ||
        bone_indices.contains(name)) {
      return absl::InvalidArgumentError(
          "puppet spec bone is duplicate or references an unknown joint");
    }
    bone_indices.emplace(name, puppet.bones.size());
    puppet.bones.push_back({.start_joint = start->second,
                            .end_joint = end->second,
                            .may_stretch = bone.at("may_stretch").get<bool>()});
  }

  absl::flat_hash_map<std::string, size_t> part_indices;
  // Derived ownership regions, so a later part can subtract exactly what an
  // earlier one claimed instead of repeating an outline that can drift.
  absl::flat_hash_map<std::string, RgbaImage> ownership_masks;
  // Stretches run after every part is parsed, so a layer may stretch to cover a
  // part declared after it and spec order stays free.
  struct PendingStretch {
    std::string part_name;
    nlohmann::json settings;
  };
  std::vector<PendingStretch> pending_stretches;
  const nlohmann::json& parts = spec.at("parts");
  if (!parts.is_array() || parts.empty()) {
    return absl::InvalidArgumentError("puppet spec parts must be a non-empty array");
  }
  for (size_t part_index = 0; part_index < parts.size(); ++part_index) {
    const nlohmann::json& part = parts[part_index];
    const std::string label = "spec.parts[" + std::to_string(part_index) + "]";
    const std::string name = part.at("name").get<std::string>();
    std::vector<size_t> part_bones;
    if (part.contains("bone")) {
      const std::string bone_name = part.at("bone").get<std::string>();
      const auto bone = bone_indices.find(bone_name);
      if (bone == bone_indices.end()) {
        return absl::InvalidArgumentError("puppet part references an unknown bone");
      }
      part_bones.push_back(bone->second);
    } else if (part.contains("bones") && part.at("bones").is_array()) {
      for (const nlohmann::json& bone_name_json : part.at("bones")) {
        const std::string bone_name = bone_name_json.get<std::string>();
        const auto bone = bone_indices.find(bone_name);
        if (bone == bone_indices.end()) {
          return absl::InvalidArgumentError("puppet part references an unknown bone");
        }
        part_bones.push_back(bone->second);
      }
    }
    if (!IsSafeLayeredPuppetPartName(name) || part_indices.contains(name) || part_bones.empty() ||
        part_bones.size() > 2) {
      return absl::InvalidArgumentError("puppet part name or bone chain is invalid");
    }

    std::vector<LayeredPuppetPolygon> source_polygons;
    if (part.contains("source_polygons")) {
      const nlohmann::json& polygons = part.at("source_polygons");
      if (!polygons.is_array()) {
        return absl::InvalidArgumentError(label + ".source_polygons must be an array");
      }
      for (size_t index = 0; index < polygons.size(); ++index) {
        ASSIGN_OR_RETURN(LayeredPuppetPolygon polygon,
                         ReadPolygon(polygons[index],
                                     label + ".source_polygons[" + std::to_string(index) + "]"));
        source_polygons.push_back(std::move(polygon));
      }
    }

    std::vector<LayeredPuppetPolygon> source_exclusions;
    if (part.contains("source_exclude_polygons")) {
      const nlohmann::json& polygons = part.at("source_exclude_polygons");
      if (!polygons.is_array()) {
        return absl::InvalidArgumentError(label + ".source_exclude_polygons must be an array");
      }
      for (size_t index = 0; index < polygons.size(); ++index) {
        ASSIGN_OR_RETURN(LayeredPuppetPolygon polygon,
                         ReadPolygon(polygons[index], label + ".source_exclude_polygons[" +
                                                          std::to_string(index) + "]"));
        source_exclusions.push_back(std::move(polygon));
      }
    }
    // A part may derive ownership from its own candidate layer instead of an
    // authored outline. Doing so is what keeps the arm's outline and the body's
    // matching cut-out from drifting apart, and what stops a candidate whose
    // alpha spills onto a neighbouring part from claiming it.
    RgbaImage semantic_candidate;
    RgbaImage ownership_mask;
    if (part.contains("source_from_semantic_reach")) {
      if (!part.contains("semantic_tag") || semantic_metadata == nullptr) {
        return absl::InvalidArgumentError(label +
                                          ".source_from_semantic_reach needs a semantic tag");
      }
      if (!source_polygons.empty() || !source_exclusions.empty()) {
        return absl::InvalidArgumentError(label +
                                          " cannot mix derived ownership with authored polygons");
      }
      const nlohmann::json& settings = part.at("source_from_semantic_reach");
      ASSIGN_OR_RETURN(
          semantic_candidate,
          LoadSemanticCandidate(
              source, part.at("semantic_tag").get<std::string>(), semantic_root, *semantic_metadata,
              part.contains("semantic_component")
                  ? std::optional<size_t>(part.at("semantic_component").get<size_t>())
                  : std::nullopt,
              part.value("clip_to_source_alpha", false)));
      ASSIGN_OR_RETURN(ownership_mask,
                       BuildLayeredPuppetOwnershipMask(semantic_candidate, source, part_bones,
                                                       puppet.bones, puppet.source_joints,
                                                       {.start = settings.at("start").get<double>(),
                                                        .end = settings.at("end").get<double>(),
                                                        .grow = settings.value("grow", 1)}));
      ownership_masks.emplace(name, ownership_mask);
    }

    std::vector<std::string> excluded_parts;
    if (part.contains("source_exclude_parts")) {
      const nlohmann::json& names = part.at("source_exclude_parts");
      if (!names.is_array()) {
        return absl::InvalidArgumentError(label + ".source_exclude_parts must be an array");
      }
      for (const nlohmann::json& entry : names) {
        if (!entry.is_string()) {
          return absl::InvalidArgumentError(label + ".source_exclude_parts must contain names");
        }
        excluded_parts.push_back(entry.get<std::string>());
      }
    }

    RgbaImage visible_artwork = EmptyCanvasLike(source);
    if (ownership_mask.IsValid()) {
      ASSIGN_OR_RETURN(visible_artwork, BuildLayeredPuppetMaskedArtwork(source, ownership_mask));
    } else if (!source_polygons.empty()) {
      ASSIGN_OR_RETURN(visible_artwork, BuildLayeredPuppetPartArtwork(source, source_polygons, {}));
      RETURN_IF_ERROR(ClearVisiblePolygons(visible_artwork, source, source_exclusions));
    } else if (!source_exclusions.empty()) {
      return absl::InvalidArgumentError("source exclusions require source ownership polygons");
    }
    std::vector<const RgbaImage*> excluded_masks;
    for (const std::string& excluded : excluded_parts) {
      const auto owner = ownership_masks.find(excluded);
      if (owner == ownership_masks.end()) {
        return absl::InvalidArgumentError(label + " excludes " + excluded +
                                          ", which has no derived ownership yet");
      }
      excluded_masks.push_back(&owner->second);
      const absl::Status subtracted = SubtractLayeredPuppetMask(visible_artwork, owner->second);
      if (!subtracted.ok()) {
        return absl::Status(subtracted.code(), absl::StrCat(label, " could not exclude ", excluded,
                                                            ": ", subtracted.message()));
      }
    }

    std::vector<LayeredPuppetFill> fills;
    if (part.contains("fill_polygons")) {
      const nlohmann::json& fill_json = part.at("fill_polygons");
      if (!fill_json.is_array()) {
        return absl::InvalidArgumentError(label + ".fill_polygons must be an array");
      }
      for (size_t index = 0; index < fill_json.size(); ++index) {
        const nlohmann::json& fill = fill_json[index];
        ASSIGN_OR_RETURN(LayeredPuppetPolygon polygon,
                         ReadPolygon(fill.at("points"), label + ".fill_polygons[" +
                                                            std::to_string(index) + "].points"));
        ASSIGN_OR_RETURN(
            const FillColor color,
            ReadFillColor(fill, source, label + ".fill_polygons[" + std::to_string(index) + "]"));
        fills.push_back({.polygon = std::move(polygon), .color = color});
      }
    }
    RgbaImage artwork;
    if (part.contains("semantic_tag")) {
      if (semantic_metadata == nullptr || !fills.empty()) {
        return absl::InvalidArgumentError(
            "semantic puppet part requires metadata and forbids solid fills");
      }
      if (!semantic_candidate.IsValid()) {
        ASSIGN_OR_RETURN(
            semantic_candidate,
            LoadSemanticCandidate(
                source, part.at("semantic_tag").get<std::string>(), semantic_root,
                *semantic_metadata,
                part.contains("semantic_component")
                    ? std::optional<size_t>(part.at("semantic_component").get<size_t>())
                    : std::nullopt,
                part.value("clip_to_source_alpha", false)));
      }
      if (part.value("immutable_semantic_layer", false)) {
        build.immutable_semantic_layers.insert_or_assign(name, semantic_candidate);
      }
      ASSIGN_OR_RETURN(artwork,
                       PreserveSemanticVisiblePixels(semantic_candidate, source, visible_artwork));
    } else {
      ASSIGN_OR_RETURN(artwork, BuildLayeredPuppetPartArtwork(source, source_polygons, fills));
      RETURN_IF_ERROR(ClearVisiblePolygons(artwork, source, source_exclusions));
    }
    // The rendered artwork must lose the same region the ownership record did,
    // or the part keeps drawing pixels it no longer owns and the ownership gate
    // reports a clean decomposition over a composite that still ghosts.
    for (const RgbaImage* excluded : excluded_masks) {
      const absl::Status subtracted = SubtractLayeredPuppetMask(artwork, *excluded);
      if (!subtracted.ok()) {
        return absl::Status(
            subtracted.code(),
            absl::StrCat(label, " could not exclude rendered artwork: ", subtracted.message()));
      }
    }
    if (!ownership_masks.contains(name)) ownership_masks.emplace(name, visible_artwork);
    if (part.contains("stretch_to_cover_parts")) {
      const nlohmann::json& settings = part.at("stretch_to_cover_parts");
      if (!settings.is_object() || !settings.contains("parts") ||
          !settings.at("parts").is_array()) {
        return absl::InvalidArgumentError(label + ".stretch_to_cover_parts needs a parts list");
      }
      pending_stretches.push_back({.part_name = name, .settings = settings});
    }
    LayeredPuppetMesh mesh;
    if (part_bones.size() == 2) {
      ASSIGN_OR_RETURN(
          mesh, BuildLayeredPuppetMesh(artwork, part_bones, puppet.bones, puppet.source_joints,
                                       part.value("mesh_spacing", 4),
                                       part.value("joint_blend_radius", 12.0),
                                       part.value("joint_blend_lateral_scale", 0.0)));
    }
    part_indices.emplace(name, puppet.parts.size());
    puppet.parts.push_back({.name = name,
                            .bone_indices = std::move(part_bones),
                            .artwork = std::move(artwork),
                            .visible_artwork = std::move(visible_artwork),
                            .mesh = std::move(mesh)});
  }

  const nlohmann::json& poses = spec.at("poses");
  const nlohmann::json& draw_orders = spec.at("draw_order");
  if (!poses.is_object() || poses.empty() || !draw_orders.is_object() ||
      draw_orders.size() != poses.size()) {
    return absl::InvalidArgumentError("puppet poses and draw_order must be matching objects");
  }
  std::vector<std::string> pose_names;
  if (spec.contains("pose_order")) {
    const nlohmann::json& pose_order = spec.at("pose_order");
    if (!pose_order.is_array() || pose_order.size() != poses.size()) {
      return absl::InvalidArgumentError("puppet pose_order must name every pose exactly once");
    }
    for (const nlohmann::json& name : pose_order) {
      if (!name.is_string()) {
        return absl::InvalidArgumentError("puppet pose_order must contain names");
      }
      const std::string pose_name = name.get<std::string>();
      if (!poses.contains(pose_name) || !draw_orders.contains(pose_name) ||
          std::find(pose_names.begin(), pose_names.end(), pose_name) != pose_names.end()) {
        return absl::InvalidArgumentError("puppet pose_order is duplicate or unknown");
      }
      pose_names.push_back(pose_name);
    }
  } else {
    pose_names.assign(kLegacyPoseNames.begin(), kLegacyPoseNames.end());
  }
  for (const std::string& pose_name : pose_names) {
    const nlohmann::json& pose_json = poses.at(pose_name);
    LayeredPuppetPose pose{.name = pose_name};
    pose.joints.reserve(joint_names.size());
    for (const std::string& joint_name : joint_names) {
      ASSIGN_OR_RETURN(
          ProfileControlPoint point,
          ReadPoint(pose_json.at(joint_name), "spec.poses." + pose_name + "." + joint_name));
      pose.joints.push_back(point);
    }
    for (const nlohmann::json& part_name_json : draw_orders.at(pose_name)) {
      const std::string part_name = part_name_json.get<std::string>();
      const auto part = part_indices.find(part_name);
      if (part == part_indices.end()) {
        return absl::InvalidArgumentError("puppet draw order references an unknown part");
      }
      pose.draw_order.push_back(part->second);
    }
    puppet.poses.push_back(std::move(pose));
  }
  for (const PendingStretch& pending : pending_stretches) {
    RgbaImage required = EmptyCanvasLike(source);
    RgbaImage& stretched_layer = puppet.parts[part_indices.at(pending.part_name)].artwork;
    size_t cleared_outside_required_pixels = 0;
    for (const nlohmann::json& entry : pending.settings.at("parts")) {
      if (!entry.is_string()) {
        return absl::InvalidArgumentError("stretch_to_cover_parts must contain part names");
      }
      const std::string owner_name = entry.get<std::string>();
      const auto owner = ownership_masks.find(owner_name);
      if (owner == ownership_masks.end()) {
        return absl::InvalidArgumentError(pending.part_name + " stretches to cover " + owner_name +
                                          ", which does not derive its ownership");
      }
      RgbaImage relationship = owner->second;
      const bool has_attachment = pending.settings.contains("attachment_falloff");
      if (has_attachment) {
        const nlohmann::json& falloff = pending.settings.at("attachment_falloff");
        const LayeredPuppetPart& moving_part = puppet.parts[part_indices.at(owner_name)];
        ASSIGN_OR_RETURN(
            relationship,
            BuildLayeredPuppetAttachmentMask(
                owner->second, moving_part.bone_indices, puppet.bones, puppet.source_joints,
                {.fully_connected_until = falloff.at("fully_connected_until").get<double>(),
                 .disconnected_at = falloff.at("disconnected_at").get<double>()}));
        build.attachment_relationships.insert_or_assign(owner_name, relationship);
      }

      RgbaImage coverage = owner->second;
      if (pending.settings.contains("within_polygons")) {
        const nlohmann::json& polygons_json = pending.settings.at("within_polygons");
        if (!polygons_json.is_array()) {
          return absl::InvalidArgumentError("stretch within_polygons must be an array");
        }
        std::vector<LayeredPuppetPolygon> polygons;
        for (size_t index = 0; index < polygons_json.size(); ++index) {
          ASSIGN_OR_RETURN(
              LayeredPuppetPolygon polygon,
              ReadPolygon(polygons_json[index],
                          pending.part_name + ".within_polygons[" + std::to_string(index) + "]"));
          polygons.push_back(std::move(polygon));
        }
        ASSIGN_OR_RETURN(const RgbaImage allowed,
                         BuildLayeredPuppetPartArtwork(source, polygons, {}));
        for (size_t offset = 0; offset < coverage.pixels.size(); offset += 4) {
          if (allowed.pixels[offset + 3] != 0) continue;
          std::fill_n(coverage.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
        }
      }
      if (has_attachment) {
        for (size_t offset = 0; offset < coverage.pixels.size(); offset += 4) {
          if (relationship.pixels[offset + 3] <= coverage.pixels[offset + 3]) {
            continue;
          }
          std::copy_n(relationship.pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                      coverage.pixels.begin() + static_cast<ptrdiff_t>(offset));
        }
      }
      if (pending.settings.value("clear_outside_required", false)) {
        for (size_t offset = 0; offset < stretched_layer.pixels.size(); offset += 4) {
          if (owner->second.pixels[offset + 3] == 0 || coverage.pixels[offset + 3] != 0 ||
              stretched_layer.pixels[offset + 3] == 0) {
            continue;
          }
          std::fill_n(stretched_layer.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
          ++cleared_outside_required_pixels;
        }
      }
      build.backfill_requirements.insert_or_assign(owner_name, coverage);
      for (size_t offset = 0; offset < required.pixels.size(); offset += 4) {
        if (coverage.pixels[offset + 3] <= required.pixels[offset + 3]) {
          continue;
        }
        std::copy_n(coverage.pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                    required.pixels.begin() + static_cast<ptrdiff_t>(offset));
      }
    }
    ASSIGN_OR_RETURN(const LayeredPuppetStretchReport stretch,
                     StretchLayeredPuppetBackfill(stretched_layer, required,
                                                  pending.settings.value("distance", 24),
                                                  pending.settings.value("edge_inset", 2)));
    build.stretch_reports.push_back({
        .part_name = pending.part_name,
        .required_pixels = stretch.required_pixels,
        .filled_pixels = stretch.filled_pixels,
        .unreachable_pixels = stretch.unreachable_pixels,
        .cleared_outside_required_pixels = cleared_outside_required_pixels,
    });
  }
  RETURN_IF_ERROR(ValidateLayeredPuppet(puppet));
  return build;
}

}  // namespace

absl::StatusOr<LayeredPuppetSpecBuild> BuildLayeredPuppetFromSpec(
    const RgbaImage& source, std::string_view encoded_spec,
    const LayeredPuppetSemanticSource* semantic) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("layered puppet spec source artwork is invalid");
  }
  // nlohmann reports every malformed document and every wrong-typed field by
  // throwing. This is the adapter that owns that API, so the translation to
  // absl::Status happens here and no caller ever sees an exception.
  try {
    const nlohmann::json spec = nlohmann::json::parse(encoded_spec);
    std::optional<nlohmann::json> semantic_metadata;
    if (semantic != nullptr) {
      semantic_metadata = nlohmann::json::parse(semantic->encoded_metadata);
    }
    return ParsePuppet(source, spec, semantic,
                       semantic_metadata.has_value() ? &*semantic_metadata : nullptr);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError(absl::StrCat("invalid layered puppet spec: ", error.what()));
  }
}

}  // namespace zebes
