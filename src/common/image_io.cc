#include "common/image_io.h"

#include <filesystem>

#include "absl/strings/str_cat.h"

// stb is header-only, and this is the one translation unit that owns PNG
// coding. Tools used to carry their own copies of these implementations, which
// meant several near-identical readers and writers to keep in step.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace zebes {

absl::Status WritePng(const std::string& path, int width, int height,
                      absl::Span<const uint8_t> pixels) {
  if (width <= 0 || height <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("cannot write a ", width, "x", height, " image"));
  }
  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (pixels.size() != expected) {
    return absl::InvalidArgumentError(absl::StrCat("expected ", expected, " bytes for a ", width,
                                                   "x", height, " RGBA image, got ",
                                                   pixels.size()));
  }

  const std::filesystem::path target(path);
  if (target.has_parent_path()) {
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
      return absl::InternalError(
          absl::StrCat("failed to create ", target.parent_path().string(), ": ", error.message()));
    }
  }

  if (stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) == 0) {
    return absl::InternalError(absl::StrCat("failed to write PNG: ", path));
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> ReadPng(const std::string& path) {
  int width = 0;
  int height = 0;
  int channels_in_file = 0;
  // The trailing 4 asks stb for RGBA regardless of what the file holds, so a
  // caller never has to branch on channel count.
  uint8_t* data = stbi_load(path.c_str(), &width, &height, &channels_in_file, 4);
  if (data == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("failed to read image ", path, ": ", stbi_failure_reason()));
  }

  RgbaImage image;
  image.width = width;
  image.height = height;
  image.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
  stbi_image_free(data);

  if (!image.IsValid()) {
    return absl::DataLossError(
        absl::StrCat("decoded ", path, " to an unusable ", width, "x", height, " image"));
  }
  return image;
}

}  // namespace zebes
