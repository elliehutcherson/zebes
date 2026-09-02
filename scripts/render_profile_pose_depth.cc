#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, layers, "", "Posed 1-based layer-ID PNG.");
ABSL_FLAG(std::string, binding, "", "Character-binding JSON manifest.");
ABSL_FLAG(std::string, pose, "", "Pose key in the binding manifest.");
ABSL_FLAG(std::string, output, "", "Destination ordinal-depth PNG.");

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
  const std::string layers_path = absl::GetFlag(FLAGS_layers);
  const std::string binding_path = absl::GetFlag(FLAGS_binding);
  const std::string pose_name = absl::GetFlag(FLAGS_pose);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  if (layers_path.empty() || binding_path.empty() || pose_name.empty() || output_path.empty()) {
    std::cerr << "--layers, --binding, --pose, and --output are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> layers_image = zebes::ReadPng(layers_path);
  if (!layers_image.ok()) {
    std::cerr << "could not read posed layers: " << layers_image.status().message() << "\n";
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
    if (layers_image->width != width || layers_image->height != height) {
      std::cerr << "posed layer dimensions do not match binding manifest\n";
      return 1;
    }
    const nlohmann::json& pose_depths = manifest->at("depths").at(pose_name);
    if (!pose_depths.is_object()) {
      std::cerr << "binding depths must map bone names to grayscale values\n";
      return 1;
    }

    std::vector<uint8_t> depth_by_layer;
    for (const nlohmann::json& bone : manifest->at("bones")) {
      const std::string name = bone.at("name").get<std::string>();
      const int value = pose_depths.at(name).get<int>();
      if (value < 1 || value > 255) {
        std::cerr << "depth value for " << name << " must be within [1, 255]\n";
        return 1;
      }
      depth_by_layer.push_back(static_cast<uint8_t>(value));
    }

    std::vector<uint8_t> layer_ids(static_cast<size_t>(width) * height, 0);
    for (size_t index = 0; index < layer_ids.size(); ++index) {
      layer_ids[index] = layers_image->pixels[index * 4];
    }
    const absl::StatusOr<zebes::RgbaImage> depth =
        zebes::RenderProfileOrdinalDepth(layer_ids, width, height, depth_by_layer);
    if (!depth.ok()) {
      std::cerr << "could not render ordinal depth: " << depth.status().message() << "\n";
      return 1;
    }
    const absl::Status written =
        zebes::WritePng(output_path, depth->width, depth->height, depth->pixels);
    if (!written.ok()) {
      std::cerr << "could not write ordinal depth: " << written.message() << "\n";
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
