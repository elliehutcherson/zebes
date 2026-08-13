#include "common/image_io.h"

#include <filesystem>

#include "absl/strings/str_cat.h"

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

}  // namespace zebes
