#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/profile_deformation.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, source, "", "Isolated source-color PNG at binding size.");
ABSL_FLAG(std::string, source_layers, "", "Neutral 1-based layer-ID PNG.");
ABSL_FLAG(std::string, target_layers, "", "Target-pose 1-based layer-ID PNG.");
ABSL_FLAG(std::string, binding, "", "Character-binding JSON manifest.");
ABSL_FLAG(std::string, pose, "", "Target pose key in the binding manifest.");
ABSL_FLAG(std::string, output, "", "Destination deformed RGBA PNG.");
ABSL_FLAG(double, joint_blend_radius, 12.0,
          "Radius in binding pixels for blending incident bone transforms.");
ABSL_FLAG(int, maximum_source_search_radius, 16, "Maximum same-layer source-pixel search radius.");

namespace {

absl::StatusOr<nlohmann::json> ReadJson(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError("could not open character-binding manifest");
  }
  try {
    return nlohmann::json::parse(stream);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError(std::string("invalid character-binding manifest: ") + error.what());
  }
}

absl::StatusOr<std::vector<uint8_t>> ReadLayers(const std::string& path, int width, int height) {
  const absl::StatusOr<zebes::RgbaImage> image = zebes::ReadPng(path);
  if (!image.ok()) return image.status();
  if (image->width != width || image->height != height) {
    return absl::InvalidArgumentError(
        "profile deformation layer image dimensions do not match binding");
  }
  std::vector<uint8_t> layers(static_cast<size_t>(width) * height, 0);
  for (size_t index = 0; index < layers.size(); ++index) {
    layers[index] = image->pixels[index * 4];
  }
  return layers;
}

int Run() {
  const std::string source_path = absl::GetFlag(FLAGS_source);
  const std::string source_layers_path = absl::GetFlag(FLAGS_source_layers);
  const std::string target_layers_path = absl::GetFlag(FLAGS_target_layers);
  const std::string binding_path = absl::GetFlag(FLAGS_binding);
  const std::string pose_name = absl::GetFlag(FLAGS_pose);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  if (source_path.empty() || source_layers_path.empty() || target_layers_path.empty() ||
      binding_path.empty() || pose_name.empty() || output_path.empty()) {
    std::cerr << "--source, --source_layers, --target_layers, --binding, --pose, "
                 "and --output are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> source = zebes::ReadPng(source_path);
  if (!source.ok()) {
    std::cerr << "could not read source image: " << source.status().message() << "\n";
    return 1;
  }
  const absl::StatusOr<nlohmann::json> manifest = ReadJson(binding_path);
  if (!manifest.ok()) {
    std::cerr << manifest.status().message() << "\n";
    return 1;
  }

  try {
    const int width = manifest->at("width").get<int>();
    const int height = manifest->at("height").get<int>();
    if (source->width != width || source->height != height) {
      std::cerr << "source dimensions do not match binding manifest\n";
      return 1;
    }
    const absl::StatusOr<std::vector<uint8_t>> source_layers =
        ReadLayers(source_layers_path, width, height);
    const absl::StatusOr<std::vector<uint8_t>> target_layers =
        ReadLayers(target_layers_path, width, height);
    if (!source_layers.ok() || !target_layers.ok()) {
      const absl::Status status =
          !source_layers.ok() ? source_layers.status() : target_layers.status();
      std::cerr << "could not read deformation layers: " << status.message() << "\n";
      return 1;
    }

    const nlohmann::json& source_joint_json = manifest->at("joints");
    const nlohmann::json& target_joint_json = manifest->at("poses").at(pose_name);
    std::vector<zebes::ProfileControlPoint> source_joints;
    std::vector<zebes::ProfileControlPoint> target_joints;
    std::unordered_map<std::string, size_t> joint_indices;
    for (const auto& [name, coordinates] : source_joint_json.items()) {
      const nlohmann::json& target_coordinates = target_joint_json.at(name);
      if (!coordinates.is_array() || coordinates.size() != 2 || !target_coordinates.is_array() ||
          target_coordinates.size() != 2) {
        std::cerr << "joint " << name << " must contain source and target x/y\n";
        return 1;
      }
      joint_indices.emplace(name, source_joints.size());
      source_joints.push_back(
          {.x = coordinates.at(0).get<double>(), .y = coordinates.at(1).get<double>()});
      target_joints.push_back({.x = target_coordinates.at(0).get<double>(),
                               .y = target_coordinates.at(1).get<double>()});
    }

    std::vector<zebes::ProfileControlBone> bones;
    for (const nlohmann::json& bone : manifest->at("bones")) {
      const std::string start = bone.at("start").get<std::string>();
      const std::string end = bone.at("end").get<std::string>();
      const auto start_index = joint_indices.find(start);
      const auto end_index = joint_indices.find(end);
      if (start_index == joint_indices.end() || end_index == joint_indices.end()) {
        std::cerr << "bone references an unknown deformation joint\n";
        return 1;
      }
      bones.push_back({.start_joint = start_index->second, .end_joint = end_index->second});
    }

    const zebes::ProfileDeformationConfig config{
        .joint_blend_radius = absl::GetFlag(FLAGS_joint_blend_radius),
        .maximum_source_search_radius = absl::GetFlag(FLAGS_maximum_source_search_radius),
    };
    const absl::StatusOr<zebes::ProfileDeformationResult> result = zebes::DeformProfileArtwork(
        *source, *source_layers, *target_layers, source_joints, target_joints, bones, config);
    if (!result.ok()) {
      std::cerr << "could not deform profile artwork: " << result.status().message() << "\n";
      return 1;
    }
    const absl::Status written = zebes::WritePng(output_path, result->image.width,
                                                 result->image.height, result->image.pixels);
    if (!written.ok()) {
      std::cerr << "could not write deformed profile: " << written.message() << "\n";
      return 1;
    }
    std::cout << "wrote " << output_path << "\n"
              << "mapped pixels: " << result->mapped_pixels << "\n"
              << "unmapped pixels: " << result->unmapped_pixels << "\n";
  } catch (const nlohmann::json::exception& error) {
    std::cerr << "invalid character-binding manifest: " << error.what() << "\n";
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
