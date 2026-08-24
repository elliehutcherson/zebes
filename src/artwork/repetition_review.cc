#include "artwork/repetition_review.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/status/status.h"

namespace zebes {
namespace {

OpposingEdgeDifference CompareEdges(const RgbaImage& image, bool horizontal) {
  const int pixel_count = horizontal ? image.height : image.width;
  OpposingEdgeDifference result{.pixels_compared = pixel_count};
  int64_t total_difference = 0;

  for (int pixel = 0; pixel < pixel_count; ++pixel) {
    const int first_x = horizontal ? 0 : pixel;
    const int first_y = horizontal ? pixel : 0;
    const int second_x = horizontal ? image.width - 1 : pixel;
    const int second_y = horizontal ? pixel : image.height - 1;
    const size_t first_offset = static_cast<size_t>(first_y * image.width + first_x) * 4;
    const size_t second_offset = static_cast<size_t>(second_y * image.width + second_x) * 4;
    bool exact_match = true;
    for (int channel = 0; channel < 4; ++channel) {
      const int difference = std::abs(static_cast<int>(image.pixels[first_offset + channel]) -
                                      static_cast<int>(image.pixels[second_offset + channel]));
      total_difference += difference;
      result.maximum_channel_difference = std::max(result.maximum_channel_difference, difference);
      exact_match = exact_match && difference == 0;
    }
    if (exact_match) ++result.exact_pixel_matches;
  }

  result.mean_absolute_channel_difference =
      static_cast<double>(total_difference) / (pixel_count * 4);
  return result;
}

}  // namespace

absl::StatusOr<RepetitionDiagnostics> AnalyzeRepetition(const RgbaImage& image) {
  if (!image.IsValid()) {
    return absl::InvalidArgumentError("repetition diagnostics require a valid RGBA image");
  }
  return RepetitionDiagnostics{
      .horizontal = CompareEdges(image, true),
      .vertical = CompareEdges(image, false),
  };
}

absl::StatusOr<RgbaImage> BuildRepetitionPreview(const RgbaImage& image, int copies_x, int copies_y,
                                                 size_t maximum_pixels) {
  if (!image.IsValid()) {
    return absl::InvalidArgumentError("repetition preview requires a valid RGBA image");
  }
  if (copies_x <= 0 || copies_y <= 0 || maximum_pixels == 0) {
    return absl::InvalidArgumentError("repetition preview settings are invalid");
  }
  const int64_t width = static_cast<int64_t>(image.width) * copies_x;
  const int64_t height = static_cast<int64_t>(image.height) * copies_y;
  const int64_t pixels = width * height;
  if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
      pixels <= 0 || static_cast<uint64_t>(pixels) > maximum_pixels) {
    return absl::ResourceExhaustedError("repetition preview exceeds its pixel limit");
  }

  RgbaImage preview{
      .width = static_cast<int>(width),
      .height = static_cast<int>(height),
      .pixels = std::vector<uint8_t>(static_cast<size_t>(pixels) * 4),
  };
  const size_t source_row_bytes = static_cast<size_t>(image.width) * 4;
  for (int copy_y = 0; copy_y < copies_y; ++copy_y) {
    for (int y = 0; y < image.height; ++y) {
      for (int copy_x = 0; copy_x < copies_x; ++copy_x) {
        const size_t source = static_cast<size_t>(y) * source_row_bytes;
        const size_t destination = (static_cast<size_t>(copy_y * image.height + y) * preview.width +
                                    copy_x * image.width) *
                                   4;
        std::copy_n(image.pixels.begin() + static_cast<ptrdiff_t>(source), source_row_bytes,
                    preview.pixels.begin() + static_cast<ptrdiff_t>(destination));
      }
    }
  }
  return preview;
}

}  // namespace zebes
