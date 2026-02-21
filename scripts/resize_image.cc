#include <cstdlib>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace {

absl::Status Run(int argc, char* argv[]) {
  if (argc != 5) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Usage: %s <input_image> <output_image> <target_width> <target_height>", argv[0]));
  }

  std::string input_path = argv[1];
  std::string output_path = argv[2];
  int target_width = std::atoi(argv[3]);
  int target_height = std::atoi(argv[4]);

  if (target_width <= 0 || target_height <= 0) {
    return absl::InvalidArgumentError("Target width and height must be positive integers.");
  }

  LOG(INFO) << "Reading input file: " << input_path;

  int width, height, channels;
  uint8_t* img_data = stbi_load(input_path.c_str(), &width, &height, &channels, 0);

  if (img_data == nullptr) {
    return absl::InternalError(
        absl::StrFormat("Failed to load image '%s': %s", input_path, stbi_failure_reason()));
  }

  LOG(INFO) << "Original dimensions: " << width << "x" << height << " with " << channels
            << " channels.";

  std::vector<uint8_t> resized_data(target_width * target_height * channels);

  stbir_pixel_layout format = STBIR_RGB;
  if (channels == 1) {
    format = STBIR_1CHANNEL;
  } else if (channels == 2) {
    format = STBIR_2CHANNEL;
  } else if (channels == 3) {
    format = STBIR_RGB;
  } else if (channels == 4) {
    format = STBIR_RGBA;
  } else {
    stbi_image_free(img_data);
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported number of channels: %d", channels));
  }

  stbir_resize_uint8_linear(img_data, width, height, 0, resized_data.data(), target_width,
                            target_height, 0, format);

  stbi_image_free(img_data);

  LOG(INFO) << "Resized to: " << target_width << "x" << target_height;
  LOG(INFO) << "Writing output to: " << output_path;

  int success = 0;
  if (output_path.length() >= 4 && output_path.substr(output_path.length() - 4) == ".png") {
    success = stbi_write_png(output_path.c_str(), target_width, target_height, channels,
                             resized_data.data(), target_width * channels);
  } else if (output_path.length() >= 4 &&
             (output_path.substr(output_path.length() - 4) == ".jpg" ||
              output_path.substr(output_path.length() - 5) == ".jpeg")) {
    success = stbi_write_jpg(output_path.c_str(), target_width, target_height, channels,
                             resized_data.data(), 90);
  } else {
    // Default to png
    success = stbi_write_png(output_path.c_str(), target_width, target_height, channels,
                             resized_data.data(), target_width * channels);
  }

  if (!success) {
    return absl::InternalError(absl::StrFormat("Failed to write image file to '%s'", output_path));
  }

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::Status status = Run(argc, argv);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }

  LOG(INFO) << "Okay, done.";
  return 0;
}