#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, mask, "", "Binary posed-silhouette PNG.");
ABSL_FLAG(std::string, binding, "", "Character-binding JSON manifest.");
ABSL_FLAG(std::string, pose, "", "Pose key in the binding manifest.");
ABSL_FLAG(std::string, output, "", "Destination binary semantic-control PNG.");

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

int Run() {
  const std::string mask_path = absl::GetFlag(FLAGS_mask);
  const std::string binding_path = absl::GetFlag(FLAGS_binding);
  const std::string pose_name = absl::GetFlag(FLAGS_pose);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  if (mask_path.empty() || binding_path.empty() || pose_name.empty() || output_path.empty()) {
    std::cerr << "--mask, --binding, --pose, and --output are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> mask_image = zebes::ReadPng(mask_path);
  if (!mask_image.ok()) {
    std::cerr << "could not read posed mask: " << mask_image.status().message() << "\n";
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
    if (mask_image->width != width || mask_image->height != height) {
      std::cerr << "posed mask dimensions do not match binding manifest\n";
      return 1;
    }
    const nlohmann::json& pose = manifest->at("poses").at(pose_name);
    if (!pose.is_object()) {
      std::cerr << "binding pose must be an object of named joint coordinates\n";
      return 1;
    }

    std::vector<zebes::ProfileControlPoint> joints;
    std::unordered_map<std::string, size_t> joint_indices;
    for (const auto& [name, coordinates] : pose.items()) {
      if (!coordinates.is_array() || coordinates.size() != 2) {
        std::cerr << "joint " << name << " must contain x and y\n";
        return 1;
      }
      joint_indices.emplace(name, joints.size());
      joints.push_back(
          {.x = coordinates.at(0).get<double>(), .y = coordinates.at(1).get<double>()});
    }

    std::vector<zebes::ProfileControlBone> bones;
    for (const nlohmann::json& bone : manifest->at("bones")) {
      const std::string start = bone.at("start").get<std::string>();
      const std::string end = bone.at("end").get<std::string>();
      const auto start_index = joint_indices.find(start);
      const auto end_index = joint_indices.find(end);
      if (start_index == joint_indices.end() || end_index == joint_indices.end()) {
        std::cerr << "bone references an unknown pose joint\n";
        return 1;
      }
      bones.push_back({.start_joint = start_index->second, .end_joint = end_index->second});
    }

    std::vector<uint8_t> silhouette(static_cast<size_t>(width) * height, 0);
    for (size_t index = 0; index < silhouette.size(); ++index) {
      const size_t pixel = index * 4;
      silhouette[index] = mask_image->pixels[pixel] != 0 || mask_image->pixels[pixel + 1] != 0 ||
                          mask_image->pixels[pixel + 2] != 0;
    }
    const absl::StatusOr<zebes::RgbaImage> control =
        zebes::RenderProfilePoseControl(silhouette, width, height, joints, bones);
    if (!control.ok()) {
      std::cerr << "could not render posed control: " << control.status().message() << "\n";
      return 1;
    }
    const absl::Status written =
        zebes::WritePng(output_path, control->width, control->height, control->pixels);
    if (!written.ok()) {
      std::cerr << "could not write posed control: " << written.message() << "\n";
      return 1;
    }
  } catch (const nlohmann::json::exception& error) {
    std::cerr << "invalid character-binding manifest: " << error.what() << "\n";
    return 1;
  }

  std::cout << "wrote " << output_path << "\n";
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
