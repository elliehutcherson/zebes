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
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, source, "", "Approved working-resolution RGBA source PNG.");
ABSL_FLAG(std::string, spec, "", "Explicit layered puppet JSON specification.");
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

absl::StatusOr<zebes::LayeredPuppet> ParsePuppet(const zebes::RgbaImage& source,
                                                 const nlohmann::json& spec) {
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
    const std::string bone_name = part.at("bone").get<std::string>();
    const auto bone = bone_indices.find(bone_name);
    if (!IsSafePartName(name) || bone == bone_indices.end() || part_indices.contains(name)) {
      return absl::InvalidArgumentError("puppet part name or bone is invalid");
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
    ASSIGN_OR_RETURN(zebes::RgbaImage artwork,
                     zebes::BuildLayeredPuppetPartArtwork(source, source_polygons, fills));
    part_indices.emplace(name, puppet.parts.size());
    puppet.parts.push_back(
        {.name = name, .bone_index = bone->second, .artwork = std::move(artwork)});
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

int Run() {
  const std::string source_path = absl::GetFlag(FLAGS_source);
  const std::string spec_path = absl::GetFlag(FLAGS_spec);
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

  try {
    absl::StatusOr<zebes::LayeredPuppet> puppet = ParsePuppet(*source, *spec);
    if (!puppet.ok()) {
      std::cerr << puppet.status().message() << '\n';
      return 1;
    }
    const std::filesystem::path parts_directory = output / "parts";
    const std::filesystem::path working_directory = output / "working";
    const std::filesystem::path frames_directory = output / "frames";
    std::error_code error;
    std::filesystem::create_directories(parts_directory, error);
    std::filesystem::create_directories(working_directory, error);
    std::filesystem::create_directories(frames_directory, error);
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
      int opaque_pixels = 0;
      for (size_t offset = 3; offset < part.artwork.pixels.size(); offset += 4) {
        if (part.artwork.pixels[offset] != 0) ++opaque_pixels;
      }
      part_pixel_counts[part.name] = opaque_pixels;
    }

    std::vector<zebes::RgbaImage> frames;
    nlohmann::json frame_digests = nlohmann::json::object();
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
    const nlohmann::json manifest = {
        {"version", 1},
        {"poses", kPoseNames},
        {"working_size", {puppet->width, puppet->height}},
        {"frame_size", {absl::GetFlag(FLAGS_frame_size), absl::GetFlag(FLAGS_frame_size)}},
        {"frame_digests", std::move(frame_digests)},
        {"part_pixel_counts", std::move(part_pixel_counts)},
        {"source_rgba_digest", *source_digest},
    };
    const absl::Status manifest_written = WriteJson(output / "manifest.json", manifest);
    if (!manifest_written.ok()) {
      std::cerr << manifest_written.message() << '\n';
      return 1;
    }
    std::cout << "wrote " << puppet->poses.size() << "-pose layered puppet proof to " << output
              << "; " << puppet->parts.size() << " authored parts\n";
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
