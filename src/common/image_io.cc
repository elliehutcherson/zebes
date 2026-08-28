#include "common/image_io.h"

#include <filesystem>
#include <limits>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"

// stb is header-only, and this is the one translation unit that owns PNG
// coding. Tools used to carry their own copies of these implementations, which
// meant several near-identical readers and writers to keep in step.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace zebes {
namespace {

void AppendEncodedBytes(void* context, void* data, int size) {
  auto* output = static_cast<std::vector<uint8_t>*>(context);
  const auto* bytes = static_cast<const uint8_t*>(data);
  output->insert(output->end(), bytes, bytes + size);
}

absl::Status ValidateRgbaPixels(int width, int height, absl::Span<const uint8_t> pixels,
                                std::string_view operation) {
  if (width <= 0 || height <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("cannot ", operation, " a ", width, "x", height, " image"));
  }
  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (pixels.size() != expected) {
    return absl::InvalidArgumentError(absl::StrCat("expected ", expected, " bytes for a ", width,
                                                   "x", height, " RGBA image, got ",
                                                   pixels.size()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status WritePng(const std::string& path, int width, int height,
                      absl::Span<const uint8_t> pixels) {
  const absl::Status valid = ValidateRgbaPixels(width, height, pixels, "write");
  if (!valid.ok()) return valid;

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

absl::StatusOr<std::vector<uint8_t>> EncodePng(const RgbaImage& image) {
  const absl::Status valid = ValidateRgbaPixels(image.width, image.height, image.pixels, "encode");
  if (!valid.ok()) return valid;
  std::vector<uint8_t> output;
  if (stbi_write_png_to_func(AppendEncodedBytes, &output, image.width, image.height, 4,
                             image.pixels.data(), image.width * 4) == 0 ||
      output.empty()) {
    return absl::InternalError("failed to encode PNG");
  }
  return output;
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

absl::StatusOr<RgbaImage> DecodeImage(absl::Span<const uint8_t> bytes, int64_t maximum_pixels) {
  if (maximum_pixels <= 0) {
    return absl::InvalidArgumentError("image decode pixel limit must be positive");
  }
  if (bytes.empty()) return absl::DataLossError("image bytes are empty");
  if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return absl::OutOfRangeError("image bytes exceed the decoder's supported size");
  }

  // Header first: measuring by decoding would allocate what the limit exists
  // to prevent.
  int width = 0;
  int height = 0;
  int channels_in_file = 0;
  if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                            &channels_in_file) == 0) {
    return absl::DataLossError(
        absl::StrCat("image bytes are not a supported image: ", stbi_failure_reason()));
  }
  if (width <= 0 || height <= 0) {
    return absl::DataLossError(
        absl::StrCat("image bytes describe an unusable ", width, "x", height, " image"));
  }
  if (static_cast<int64_t>(width) * height > maximum_pixels) {
    return absl::ResourceExhaustedError(absl::StrCat(
        "image is ", width, "x", height, ", which exceeds the ", maximum_pixels, " pixel limit"));
  }

  uint8_t* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                        &height, &channels_in_file, 4);
  if (data == nullptr) {
    return absl::DataLossError(
        absl::StrCat("failed to decode image bytes: ", stbi_failure_reason()));
  }
  absl::Cleanup free_data = [data] { stbi_image_free(data); };

  RgbaImage image;
  image.width = width;
  image.height = height;
  image.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);

  if (!image.IsValid()) {
    return absl::DataLossError(absl::StrCat("decoded an unusable ", width, "x", height, " image"));
  }
  return image;
}

}  // namespace zebes
