#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "common/status_macros.h"
#include "terrain/terrain_mask.h"

// stb_image_write is a header-only library. Keep its implementation in this
// standalone tool's translation unit.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

using ::zebes::Blob47MaskTable;
using ::zebes::kBlob47Columns;
using ::zebes::kBlob47TileCount;
using ::zebes::kEast;
using ::zebes::kNorth;
using ::zebes::kNorthEast;
using ::zebes::kNorthWest;
using ::zebes::kSouth;
using ::zebes::kSouthEast;
using ::zebes::kSouthWest;
using ::zebes::kWest;

constexpr int kTileSize = 32;
constexpr int kColumnCount = kBlob47Columns;
constexpr int kExpectedMaskCount = kBlob47TileCount;

bool IsSolid(uint8_t mask, int x, int y) {
  constexpr float kHalfTileSize = kTileSize / 2.0f;

  const bool is_left = x < kTileSize / 2;
  const bool is_top = y < kTileSize / 2;
  const float u = is_left ? (x + 0.5f) / kHalfTileSize : (kTileSize - x - 0.5f) / kHalfTileSize;
  const float v = is_top ? (y + 0.5f) / kHalfTileSize : (kTileSize - y - 0.5f) / kHalfTileSize;

  const uint8_t diagonal =
      is_top ? (is_left ? kNorthWest : kNorthEast) : (is_left ? kSouthWest : kSouthEast);
  const uint8_t horizontal_edge = is_left ? kWest : kEast;
  const uint8_t vertical_edge = is_top ? kNorth : kSouth;

  const float diagonal_value = (mask & diagonal) ? 1.0f : 0.0f;
  const float horizontal_value = (mask & horizontal_edge) ? 1.0f : 0.0f;
  const float vertical_value = (mask & vertical_edge) ? 1.0f : 0.0f;
  const float value = diagonal_value * (1 - u) * (1 - v) + vertical_value * u * (1 - v) +
                      horizontal_value * (1 - u) * v + u * v;

  return value >= 0.5f;
}

absl::Status WriteCsv(const std::string& output_path, absl::Span<const uint8_t> masks) {
  std::ofstream csv(output_path);
  if (!csv.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open CSV output: ", output_path));
  }

  csv << "index,neighbor_mask\n";
  for (int index = 0; index < static_cast<int>(masks.size()); ++index) {
    csv << index << ',' << static_cast<int>(masks[index]) << '\n';
  }

  csv.close();
  if (!csv) {
    return absl::InternalError(absl::StrCat("Failed to write CSV output: ", output_path));
  }
  return absl::OkStatus();
}

absl::Status Generate(const std::string& output_prefix) {
  if (output_prefix.empty()) {
    return absl::InvalidArgumentError("Output prefix must not be empty");
  }

  absl::Span<const uint8_t> masks = Blob47MaskTable();
  if (masks.size() != kExpectedMaskCount) {
    return absl::InternalError(absl::StrCat("Expected ", kExpectedMaskCount,
                                            " normalized masks, generated ", masks.size()));
  }

  const int row_count = (static_cast<int>(masks.size()) + kColumnCount - 1) / kColumnCount;
  const int width = kColumnCount * kTileSize;
  const int height = row_count * kTileSize;
  std::vector<uint8_t> rgba(width * height * 4, 0);

  for (int index = 0; index < static_cast<int>(masks.size()); ++index) {
    const int origin_x = (index % kColumnCount) * kTileSize;
    const int origin_y = (index / kColumnCount) * kTileSize;

    for (int y = 0; y < kTileSize; ++y) {
      for (int x = 0; x < kTileSize; ++x) {
        if (!IsSolid(masks[index], x, y)) {
          continue;
        }

        const int pixel = ((origin_y + y) * width + origin_x + x) * 4;
        rgba[pixel + 0] = 255;
        rgba[pixel + 1] = 255;
        rgba[pixel + 2] = 255;
        rgba[pixel + 3] = 255;
      }
    }
  }

  const std::string png_path = absl::StrCat(output_prefix, ".png");
  if (!stbi_write_png(png_path.c_str(), width, height, 4, rgba.data(), width * 4)) {
    return absl::InternalError(absl::StrCat("Failed to write PNG output: ", png_path));
  }

  const std::string csv_path = absl::StrCat(output_prefix, ".csv");
  RETURN_IF_ERROR(WriteCsv(csv_path, masks));

  LOG(INFO) << "Generated " << masks.size() << " masks in " << width << "x" << height
            << " atlas: " << png_path;
  LOG(INFO) << "Wrote mask index: " << csv_path;
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  // Without this absl drops INFO before it reaches stderr, so every progress
  // line this tool has ever logged went nowhere.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  if (argc != 2) {
    LOG(ERROR) << "Usage: " << argv[0] << " <output_prefix>";
    return 1;
  }

  const absl::Status status = Generate(argv[1]);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
