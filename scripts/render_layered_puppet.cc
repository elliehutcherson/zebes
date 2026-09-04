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

absl::StatusOr<zebes::RgbaImage> LoadSemanticPart(
    const zebes::RgbaImage& source, const zebes::RgbaImage& visible_artwork, const std::string& tag,
    const std::filesystem::path& root, const nlohmann::json& metadata, bool clip_to_source_alpha) {
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
  const int canvas_height = frame_size.at(0).get<int>();
  const int canvas_width = frame_size.at(1).get<int>();
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
  return zebes::PreserveSemanticVisiblePixels(candidate, source, visible_artwork);
}

absl::StatusOr<zebes::LayeredPuppet> ParsePuppet(const zebes::RgbaImage& source,
                                                 const nlohmann::json& spec,
                                                 const std::filesystem::path& semantic_root,
                                                 const nlohmann::json* semantic_metadata) {
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
    zebes::RgbaImage visible_artwork = EmptyCanvasLike(source);
    if (!source_polygons.empty()) {
      ASSIGN_OR_RETURN(visible_artwork,
                       zebes::BuildLayeredPuppetPartArtwork(source, source_polygons, {}));
      RETURN_IF_ERROR(ClearVisiblePolygons(visible_artwork, source, source_exclusions));
    } else if (!source_exclusions.empty()) {
      return absl::InvalidArgumentError("source exclusions require source ownership polygons");
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
      ASSIGN_OR_RETURN(
          artwork, LoadSemanticPart(source, visible_artwork,
                                    part.at("semantic_tag").get<std::string>(), semantic_root,
                                    *semantic_metadata, part.value("clip_to_source_alpha", false)));
    } else {
      ASSIGN_OR_RETURN(artwork,
                       zebes::BuildLayeredPuppetPartArtwork(source, source_polygons, fills));
      RETURN_IF_ERROR(ClearVisiblePolygons(artwork, source, source_exclusions));
    }
    zebes::LayeredPuppetMesh mesh;
    if (part_bones.size() == 2) {
      ASSIGN_OR_RETURN(
          mesh, zebes::BuildLayeredPuppetMesh(artwork, part_bones, puppet.bones,
                                              puppet.source_joints, part.value("mesh_spacing", 4),
                                              part.value("joint_blend_radius", 12.0)));
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
    absl::StatusOr<zebes::LayeredPuppet> puppet =
        ParsePuppet(*source, *spec, semantic_root, semantic_metadata_pointer);
    if (!puppet.ok()) {
      std::cerr << puppet.status().message() << '\n';
      return 1;
    }
    const std::filesystem::path parts_directory = output / "parts";
    const std::filesystem::path working_directory = output / "working";
    const std::filesystem::path frames_directory = output / "frames";
    const std::filesystem::path part_poses_directory = output / "part-poses";
    std::error_code error;
    std::filesystem::create_directories(parts_directory, error);
    std::filesystem::create_directories(working_directory, error);
    std::filesystem::create_directories(frames_directory, error);
    std::filesystem::create_directories(part_poses_directory, error);
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

    nlohmann::json skinned_part_diagnostics = nlohmann::json::object();
    nlohmann::json validation_errors = nlohmann::json::array();
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
    for (size_t part_index = 0; part_index < puppet->parts.size(); ++part_index) {
      const zebes::LayeredPuppetPart& part = puppet->parts[part_index];
      if (part.bone_indices.size() != 2) continue;
      const size_t source_opaque_pixels = OpaquePixelCount(part.artwork);
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
        pose_diagnostics[pose.name] = {
            {"components", components->size()},
            {"opaque_pixels", rendered_opaque_pixels},
            {"retained_area_ratio",
             static_cast<double>(rendered_opaque_pixels) / source_opaque_pixels},
            {"neutral_reconstruction_difference", reconstruction_difference},
            {"rgba_digest", *digest},
        };
      }
      skinned_part_diagnostics[part.name] = {
          {"mesh_vertices", part.mesh.vertices.size()},
          {"mesh_triangles", part.mesh.triangles.size()},
          {"poses", std::move(pose_diagnostics)},
      };
    }

    std::vector<zebes::RgbaImage> frames;
    nlohmann::json frame_digests = nlohmann::json::object();
    size_t neutral_composite_difference = 0;
    for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
      absl::StatusOr<zebes::RgbaImage> working = zebes::RenderLayeredPuppetPose(*puppet, pose);
      if (!working.ok()) {
        std::cerr << working.status().message() << '\n';
        return 1;
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
        {"skinned_parts", std::move(skinned_part_diagnostics)},
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
