#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "terrain/blob47_compose.h"

namespace {

using ::zebes::Blob47Atlas;
using ::zebes::ComposeBlob47;
using ::zebes::QuadrantSheet;
using ::zebes::ReadPng;
using ::zebes::RgbaImage;
using ::zebes::SeedQuadrantSheetFrom3x3;
using ::zebes::WriteBlob47Manifest;

absl::Status WritePng(const std::string& path, const RgbaImage& image) {
  return zebes::WritePng(path, image.width, image.height, image.pixels);
}

absl::Status WriteTextFile(const std::string& path, const std::string& contents) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open output: ", path));
  }
  file << contents;
  file.close();
  if (!file) {
    return absl::InternalError(absl::StrCat("Failed to write output: ", path));
  }
  return absl::OkStatus();
}

absl::StatusOr<int> ParsePositiveInt(const std::string& text, const std::string& name) {
  int value = 0;
  if (!absl::SimpleAtoi(text, &value) || value <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(name, " must be a positive integer, got ", text));
  }
  return value;
}

// Where `seed` gets the four concave corners a 3x3 block cannot supply. The two
// flags are mutually exclusive because both fill the same four cells.
struct SeedOptions {
  zebes::InnerCornerSeed placeholder = zebes::InnerCornerSeed::kLeaveBlank;
  bool has_ring = false;
  int ring_x = 0;
  int ring_y = 0;
};

absl::StatusOr<SeedOptions> ParseSeedOptions(int argc, char* argv[], int first_flag) {
  SeedOptions options;
  int index = first_flag;
  while (index < argc) {
    const std::string flag = argv[index];
    if (flag == "--placeholder-inner-corners") {
      options.placeholder = zebes::InnerCornerSeed::kPlaceholderFromFill;
      ++index;
      continue;
    }
    if (flag != "--inner-corners") {
      return absl::InvalidArgumentError(absl::StrCat(
          "Unknown flag '", flag, "'; expected --inner-corners or --placeholder-inner-corners"));
    }
    if (index + 2 >= argc) {
      return absl::InvalidArgumentError("--inner-corners requires <ring_x> <ring_y>");
    }
    if (!absl::SimpleAtoi(argv[index + 1], &options.ring_x) ||
        !absl::SimpleAtoi(argv[index + 2], &options.ring_y)) {
      return absl::InvalidArgumentError("--inner-corners takes two integer tile coordinates");
    }
    options.has_ring = true;
    index += 3;
  }

  if (options.has_ring && options.placeholder == zebes::InnerCornerSeed::kPlaceholderFromFill) {
    return absl::InvalidArgumentError(
        "--inner-corners and --placeholder-inner-corners both fill the concave corners; pass one");
  }
  return options;
}

