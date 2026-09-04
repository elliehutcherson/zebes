#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/layered_puppet.h"
#include "artwork/layered_puppet_diagnostics.h"
#include "artwork/semantic_layer_import.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, source, "", "Approved working-resolution RGBA source PNG.");
ABSL_FLAG(std::string, spec, "", "Explicit layered puppet JSON specification.");
ABSL_FLAG(std::string, semantic_root, "",
          "Optional See-through optimized PNG and info.json directory.");
ABSL_FLAG(std::string, output, "", "Destination evidence directory.");
ABSL_FLAG(int, frame_size, 48, "Native square output frame size.");
ABSL_FLAG(int, zoom, 8, "Integer nearest-neighbor evidence zoom.");

namespace {

constexpr std::array<const char*, 4> kPoseNames = {"neutral", "contact", "passing", "airborne"};

absl::StatusOr<nlohmann::json> ReadJson(const std::string& path, const std::string& label) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError("could not open " + label + ": " + path);
  try {
    return nlohmann::json::parse(stream);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError("invalid " + label + ": " + error.what());
  }
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
  std::ofstream stream(path);
  if (!stream.is_open()) return absl::InternalError("could not create puppet manifest");
  stream << value.dump(2) << '\n';
  if (!stream.good()) return absl::InternalError("could not write puppet manifest");
  return absl::OkStatus();
}

absl::StatusOr<zebes::ProfileControlPoint> ReadPoint(const nlohmann::json& value,
                                                     const std::string& label) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
    return absl::InvalidArgumentError(label + " must be a two-number point");
  }
  const zebes::ProfileControlPoint point{
      .x = value[0].get<double>(),
      .y = value[1].get<double>(),
  };
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return absl::InvalidArgumentError(label + " must contain finite coordinates");
  }
  return point;
}

absl::StatusOr<zebes::LayeredPuppetPolygon> ReadPolygon(const nlohmann::json& value,
                                                        const std::string& label) {
  if (!value.is_array() || value.size() < 3) {
    return absl::InvalidArgumentError(label + " must contain at least three points");
  }
  zebes::LayeredPuppetPolygon polygon;
  polygon.points.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    ASSIGN_OR_RETURN(zebes::ProfileControlPoint point,
                     ReadPoint(value[index], label + "[" + std::to_string(index) + "]"));
    polygon.points.push_back(point);
  }
  return polygon;
}

