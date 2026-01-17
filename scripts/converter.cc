#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

// STB libraries are header-only C libraries.
// We confine their implementation definitions to this file.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace {

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Calculates Euclidean distance between two colors to check tolerance.
double CalculateColorDistance(const Color& c1, const Color& c2) {
  return std::sqrt(std::pow(c2.r - c1.r, 2) + std::pow(c2.g - c1.g, 2) + std::pow(c2.b - c1.b, 2));
}

}  // namespace

// Converts a JPEG to a PNG with resizing and chroma keying.
// Returns absl::OkStatus() on success, or an error status on failure.
absl::Status ConvertAndResizeImage(const std::string& input_path, const std::string& output_path,
                                   int target_width, int target_height, Color chroma_key,
                                   double tolerance) {
  LOG(INFO) << "Reading input file: " << input_path;

  int width, height, channels;
  // Force load as 3 channels (RGB)
  uint8_t* img_data = stbi_load(input_path.c_str(), &width, &height, &channels, 3);

  if (img_data == nullptr) {
    return absl::InternalError(
        absl::StrFormat("Failed to load image '%s': %s", input_path, stbi_failure_reason()));
  }

  LOG(INFO) << "Original dimensions: " << width << "x" << height;

  // 1. Resize Image
  std::vector<uint8_t> resized_data(target_width * target_height * 3);
  stbir_resize_uint8_linear(img_data, width, height, 0, resized_data.data(), target_width,
                            target_height, 0, STBIR_RGB);

  stbi_image_free(img_data);

  LOG(INFO) << "Resized to: " << target_width << "x" << target_height;

  // 2. Process Transparency (RGB -> RGBA)
  std::vector<uint8_t> final_data(target_width * target_height * 4);
  int replaced_count = 0;

  for (int i = 0; i < target_width * target_height; ++i) {
    uint8_t r = resized_data[i * 3 + 0];
    uint8_t g = resized_data[i * 3 + 1];
    uint8_t b = resized_data[i * 3 + 2];

    final_data[i * 4 + 0] = r;
    final_data[i * 4 + 1] = g;
    final_data[i * 4 + 2] = b;

    Color current_pixel = {r, g, b};

    if (CalculateColorDistance(current_pixel, chroma_key) <= tolerance) {
      final_data[i * 4 + 3] = 0;  // Transparent
      replaced_count++;
    } else {
      final_data[i * 4 + 3] = 255;  // Opaque
    }
  }

  LOG(INFO) << "Processed pixels. Made " << replaced_count << " pixels transparent.";

  // 3. Write PNG
  LOG(INFO) << "Writing output to: " << output_path;
  int success = stbi_write_png(output_path.c_str(), target_width, target_height, 4,
                               final_data.data(), target_width * 4);

  if (!success) {
    return absl::InternalError(absl::StrFormat("Failed to write PNG file to '%s'", output_path));
  }

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  // Ideally, use absl::ParseCommandLine here, but we stick to standard argv
  // to match the previous interface request.
  if (argc < 8) {
    LOG(ERROR) << "Usage: " << argv[0]
               << " <input_jpg> <output_png> <width> <height> <R> <G> <B> [tolerance]";
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  int target_width = std::atoi(argv[3]);
  int target_height = std::atoi(argv[4]);

  Color chroma_key;
  chroma_key.r = static_cast<uint8_t>(std::atoi(argv[5]));
  chroma_key.g = static_cast<uint8_t>(std::atoi(argv[6]));
  chroma_key.b = static_cast<uint8_t>(std::atoi(argv[7]));

  double tolerance = (argc >= 9) ? std::atof(argv[8]) : 30.0;

  absl::Status status = ConvertAndResizeImage(input_file, output_file, target_width, target_height,
                                              chroma_key, tolerance);

  if (!status.ok()) {
    LOG(ERROR) << "Operation failed: " << status.message();
    return 1;
  }

  LOG(INFO) << "Conversion successful.";
  return 0;
}