// Crops the 16 derivable quadrants out of an existing 3x3 terrain block, and
// with --inner-corners the remaining 4 out of a 3x3 ring, completing all 20 from
// a single atlas.
absl::Status RunSeed(int argc, char* argv[]) {
  if (argc < 7) {
    return absl::InvalidArgumentError(
        "Usage: compose_blob47 seed <atlas.png> <tile_size> <origin_x> <origin_y> <sheet.png> "
        "[--inner-corners <ring_x> <ring_y>] [--placeholder-inner-corners]");
  }

  ASSIGN_OR_RETURN(const SeedOptions options, ParseSeedOptions(argc, argv, 7));
  ASSIGN_OR_RETURN(RgbaImage atlas, ReadPng(argv[2]));
  ASSIGN_OR_RETURN(const int tile_size, ParsePositiveInt(argv[3], "tile_size"));

  int origin_x = 0;
  int origin_y = 0;
  if (!absl::SimpleAtoi(argv[4], &origin_x) || !absl::SimpleAtoi(argv[5], &origin_y)) {
    return absl::InvalidArgumentError("origin_x and origin_y must be integers");
  }

  ASSIGN_OR_RETURN(
      QuadrantSheet sheet,
      SeedQuadrantSheetFrom3x3(atlas, tile_size, origin_x, origin_y, options.placeholder));
  if (options.has_ring) {
    RETURN_IF_ERROR(zebes::SeedInnerCornersFromRing(atlas, options.ring_x, options.ring_y, sheet));
  }
  RETURN_IF_ERROR(WritePng(argv[6], sheet.image));

  LOG(INFO) << "Seeded " << sheet.image.width << "x" << sheet.image.height << " quadrant sheet at "
            << sheet.quadrant_size << "px: " << argv[6];
  LOG(INFO) << "Inner-corner column is index "
            << static_cast<int>(zebes::QuadrantState::kInnerCorner) << " (one cell per row).";
  if (options.has_ring) {
    LOG(INFO) << "Inner corners cropped from the ring at tile (" << options.ring_x << ", "
              << options.ring_y << "); the sheet is complete and ready to compose.";
    return absl::OkStatus();
  }
  if (options.placeholder == zebes::InnerCornerSeed::kPlaceholderFromFill) {
    LOG(INFO) << "Inner corners are interior placeholders; redraw them for rounded corners.";
    return absl::OkStatus();
  }
  LOG(INFO) << "Draw those 4 cells to complete the terrain, or re-run with "
               "--inner-corners <ring_x> <ring_y> to crop them from a 3x3 ring.";
  return absl::OkStatus();
}

// Crops a slope unit out of an atlas into its canonical column.
absl::Status CropSlopeInto(const RgbaImage& atlas, int tile_size, const std::string& assignment,
                           RgbaImage& sheet) {
  const size_t equals = assignment.find('=');
  const size_t comma = assignment.find(',', equals + 1);
  if (equals == std::string::npos || comma == std::string::npos) {
    return absl::InvalidArgumentError(
        absl::StrCat("--tile expects <ShapeName>=<x>,<y>, got '", assignment, "'"));
  }

  const std::string name = assignment.substr(0, equals);
  const std::optional<zebes::TileShape> shape = zebes::TileShapeFromIdentifier(name);
  if (!shape.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("Unknown tile shape '", name, "'"));
  }

  const int column = static_cast<int>(*shape) - zebes::kFirstSlopeShape;
  if (column < 0 || column >= zebes::kSlopeShapeCount) {
    return absl::InvalidArgumentError(
        absl::StrCat("'", name, "' is not a slope shape; the slope sheet only carries slopes"));
  }

  int tile_x = 0;
  int tile_y = 0;
  if (!absl::SimpleAtoi(assignment.substr(equals + 1, comma - equals - 1), &tile_x) ||
      !absl::SimpleAtoi(assignment.substr(comma + 1), &tile_y) || tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("--tile coordinates must be non-negative integers, got '", assignment, "'"));
  }
  if ((tile_x + 1) * tile_size > atlas.width || (tile_y + 1) * tile_size > atlas.height) {
    return absl::OutOfRangeError(
        absl::StrCat("Tile (", tile_x, ", ", tile_y, ") for '", name, "' is outside the atlas"));
  }

  RETURN_IF_ERROR(zebes::CopyTile(atlas, tile_x * tile_size, tile_y * tile_size, tile_size, sheet,
                                  column * tile_size, 0));
  LOG(INFO) << "Slope column " << column << " (" << name << ") <- atlas tile (" << tile_x << ", "
            << tile_y << ")";
  return absl::OkStatus();
}