absl::StatusOr<std::array<uint8_t, 4>> ReadFillColor(const nlohmann::json& value,
                                                     const zebes::RgbaImage& source,
                                                     const std::string& label) {
  if (value.contains("sample")) {
    ASSIGN_OR_RETURN(const zebes::ProfileControlPoint sample,
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
    return std::array<uint8_t, 4>{source.pixels[offset], source.pixels[offset + 1],
                                  source.pixels[offset + 2], source.pixels[offset + 3]};
  }
  if (!value.contains("color") || !value.at("color").is_array() || value.at("color").size() != 4) {
    return absl::InvalidArgumentError(label + " needs a sample point or RGBA8 color");
  }
  std::array<uint8_t, 4> color{};
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

bool IsSafePartName(const std::string& name) {
  if (name.empty()) return false;
  for (const char character : name) {
    if ((character < 'a' || character > 'z') && (character < 'A' || character > 'Z') &&
        (character < '0' || character > '9') && character != '_' && character != '-') {
      return false;
    }
  }
  return true;
}

zebes::RgbaImage EmptyCanvasLike(const zebes::RgbaImage& source) {
  return {
      .width = source.width,
      .height = source.height,
      .pixels = std::vector<uint8_t>(source.pixels.size(), 0),
  };
}

absl::Status ClearVisiblePolygons(zebes::RgbaImage& visible, const zebes::RgbaImage& source,
                                  absl::Span<const zebes::LayeredPuppetPolygon> exclusions) {
  if (exclusions.empty()) return absl::OkStatus();
  ASSIGN_OR_RETURN(zebes::RgbaImage excluded,
                   zebes::BuildLayeredPuppetPartArtwork(source, exclusions, {}));
  for (size_t offset = 0; offset < visible.pixels.size(); offset += 4) {
    if (excluded.pixels[offset + 3] == 0) continue;
    std::fill_n(visible.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
  }
  return absl::OkStatus();
}

// Restores one semantic layer onto the source canvas. This is the candidate
// artwork before any ownership decision, so it can also drive the ownership
// mask.
absl::StatusOr<zebes::RgbaImage> LoadSemanticCandidate(const zebes::RgbaImage& source,
                                                       const std::string& tag,
                                                       const std::filesystem::path& root,
                                                       const nlohmann::json& metadata,
                                                       bool clip_to_source_alpha) {
  if (!IsSafePartName(tag) || root.empty()) {
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
  const absl::StatusOr<zebes::RgbaImage> cropped = zebes::ReadPng((root / (tag + ".png")).string());
  if (!cropped.ok()) return cropped.status();
  ASSIGN_OR_RETURN(
      zebes::RgbaImage restored,
      zebes::RestoreSemanticLayer(
          *cropped, {.x = left, .y = top, .width = right - left, .height = bottom - top},
          canvas_width, canvas_height));
  ASSIGN_OR_RETURN(zebes::RgbaImage candidate,
                   zebes::DownsampleSemanticLayer(restored, source.width, source.height));
  if (clip_to_source_alpha) {
    ASSIGN_OR_RETURN(candidate, zebes::ClipSemanticLayerToMask(candidate, source));
  }
  return candidate;
}

absl::StatusOr<zebes::LayeredPuppet> ParsePuppet(
    const zebes::RgbaImage& source, const nlohmann::json& spec,
    const std::filesystem::path& semantic_root, const nlohmann::json* semantic_metadata,
    nlohmann::json& stretch_reports,
    absl::flat_hash_map<std::string, zebes::RgbaImage>& backfill_requirements,
    absl::flat_hash_map<std::string, zebes::RgbaImage>& attachment_relationships,
    absl::flat_hash_map<std::string, zebes::RgbaImage>& immutable_semantic_layers) {
  if (spec.at("version").get<int>() != 1) {
    return absl::InvalidArgumentError("layered puppet spec version must be 1");
  }
  if (spec.at("width").get<int>() != source.width ||
      spec.at("height").get<int>() != source.height) {
    return absl::InvalidArgumentError("source and puppet spec dimensions differ");
  }

  zebes::LayeredPuppet puppet{
      .width = source.width,
      .height = source.height,
  };
  absl::flat_hash_map<std::string, size_t> joint_indices;
  std::vector<std::string> joint_names;
  const nlohmann::json& joints = spec.at("joints");
  if (!joints.is_object() || joints.empty()) {
    return absl::InvalidArgumentError("puppet spec joints must be a non-empty object");
  }
  for (const auto& [name, coordinates] : joints.items()) {
    ASSIGN_OR_RETURN(zebes::ProfileControlPoint point,
                     ReadPoint(coordinates, "spec.joints." + name));
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
    puppet.bones.push_back({.start_joint = start->second, .end_joint = end->second});
  }

  absl::flat_hash_map<std::string, size_t> part_indices;
  // Derived ownership regions, so a later part can subtract exactly what an
  // earlier one claimed instead of repeating an outline that can drift.
  absl::flat_hash_map<std::string, zebes::RgbaImage> ownership_masks;
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
    if (!IsSafePartName(name) || part_indices.contains(name) || part_bones.empty() ||
        part_bones.size() > 2) {
      return absl::InvalidArgumentError("puppet part name or bone chain is invalid");
    }

    std::vector<zebes::LayeredPuppetPolygon> source_polygons;
    if (part.contains("source_polygons")) {
      const nlohmann::json& polygons = part.at("source_polygons");
      if (!polygons.is_array()) {
        return absl::InvalidArgumentError(label + ".source_polygons must be an array");
      }
      for (size_t index = 0; index < polygons.size(); ++index) {
        ASSIGN_OR_RETURN(zebes::LayeredPuppetPolygon polygon,
                         ReadPolygon(polygons[index],
                                     label + ".source_polygons[" + std::to_string(index) + "]"));
        source_polygons.push_back(std::move(polygon));
      }
    }

    std::vector<zebes::LayeredPuppetPolygon> source_exclusions;
    if (part.contains("source_exclude_polygons")) {
      const nlohmann::json& polygons = part.at("source_exclude_polygons");
      if (!polygons.is_array()) {
        return absl::InvalidArgumentError(label + ".source_exclude_polygons must be an array");
      }
      for (size_t index = 0; index < polygons.size(); ++index) {
        ASSIGN_OR_RETURN(zebes::LayeredPuppetPolygon polygon,
                         ReadPolygon(polygons[index], label + ".source_exclude_polygons[" +
                                                          std::to_string(index) + "]"));
        source_exclusions.push_back(std::move(polygon));
      }
    }
    // A part may derive ownership from its own candidate layer instead of an
    // authored outline. Doing so is what keeps the arm's outline and the body's
    // matching cut-out from drifting apart, and what stops a candidate whose
    // alpha spills onto a neighbouring part from claiming it.
    zebes::RgbaImage semantic_candidate;
    zebes::RgbaImage ownership_mask;
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
          LoadSemanticCandidate(source, part.at("semantic_tag").get<std::string>(), semantic_root,
                                *semantic_metadata, part.value("clip_to_source_alpha", false)));
      ASSIGN_OR_RETURN(ownership_mask, zebes::BuildLayeredPuppetOwnershipMask(
                                           semantic_candidate, source, part_bones, puppet.bones,
                                           puppet.source_joints,
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

    zebes::RgbaImage visible_artwork = EmptyCanvasLike(source);
    if (ownership_mask.IsValid()) {
      ASSIGN_OR_RETURN(visible_artwork,
                       zebes::BuildLayeredPuppetMaskedArtwork(source, ownership_mask));
    } else if (!source_polygons.empty()) {
      ASSIGN_OR_RETURN(visible_artwork,
                       zebes::BuildLayeredPuppetPartArtwork(source, source_polygons, {}));
      RETURN_IF_ERROR(ClearVisiblePolygons(visible_artwork, source, source_exclusions));
    } else if (!source_exclusions.empty()) {
      return absl::InvalidArgumentError("source exclusions require source ownership polygons");
    }
    std::vector<const zebes::RgbaImage*> excluded_masks;
    for (const std::string& excluded : excluded_parts) {
      const auto owner = ownership_masks.find(excluded);
      if (owner == ownership_masks.end()) {
        return absl::InvalidArgumentError(label + " excludes " + excluded +
                                          ", which has no derived ownership yet");
      }
      excluded_masks.push_back(&owner->second);
      RETURN_IF_ERROR(zebes::SubtractLayeredPuppetMask(visible_artwork, owner->second));
    }

    std::vector<zebes::LayeredPuppetFill> fills;
    if (part.contains("fill_polygons")) {
      const nlohmann::json& fill_json = part.at("fill_polygons");
      if (!fill_json.is_array()) {
        return absl::InvalidArgumentError(label + ".fill_polygons must be an array");
      }
      for (size_t index = 0; index < fill_json.size(); ++index) {
        const nlohmann::json& fill = fill_json[index];
        ASSIGN_OR_RETURN(zebes::LayeredPuppetPolygon polygon,
                         ReadPolygon(fill.at("points"), label + ".fill_polygons[" +
                                                            std::to_string(index) + "].points"));
        absl::StatusOr<std::array<uint8_t, 4>> color =
            ReadFillColor(fill, source, label + ".fill_polygons[" + std::to_string(index) + "]");
        if (!color.ok()) return color.status();
        fills.push_back({.polygon = std::move(polygon), .color = *color});
      }
    }
    zebes::RgbaImage artwork;
    if (part.contains("semantic_tag")) {
      if (semantic_metadata == nullptr || !fills.empty()) {
        return absl::InvalidArgumentError(
            "semantic puppet part requires metadata and forbids solid fills");
      }
      if (!semantic_candidate.IsValid()) {
        ASSIGN_OR_RETURN(
            semantic_candidate,
            LoadSemanticCandidate(source, part.at("semantic_tag").get<std::string>(), semantic_root,
                                  *semantic_metadata, part.value("clip_to_source_alpha", false)));
      }
      if (part.value("immutable_semantic_layer", false)) {
        immutable_semantic_layers.insert_or_assign(name, semantic_candidate);
      }
      ASSIGN_OR_RETURN(artwork, zebes::PreserveSemanticVisiblePixels(semantic_candidate, source,
                                                                     visible_artwork));
    } else {
      ASSIGN_OR_RETURN(artwork,
                       zebes::BuildLayeredPuppetPartArtwork(source, source_polygons, fills));
      RETURN_IF_ERROR(ClearVisiblePolygons(artwork, source, source_exclusions));
    }
    // The rendered artwork must lose the same region the ownership record did,
    // or the part keeps drawing pixels it no longer owns and the ownership gate
    // reports a clean decomposition over a composite that still ghosts.
    for (const zebes::RgbaImage* excluded : excluded_masks) {
      RETURN_IF_ERROR(zebes::SubtractLayeredPuppetMask(artwork, *excluded));
    }
    if (part.contains("stretch_to_cover_parts")) {
      const nlohmann::json& settings = part.at("stretch_to_cover_parts");
      if (!settings.is_object() || !settings.contains("parts") ||
          !settings.at("parts").is_array()) {
        return absl::InvalidArgumentError(label + ".stretch_to_cover_parts needs a parts list");
      }
      pending_stretches.push_back({.part_name = name, .settings = settings});
    }
    zebes::LayeredPuppetMesh mesh;
    if (part_bones.size() == 2) {
      ASSIGN_OR_RETURN(
          mesh, zebes::BuildLayeredPuppetMesh(artwork, part_bones, puppet.bones,
                                              puppet.source_joints, part.value("mesh_spacing", 4),
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
  for (const std::string pose_name : kPoseNames) {
    const nlohmann::json& pose_json = poses.at(pose_name);
    zebes::LayeredPuppetPose pose{.name = pose_name};
    pose.joints.reserve(joint_names.size());
    for (const std::string& joint_name : joint_names) {
      ASSIGN_OR_RETURN(
          zebes::ProfileControlPoint point,
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
    zebes::RgbaImage required = EmptyCanvasLike(source);
    zebes::RgbaImage& stretched_layer = puppet.parts[part_indices.at(pending.part_name)].artwork;
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
      zebes::RgbaImage relationship = owner->second;
      const bool has_attachment = pending.settings.contains("attachment_falloff");
      if (has_attachment) {
        const nlohmann::json& falloff = pending.settings.at("attachment_falloff");
        const zebes::LayeredPuppetPart& moving_part = puppet.parts[part_indices.at(owner_name)];
        ASSIGN_OR_RETURN(
            relationship,
            zebes::BuildLayeredPuppetAttachmentMask(
                owner->second, moving_part.bone_indices, puppet.bones, puppet.source_joints,
                {.fully_connected_until = falloff.at("fully_connected_until").get<double>(),
                 .disconnected_at = falloff.at("disconnected_at").get<double>()}));
        attachment_relationships.insert_or_assign(owner_name, relationship);
      }

      zebes::RgbaImage coverage = owner->second;
      if (pending.settings.contains("within_polygons")) {
        const nlohmann::json& polygons_json = pending.settings.at("within_polygons");
        if (!polygons_json.is_array()) {
          return absl::InvalidArgumentError("stretch within_polygons must be an array");
        }
        std::vector<zebes::LayeredPuppetPolygon> polygons;
        for (size_t index = 0; index < polygons_json.size(); ++index) {
          ASSIGN_OR_RETURN(
              zebes::LayeredPuppetPolygon polygon,
              ReadPolygon(polygons_json[index],
                          pending.part_name + ".within_polygons[" + std::to_string(index) + "]"));
          polygons.push_back(std::move(polygon));
        }
        ASSIGN_OR_RETURN(const zebes::RgbaImage allowed,
                         zebes::BuildLayeredPuppetPartArtwork(source, polygons, {}));
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
      backfill_requirements.insert_or_assign(owner_name, coverage);
      for (size_t offset = 0; offset < required.pixels.size(); offset += 4) {
        if (coverage.pixels[offset + 3] <= required.pixels[offset + 3]) {
          continue;
        }
        std::copy_n(coverage.pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                    required.pixels.begin() + static_cast<ptrdiff_t>(offset));
      }
    }
    ASSIGN_OR_RETURN(const zebes::LayeredPuppetStretchReport stretch,
                     zebes::StretchLayeredPuppetBackfill(stretched_layer, required,
                                                         pending.settings.value("distance", 24),
                                                         pending.settings.value("edge_inset", 2)));
    stretch_reports[pending.part_name] = {
        {"required_pixels", stretch.required_pixels},
        {"filled_pixels", stretch.filled_pixels},
        {"unreachable_pixels", stretch.unreachable_pixels},
        {"cleared_outside_required_pixels", cleared_outside_required_pixels},
    };
  }
  RETURN_IF_ERROR(zebes::ValidateLayeredPuppet(puppet));
  return puppet;
}

absl::Status WriteImage(const std::filesystem::path& path, const zebes::RgbaImage& image) {
  return zebes::WritePng(path.string(), image.width, image.height, image.pixels);
}

size_t OpaquePixelCount(const zebes::RgbaImage& image) {
  size_t count = 0;
  for (size_t offset = 3; offset < image.pixels.size(); offset += 4) {
    if (image.pixels[offset] != 0) ++count;
  }
  return count;
}

size_t PixelDifferenceCount(const zebes::RgbaImage& first, const zebes::RgbaImage& second) {
  size_t count = 0;
  for (size_t offset = 0; offset < first.pixels.size(); offset += 4) {
    if (!std::equal(first.pixels.begin() + static_cast<ptrdiff_t>(offset),
                    first.pixels.begin() + static_cast<ptrdiff_t>(offset + 4),
                    second.pixels.begin() + static_cast<ptrdiff_t>(offset))) {
      ++count;
    }
  }
  return count;
}

bool ContainsPose(const nlohmann::json& poses, const std::string& pose_name) {
  if (!poses.is_array()) return false;
  return std::any_of(poses.begin(), poses.end(), [&pose_name](const nlohmann::json& pose) {
    return pose.is_string() && pose.get<std::string>() == pose_name;
  });
}

absl::Status ApplyConfiguredShadows(zebes::RgbaImage& composite, const zebes::LayeredPuppet& puppet,
                                    const zebes::LayeredPuppetPose& pose,
                                    const nlohmann::json& spec) {
  if (!spec.contains("shadows")) return absl::OkStatus();
  if (!spec.at("shadows").is_array()) {
    return absl::InvalidArgumentError("puppet shadows must be an array");
  }
  for (const nlohmann::json& shadow : spec.at("shadows")) {
    if (!ContainsPose(shadow.at("poses"), pose.name)) continue;
    const std::string caster_name = shadow.at("caster").get<std::string>();
    const auto caster = std::find_if(
        puppet.parts.begin(), puppet.parts.end(),
        [&caster_name](const zebes::LayeredPuppetPart& part) { return part.name == caster_name; });
    if (caster == puppet.parts.end()) {
      return absl::InvalidArgumentError("puppet shadow references an unknown caster");
    }
    const size_t caster_index = static_cast<size_t>(caster - puppet.parts.begin());
    ASSIGN_OR_RETURN(const zebes::RgbaImage caster_image,
                     zebes::RenderLayeredPuppetPart(puppet, pose, caster_index));
    zebes::RgbaImage receiver = EmptyCanvasLike(composite);
    for (const nlohmann::json& receiver_name_json : shadow.at("receivers")) {
      const std::string receiver_name = receiver_name_json.get<std::string>();
      const auto found = std::find_if(puppet.parts.begin(), puppet.parts.end(),
                                      [&receiver_name](const zebes::LayeredPuppetPart& part) {
                                        return part.name == receiver_name;
                                      });
      if (found == puppet.parts.end()) {
        return absl::InvalidArgumentError("puppet shadow references an unknown receiver");
      }
      const size_t receiver_index = static_cast<size_t>(found - puppet.parts.begin());
      ASSIGN_OR_RETURN(const zebes::RgbaImage rendered,
                       zebes::RenderLayeredPuppetPart(puppet, pose, receiver_index));
      for (size_t offset = 3; offset < receiver.pixels.size(); offset += 4) {
        receiver.pixels[offset] = std::max(receiver.pixels[offset], rendered.pixels[offset]);
      }
    }
    const nlohmann::json& offset = shadow.at("offset");
    if (!offset.is_array() || offset.size() != 2) {
      return absl::InvalidArgumentError("puppet shadow offset must contain x and y");
    }
    const int opacity = shadow.at("opacity").get<int>();
    if (opacity <= 0 || opacity > 255) {
      return absl::InvalidArgumentError("puppet shadow opacity must be in [1, 255]");
    }
    RETURN_IF_ERROR(zebes::ApplyLayeredPuppetShadow(composite, caster_image, receiver,
                                                    {.offset_x = offset.at(0).get<int>(),
                                                     .offset_y = offset.at(1).get<int>(),
                                                     .spread = shadow.value("spread", 0),
                                                     .opacity = static_cast<uint8_t>(opacity)}));
  }
  return absl::OkStatus();
}

zebes::RgbaImage TintDifference(const zebes::RgbaImage& composite,
                                const zebes::RgbaImage& without_part) {
  zebes::RgbaImage tinted = composite;
  for (size_t offset = 0; offset < tinted.pixels.size(); offset += 4) {
    const bool differs = !std::equal(composite.pixels.begin() + static_cast<ptrdiff_t>(offset),
                                     composite.pixels.begin() + static_cast<ptrdiff_t>(offset + 4),
                                     without_part.pixels.begin() + static_cast<ptrdiff_t>(offset));
    if (!differs || composite.pixels[offset + 3] == 0) continue;
    tinted.pixels[offset] = 255;
    tinted.pixels[offset + 1] = 0;
    tinted.pixels[offset + 2] = 255;
  }
  return tinted;
}

int Run() {
  const std::string source_path = absl::GetFlag(FLAGS_source);
  const std::string spec_path = absl::GetFlag(FLAGS_spec);
  const std::filesystem::path semantic_root = absl::GetFlag(FLAGS_semantic_root);
  const std::filesystem::path output = absl::GetFlag(FLAGS_output);
  if (source_path.empty() || spec_path.empty() || output.empty()) {
    std::cerr << "--source, --spec, and --output are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> source = zebes::ReadPng(source_path);
  const absl::StatusOr<nlohmann::json> spec = ReadJson(spec_path, "layered puppet spec");
  if (!source.ok() || !spec.ok()) {
    const absl::Status status = !source.ok() ? source.status() : spec.status();
    std::cerr << status.message() << '\n';
    return 1;
  }

  nlohmann::json semantic_metadata;
  const nlohmann::json* semantic_metadata_pointer = nullptr;
  if (!semantic_root.empty()) {
    const absl::StatusOr<nlohmann::json> loaded =
        ReadJson((semantic_root / "info.json").string(), "semantic layer metadata");
    if (!loaded.ok()) {
      std::cerr << loaded.status().message() << '\n';
      return 1;
    }
    semantic_metadata = *loaded;
    semantic_metadata_pointer = &semantic_metadata;
  }

  try {
    nlohmann::json stretch_reports = nlohmann::json::object();
    absl::flat_hash_map<std::string, zebes::RgbaImage> backfill_requirements;
    absl::flat_hash_map<std::string, zebes::RgbaImage> attachment_relationships;
    absl::flat_hash_map<std::string, zebes::RgbaImage> immutable_semantic_layers;
    absl::StatusOr<zebes::LayeredPuppet> puppet =
        ParsePuppet(*source, *spec, semantic_root, semantic_metadata_pointer, stretch_reports,
                    backfill_requirements, attachment_relationships, immutable_semantic_layers);
    if (!puppet.ok()) {
      std::cerr << puppet.status().message() << '\n';
      return 1;
    }
    const std::filesystem::path parts_directory = output / "parts";
    const std::filesystem::path working_directory = output / "working";
    const std::filesystem::path frames_directory = output / "frames";
    const std::filesystem::path part_poses_directory = output / "part-poses";
    const std::filesystem::path attachment_directory = output / "attachment-masks";
    const std::filesystem::path backfill_directory = output / "backfill-masks";
    const std::filesystem::path immutable_directory = output / "immutable-sources";
    const std::filesystem::path diagnostics_working_directory = output / "diagnostics" / "working";
    const std::filesystem::path diagnostics_frames_directory = output / "diagnostics" / "frames";
    std::error_code error;
    std::filesystem::create_directories(parts_directory, error);
    std::filesystem::create_directories(working_directory, error);
    std::filesystem::create_directories(frames_directory, error);
    std::filesystem::create_directories(part_poses_directory, error);
    std::filesystem::create_directories(attachment_directory, error);
    std::filesystem::create_directories(backfill_directory, error);
    std::filesystem::create_directories(immutable_directory, error);
    std::filesystem::create_directories(diagnostics_working_directory, error);
    std::filesystem::create_directories(diagnostics_frames_directory, error);
    if (error) {
      std::cerr << "could not create layered puppet evidence directories: " << error.message()
                << '\n';
      return 1;
    }

    nlohmann::json part_pixel_counts = nlohmann::json::object();
    for (const zebes::LayeredPuppetPart& part : puppet->parts) {
      const absl::Status written = WriteImage(parts_directory / (part.name + ".png"), part.artwork);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      part_pixel_counts[part.name] = OpaquePixelCount(part.artwork);
    }
    for (const auto& [part_name, original] : immutable_semantic_layers) {
      const absl::Status written = WriteImage(immutable_directory / (part_name + ".png"), original);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
    }
    nlohmann::json backfill_mask_pixels = nlohmann::json::object();
    nlohmann::json attachment_mask_pixels = nlohmann::json::object();
    for (const auto& [part_name, requirement] : backfill_requirements) {
      const absl::Status written =
          WriteImage(backfill_directory / (part_name + ".png"), requirement);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      backfill_mask_pixels[part_name] = OpaquePixelCount(requirement);
    }
    for (const auto& [part_name, relationship] : attachment_relationships) {
      const absl::Status written =
          WriteImage(attachment_directory / (part_name + ".png"), relationship);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      attachment_mask_pixels[part_name] = OpaquePixelCount(relationship);
    }

    nlohmann::json skinned_part_diagnostics = nlohmann::json::object();
    nlohmann::json validation_errors = nlohmann::json::array();
    nlohmann::json immutable_reports = nlohmann::json::object();
    for (const auto& [part_name, original] : immutable_semantic_layers) {
      const auto current = std::find_if(
          puppet->parts.begin(), puppet->parts.end(),
          [&part_name](const zebes::LayeredPuppetPart& part) { return part.name == part_name; });
      if (current == puppet->parts.end()) {
        validation_errors.push_back("immutable semantic layer is missing from the puppet");
        continue;
      }
      const absl::StatusOr<zebes::SemanticLayerMutation> mutation =
          zebes::MeasureSemanticLayerMutation(original, current->artwork);
      if (!mutation.ok()) {
        std::cerr << mutation.status().message() << '\n';
        return 1;
      }
      const absl::StatusOr<std::string> source_digest = zebes::RgbaImageDigest(original);
      const absl::StatusOr<std::string> final_digest = zebes::RgbaImageDigest(current->artwork);
      if (!source_digest.ok() || !final_digest.ok()) {
        const absl::Status status =
            !source_digest.ok() ? source_digest.status() : final_digest.status();
        std::cerr << status.message() << '\n';
        return 1;
      }
      if (mutation->changed_pixels != 0 || mutation->alpha_added_pixels != 0 ||
          mutation->alpha_removed_pixels != 0) {
        validation_errors.push_back(part_name + " changed immutable semantic artwork");
      }
      immutable_reports[part_name] = {
          {"source_rgba_digest", *source_digest},
          {"final_rgba_digest", *final_digest},
          {"changed_pixels", mutation->changed_pixels},
          {"alpha_added_pixels", mutation->alpha_added_pixels},
          {"alpha_removed_pixels", mutation->alpha_removed_pixels},
      };
    }
    std::vector<zebes::RgbaImage> visible_layers;
    visible_layers.reserve(puppet->parts.size());
    for (const zebes::LayeredPuppetPart& part : puppet->parts) {
      visible_layers.push_back(part.visible_artwork);
    }
    absl::StatusOr<zebes::SemanticVisibleOwnership> ownership =
        zebes::MeasureSemanticVisibleOwnership(*source, visible_layers);
    if (!ownership.ok()) {
      std::cerr << ownership.status().message() << '\n';
      return 1;
    }
    if (spec->value("require_exclusive_visible_ownership", false) &&
        (ownership->unowned_pixels != 0 || ownership->multiply_owned_pixels != 0 ||
         ownership->ownership_outside_source_pixels != 0)) {
      validation_errors.push_back("source-visible pixels do not have exclusive complete ownership");
    }
    const bool require_backfill_coverage = spec->value("require_backfill_coverage", false);
    const bool require_part_ownership_isolation =
        spec->value("require_part_ownership_isolation", false);
    const bool require_no_triangle_inversion = spec->value("require_no_triangle_inversion", false);
    std::vector<size_t> skinned_part_indices;
    for (size_t part_index = 0; part_index < puppet->parts.size(); ++part_index) {
      const zebes::LayeredPuppetPart& part = puppet->parts[part_index];
      if (part.bone_indices.size() != 2) continue;
      const size_t source_opaque_pixels = OpaquePixelCount(part.artwork);
      skinned_part_indices.push_back(part_index);

      std::vector<zebes::RgbaImage> static_layers;
      nlohmann::json orphan_diagnostics = nlohmann::json::object();
      for (size_t other = 0; other < puppet->parts.size(); ++other) {
        if (other == part_index) continue;
        const zebes::LayeredPuppetPart& static_part = puppet->parts[other];
        static_layers.push_back(static_part.artwork);
        absl::StatusOr<zebes::LayeredPuppetOrphanReport> orphans =
            zebes::MeasureLayeredPuppetOrphans(static_part.artwork, part.artwork);
        if (!orphans.ok()) {
          std::cerr << orphans.status().message() << '\n';
          return 1;
        }
        if (require_part_ownership_isolation && orphans->orphan_pixels != 0) {
          validation_errors.push_back(static_part.name + " retains " +
                                      std::to_string(orphans->orphan_pixels) +
                                      " orphan pixels inside " + part.name);
        }
        orphan_diagnostics[static_part.name] = {
            {"components", orphans->components},
            {"orphan_pixels", orphans->orphan_pixels},
        };
      }
      const auto required_backfill = backfill_requirements.find(part.name);
      const zebes::RgbaImage& backfill_mask = required_backfill == backfill_requirements.end()
                                                  ? part.artwork
                                                  : required_backfill->second;
      absl::StatusOr<zebes::LayeredPuppetBackfillReport> backfill =
          zebes::MeasureLayeredPuppetBackfill(backfill_mask, static_layers);
      if (!backfill.ok()) {
        std::cerr << backfill.status().message() << '\n';
        return 1;
      }
      if (require_backfill_coverage && backfill->uncovered_pixels != 0) {
        validation_errors.push_back(part.name + " requires " +
                                    std::to_string(backfill->uncovered_pixels) +
                                    " static attachment pixels that no layer paints");
      }

      const std::filesystem::path part_directory = part_poses_directory / part.name;
      std::filesystem::create_directories(part_directory, error);
      if (error) {
        std::cerr << "could not create skinned-part evidence directory: " << error.message()
                  << '\n';
        return 1;
      }
      nlohmann::json pose_diagnostics = nlohmann::json::object();
      for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
        absl::StatusOr<zebes::RgbaImage> rendered =
            zebes::RenderLayeredPuppetPart(*puppet, pose, part_index);
        if (!rendered.ok()) {
          std::cerr << rendered.status().message() << '\n';
          return 1;
        }
        const absl::Status written = WriteImage(part_directory / (pose.name + ".png"), *rendered);
        if (!written.ok()) {
          std::cerr << written.message() << '\n';
          return 1;
        }
        absl::StatusOr<std::vector<zebes::SemanticLayerComponent>> components =
            zebes::SplitSemanticLayerComponents(*rendered);
        if (!components.ok()) {
          std::cerr << components.status().message() << '\n';
          return 1;
        }
        const size_t rendered_opaque_pixels = OpaquePixelCount(*rendered);
        const size_t reconstruction_difference =
            pose.name == "neutral" ? PixelDifferenceCount(part.artwork, *rendered) : 0;
        if (pose.name == "neutral" && reconstruction_difference != 0) {
          validation_errors.push_back(part.name + " neutral reconstruction changed source pixels");
        }
        if (components->size() != 1) {
          validation_errors.push_back(part.name + " " + pose.name +
                                      " is not one connected component");
        }
        const absl::StatusOr<std::string> digest = zebes::RgbaImageDigest(*rendered);
        if (!digest.ok()) {
          std::cerr << digest.status().message() << '\n';
          return 1;
        }
        absl::StatusOr<std::vector<zebes::ProfileControlPoint>> deformed =
            zebes::SolveLayeredPuppetMeshVertices(*puppet, pose, part_index);
        if (!deformed.ok()) {
          std::cerr << deformed.status().message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::LayeredPuppetTriangleReport> triangles =
            zebes::MeasureLayeredPuppetTriangles(part.mesh, *deformed, part.artwork);
        if (!triangles.ok()) {
          std::cerr << triangles.status().message() << '\n';
          return 1;
        }
        if (require_no_triangle_inversion &&
            (triangles->inverted_over_artwork != 0 || triangles->degenerate != 0)) {
          validation_errors.push_back(part.name + " " + pose.name + " folds " +
                                      std::to_string(triangles->inverted_over_artwork) +
                                      " triangles over artwork");
        }
        pose_diagnostics[pose.name] = {
            {"components", components->size()},
            {"opaque_pixels", rendered_opaque_pixels},
            {"retained_area_ratio",
             static_cast<double>(rendered_opaque_pixels) / source_opaque_pixels},
            {"neutral_reconstruction_difference", reconstruction_difference},
            {"inverted_triangles", triangles->inverted},
            {"inverted_triangles_over_artwork", triangles->inverted_over_artwork},
            {"degenerate_triangles", triangles->degenerate},
            {"rgba_digest", *digest},
        };
      }
      skinned_part_diagnostics[part.name] = {
          {"mesh_vertices", part.mesh.vertices.size()},
          {"mesh_triangles", part.mesh.triangles.size()},
          {"backfill",
           {{"moving_pixels", backfill->moving_pixels},
            {"uncovered_pixels", backfill->uncovered_pixels}}},
          {"orphans", std::move(orphan_diagnostics)},
          {"poses", std::move(pose_diagnostics)},
      };
    }

    std::vector<zebes::RgbaImage> frames;
    nlohmann::json frame_digests = nlohmann::json::object();
    nlohmann::json interior_holes = nlohmann::json::object();
    const bool require_no_interior_holes = spec->value("require_no_interior_holes", false);
    size_t neutral_composite_difference = 0;
    nlohmann::json shadow_changed_pixels = nlohmann::json::object();
    for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
      absl::StatusOr<zebes::RgbaImage> working = zebes::RenderLayeredPuppetPose(*puppet, pose);
      if (!working.ok()) {
        std::cerr << working.status().message() << '\n';
        return 1;
      }
      const zebes::RgbaImage unshadowed = *working;
      const absl::Status shadowed = ApplyConfiguredShadows(*working, *puppet, pose, *spec);
      if (!shadowed.ok()) {
        std::cerr << shadowed.message() << '\n';
        return 1;
      }
      const size_t shadow_pixels = PixelDifferenceCount(unshadowed, *working);
      shadow_changed_pixels[pose.name] = shadow_pixels;
      if (shadow_pixels != 0) {
        const zebes::RgbaImage shadow_tint = TintDifference(*working, unshadowed);
        const absl::Status shadow_written = WriteImage(
            diagnostics_working_directory / (pose.name + "-shadow-tint.png"), shadow_tint);
        if (!shadow_written.ok()) {
          std::cerr << shadow_written.message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::RgbaImage> shadow_frame =
            zebes::DownsampleLayeredPuppetFrame(shadow_tint, absl::GetFlag(FLAGS_frame_size));
        if (!shadow_frame.ok()) {
          std::cerr << shadow_frame.status().message() << '\n';
          return 1;
        }
        const absl::Status shadow_frame_written = WriteImage(
            diagnostics_frames_directory / (pose.name + "-shadow-tint.png"), *shadow_frame);
        if (!shadow_frame_written.ok()) {
          std::cerr << shadow_frame_written.message() << '\n';
          return 1;
        }
      }
      for (const size_t part_index : skinned_part_indices) {
        const std::string& part_name = puppet->parts[part_index].name;
        absl::StatusOr<zebes::RgbaImage> hidden =
            zebes::RenderLayeredPuppetPoseWithoutPart(*puppet, pose, part_index);
        if (!hidden.ok()) {
          std::cerr << hidden.status().message() << '\n';
          return 1;
        }
        const zebes::RgbaImage tinted = TintDifference(*working, *hidden);
        const std::string basename = pose.name + "-" + part_name;
        const absl::Status hidden_written =
            WriteImage(diagnostics_working_directory / (basename + "-hidden.png"), *hidden);
        const absl::Status tint_written =
            WriteImage(diagnostics_working_directory / (basename + "-tint.png"), tinted);
        if (!hidden_written.ok() || !tint_written.ok()) {
          const absl::Status status = !hidden_written.ok() ? hidden_written : tint_written;
          std::cerr << status.message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::RgbaImage> hidden_frame =
            zebes::DownsampleLayeredPuppetFrame(*hidden, absl::GetFlag(FLAGS_frame_size));
        absl::StatusOr<zebes::RgbaImage> tint_frame =
            zebes::DownsampleLayeredPuppetFrame(tinted, absl::GetFlag(FLAGS_frame_size));
        if (!hidden_frame.ok() || !tint_frame.ok()) {
          const absl::Status status =
              !hidden_frame.ok() ? hidden_frame.status() : tint_frame.status();
          std::cerr << status.message() << '\n';
          return 1;
        }
        const absl::Status hidden_frame_written =
            WriteImage(diagnostics_frames_directory / (basename + "-hidden.png"), *hidden_frame);
        const absl::Status tint_frame_written =
            WriteImage(diagnostics_frames_directory / (basename + "-tint.png"), *tint_frame);
        if (!hidden_frame_written.ok() || !tint_frame_written.ok()) {
          const absl::Status status =
              !hidden_frame_written.ok() ? hidden_frame_written : tint_frame_written;
          std::cerr << status.message() << '\n';
          return 1;
        }
      }
      const absl::Status working_written =
          WriteImage(working_directory / (pose.name + ".png"), *working);
      if (!working_written.ok()) {
        std::cerr << working_written.message() << '\n';
        return 1;
      }
      if (pose.name == "neutral") {
        neutral_composite_difference = PixelDifferenceCount(*source, *working);
        if (spec->value("require_exact_neutral_composite", false) &&
            neutral_composite_difference != 0) {
          validation_errors.push_back("neutral composite changed approved source pixels");
        }
      }
      absl::StatusOr<size_t> holes = zebes::MeasureLayeredPuppetInteriorHoles(*working);
      if (!holes.ok()) {
        std::cerr << holes.status().message() << '\n';
        return 1;
      }
      if (require_no_interior_holes && *holes != 0) {
        validation_errors.push_back(pose.name + " composite has " + std::to_string(*holes) +
                                    " interior holes");
      }
      interior_holes[pose.name] = *holes;
      absl::StatusOr<zebes::RgbaImage> frame =
          zebes::DownsampleLayeredPuppetFrame(*working, absl::GetFlag(FLAGS_frame_size));
      if (!frame.ok()) {
        std::cerr << frame.status().message() << '\n';
        return 1;
      }
      const absl::Status frame_written =
          WriteImage(frames_directory / (pose.name + ".png"), *frame);
      if (!frame_written.ok()) {
        std::cerr << frame_written.message() << '\n';
        return 1;
      }
      const absl::StatusOr<std::string> digest = zebes::RgbaImageDigest(*frame);
      if (!digest.ok()) {
        std::cerr << digest.status().message() << '\n';
        return 1;
      }
      frame_digests[pose.name] = *digest;
      frames.push_back(std::move(*frame));
    }

    const absl::StatusOr<zebes::RgbaImage> proof = zebes::PackLayeredPuppetFrames(frames);
    if (!proof.ok()) {
      std::cerr << proof.status().message() << '\n';
      return 1;
    }
    const absl::Status proof_written = WriteImage(output / "proof.png", *proof);
    if (!proof_written.ok()) {
      std::cerr << proof_written.message() << '\n';
      return 1;
    }
    const absl::StatusOr<zebes::RgbaImage> zoomed =
        zebes::ZoomLayeredPuppetEvidence(*proof, absl::GetFlag(FLAGS_zoom));
    if (!zoomed.ok()) {
      std::cerr << zoomed.status().message() << '\n';
      return 1;
    }
    const absl::Status zoom_written = WriteImage(output / "proof-zoom.png", *zoomed);
    if (!zoom_written.ok()) {
      std::cerr << zoom_written.message() << '\n';
      return 1;
    }

    const absl::StatusOr<std::string> source_digest = zebes::RgbaImageDigest(*source);
    if (!source_digest.ok()) {
      std::cerr << source_digest.status().message() << '\n';
      return 1;
    }
    const bool validation_passed = validation_errors.empty();
    const nlohmann::json manifest = {
        {"version", 1},
        {"poses", kPoseNames},
        {"working_size", {puppet->width, puppet->height}},
        {"frame_size", {absl::GetFlag(FLAGS_frame_size), absl::GetFlag(FLAGS_frame_size)}},
        {"frame_digests", std::move(frame_digests)},
        {"part_pixel_counts", std::move(part_pixel_counts)},
        {"source_rgba_digest", *source_digest},
        {"visible_ownership",
         {{"source_pixels", ownership->source_pixels},
          {"singly_owned_pixels", ownership->singly_owned_pixels},
          {"unowned_pixels", ownership->unowned_pixels},
          {"multiply_owned_pixels", ownership->multiply_owned_pixels},
          {"ownership_outside_source_pixels", ownership->ownership_outside_source_pixels}}},
        {"neutral_composite_difference", neutral_composite_difference},
        {"interior_holes", std::move(interior_holes)},
        {"backfill_stretch", std::move(stretch_reports)},
        {"backfill_mask_pixels", std::move(backfill_mask_pixels)},
        {"attachment_mask_pixels", std::move(attachment_mask_pixels)},
        {"shadow_changed_pixels", std::move(shadow_changed_pixels)},
        {"skinned_parts", std::move(skinned_part_diagnostics)},
        {"immutable_semantic_layers", std::move(immutable_reports)},
        {"hard_validation",
         {{"passed", validation_passed}, {"errors", std::move(validation_errors)}}},
    };
    const absl::Status manifest_written = WriteJson(output / "manifest.json", manifest);
    if (!manifest_written.ok()) {
      std::cerr << manifest_written.message() << '\n';
      return 1;
    }
    std::cout << "wrote " << puppet->poses.size() << "-pose layered puppet proof to " << output
              << "; " << puppet->parts.size() << " authored parts\n";
    if (!validation_passed) {
      std::cerr << "layered puppet hard validation failed; see manifest.json\n";
      return 1;
    }
  } catch (const nlohmann::json::exception& error) {
    std::cerr << "invalid layered puppet input: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "unexpected positional arguments\n";
    return 2;
  }
  return Run();
}