// Builds the canonical slope sheet by cropping named units out of an atlas.
//
// The sheet's column order is a TileShape-derived internal contract nobody
// should have to reproduce by hand, so the atlas stays the only thing an artist
// touches. Columns left unassigned stay transparent, which compose skips.
absl::Status RunSlopes(int argc, char* argv[]) {
  if (argc < 5) {
    return absl::InvalidArgumentError(
        "Usage: compose_blob47 slopes <atlas.png> <tile_size> <slope_sheet.png> "
        "[--tile <ShapeName>=<x>,<y>]...");
  }

  ASSIGN_OR_RETURN(RgbaImage atlas, ReadPng(argv[2]));
  ASSIGN_OR_RETURN(const int tile_size, ParsePositiveInt(argv[3], "tile_size"));

  RgbaImage sheet;
  sheet.width = tile_size * zebes::kSlopeShapeCount;
  sheet.height = tile_size;
  sheet.pixels.assign(static_cast<size_t>(sheet.width) * sheet.height * 4, 0);

  int assigned = 0;
  for (int index = 5; index < argc; index += 2) {
    if (std::string(argv[index]) != "--tile") {
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown flag '", argv[index], "'; expected --tile"));
    }
    if (index + 1 >= argc) {
      return absl::InvalidArgumentError("--tile requires <ShapeName>=<x>,<y>");
    }
    RETURN_IF_ERROR(CropSlopeInto(atlas, tile_size, argv[index + 1], sheet));
    ++assigned;
  }

  RETURN_IF_ERROR(WritePng(argv[4], sheet));
  LOG(INFO) << "Wrote " << sheet.width << "x" << sheet.height << " slope sheet with " << assigned
            << " unit(s): " << argv[4];
  return absl::OkStatus();
}

// Composites a quadrant sheet into a full blob-47 atlas plus its manifest.
absl::Status RunCompose(int argc, char* argv[]) {
  if (argc != 5 && argc != 7) {
    return absl::InvalidArgumentError(
        "Usage: compose_blob47 compose <sheet.png> <variant_count> <output_prefix> "
        "[--slopes <slope_sheet.png>]");
  }

  ASSIGN_OR_RETURN(RgbaImage image, ReadPng(argv[2]));
  ASSIGN_OR_RETURN(const int variant_count, ParsePositiveInt(argv[3], "variant_count"));

  const int expected_columns = zebes::kQuadrantStateCount * variant_count;
  if (image.width % expected_columns != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Sheet width ", image.width, " is not divisible by ", expected_columns,
                     " quadrant columns"));
  }

  QuadrantSheet sheet;
  sheet.image = std::move(image);
  sheet.variant_count = variant_count;
  sheet.quadrant_size = sheet.image.width / expected_columns;

  std::optional<zebes::SlopeSheet> slopes;
  if (argc == 7) {
    if (std::string(argv[5]) != "--slopes") {
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown flag '", argv[5], "'; expected --slopes"));
    }
    ASSIGN_OR_RETURN(RgbaImage slope_image, ReadPng(argv[6]));
    slopes = zebes::SlopeSheet{.image = std::move(slope_image),
                               .tile_size = sheet.quadrant_size * 2};
  }

  ASSIGN_OR_RETURN(Blob47Atlas atlas,
                   ComposeBlob47(sheet, slopes.has_value() ? &*slopes : nullptr));

  const std::string png_path = absl::StrCat(argv[4], ".png");
  const std::string manifest_path = absl::StrCat(argv[4], ".json");
  RETURN_IF_ERROR(WritePng(png_path, atlas.image));
  RETURN_IF_ERROR(WriteTextFile(manifest_path, WriteBlob47Manifest(atlas)));

  LOG(INFO) << "Composed " << atlas.tiles.size() << " tiles and " << atlas.slopes.size()
            << " slope unit(s) at " << atlas.tile_size << "px into " << atlas.image.width << "x"
            << atlas.image.height << " atlas: " << png_path;
  LOG(INFO) << "Wrote import manifest: " << manifest_path;
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  // Without this absl drops INFO before it reaches stderr, so every progress
  // line this tool has ever logged went nowhere.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  if (argc < 2) {
    LOG(ERROR) << "Usage: " << argv[0] << " <seed|slopes|compose> ...";
    return 1;
  }

  const std::string command = argv[1];
  absl::Status status = absl::InvalidArgumentError(
      absl::StrCat("Unknown command '", command, "'; expected 'seed', 'slopes' or 'compose'"));
  if (command == "seed") status = RunSeed(argc, argv);
  if (command == "slopes") status = RunSlopes(argc, argv);
  if (command == "compose") status = RunCompose(argc, argv);

  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
