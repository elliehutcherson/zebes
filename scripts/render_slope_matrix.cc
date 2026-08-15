// Renders a sheet of slope joins so they can be looked at.
//
// This tool began as a measurement: it drew every join twice, once the way the
// baked atlas drew it and once against the neighbours actually present, and
// reported how far apart they were. That gap is what motivated deriving
// artwork, and closing it left nothing to compare -- a cell is now drawn for the
// neighbourhood it has, by construction, and
// tests/editor/derived_artwork_test.cc asserts exactly that.
//
// What remains useful is the picture. Slope artwork is the hardest part of a
// material to tune and the Terrain tab's preview scene shows only painted
// ground, so this renders the joins a level can actually contain.

#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "objects/tileset.h"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_style.h"

namespace {

using ::zebes::BuiltInTerrainPresets;
using ::zebes::RenderShapeScene;
using ::zebes::RgbaImage;
using ::zebes::ShapeScene;
using ::zebes::TerrainGenConfig;
using ::zebes::TerrainPreset;
using ::zebes::TerrainRenderer;
using ::zebes::TileShape;
using ::zebes::WritePng;

// Scene shorthand. An unknown character is an error rather than air, so a typo
// in a case below fails instead of quietly rendering a hole.
absl::StatusOr<TileShape> ShapeFromChar(char c) {
  switch (c) {
    case '.':
      return TileShape::kNone;
    case '#':
      return TileShape::kFullBlock;
    case 'h':
      return TileShape::kHalfBlockBottom;
    // 45-degree floor units. '/' rises to the right, '\' rises to the left.
    case '/':
      return TileShape::kSlope45BottomLeft;
    case '\\':
      return TileShape::kSlope45BottomRight;
    // Gentle floor ramp rising to the right: the lower half leads.
    case 'a':
      return TileShape::kGentleSlopeBottomLeftLower;
    case 'b':
      return TileShape::kGentleSlopeBottomLeftUpper;
    // Steep floor ramp rising to the right, stacked top over bottom.
    case 't':
      return TileShape::kSteepSlopeBottomLeftTop;
    case 'u':
      return TileShape::kSteepSlopeBottomLeftBottom;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("unknown scene character '", std::string(1, c), "'"));
  }
}

struct JoinCase {
  std::string name;
  std::vector<std::string> rows;
};

std::vector<JoinCase> MatrixCases() {
  return {
      {"ramp into ground", {"../#", "####"}},
      {"ramp ending at air", {"../.", "###."}},
      {"peak: two ramps meet", {"./\\.", "####"}},
      {"valley: two ramps meet at the bottom", {"#\\./#", "#####"}},
      {"gentle ramp into ground", {".ab#", "####"}},
      {"gentle ramp with a landing", {".ahhb.", "######"}},
      {"gentle ramp ending at air", {".ab..", "####."}},
      {"steep ramp into ground", {"..t#", "..u#", "####"}},
  };
}

// Lays the cases out in vertical bands. One gutter row would keep their
// neighbourhoods from touching; two makes the image readable.
absl::StatusOr<ShapeScene> BuildScene(const std::vector<JoinCase>& cases) {
  constexpr int kGutter = 2;

  int width = 0;
  int height = 0;
  for (const JoinCase& join : cases) {
    for (const std::string& row : join.rows) {
      width = std::max(width, static_cast<int>(row.size()));
    }
    height += static_cast<int>(join.rows.size()) + kGutter;
  }

  ShapeScene scene;
  scene.width = width;
  scene.height = height;
  scene.cells.assign(static_cast<size_t>(width) * height, TileShape::kNone);

  int row_origin = 0;
  for (const JoinCase& join : cases) {
    for (size_t y = 0; y < join.rows.size(); ++y) {
      for (size_t x = 0; x < join.rows[y].size(); ++x) {
        ASSIGN_OR_RETURN(const TileShape shape, ShapeFromChar(join.rows[y][x]));
        scene.cells[(row_origin + y) * width + x] = shape;
      }
    }
    row_origin += static_cast<int>(join.rows.size()) + kGutter;
  }
  return scene;
}

absl::Status Render(const std::string& preset_name, const std::string& output_path) {
  const TerrainPreset* preset = nullptr;
  std::vector<std::string> known;
  for (const TerrainPreset& candidate : BuiltInTerrainPresets()) {
    known.push_back(candidate.name);
    if (candidate.name == preset_name) preset = &candidate;
  }
  if (preset == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "unknown preset '", preset_name, "'; known presets are ", absl::StrJoin(known, ", ")));
  }

  // One phase, so a band's shape is what the joins do rather than which phase
  // each cell happened to land on.
  TerrainGenConfig config = preset->config;
  config.variant_period = 1;

  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(config));
  ASSIGN_OR_RETURN(const ShapeScene scene, BuildScene(MatrixCases()));
  ASSIGN_OR_RETURN(const RgbaImage sheet, RenderShapeScene(renderer, scene));
  RETURN_IF_ERROR(WritePng(output_path, sheet.width, sheet.height, sheet.pixels));

  LOG(INFO) << "preset '" << preset->name << "' at tile size " << config.tile_size;
  LOG(INFO) << "wrote " << output_path;
  int band = 0;
  for (const JoinCase& join : MatrixCases()) {
    LOG(INFO) << "  band " << ++band << ": " << join.name;
  }
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  // Without this absl drops INFO before it reaches stderr, and the band legend
  // -- which is how anyone tells the rows apart -- goes nowhere.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  if (argc != 3) {
    LOG(ERROR) << "Usage: " << argv[0] << " <preset_name> <output.png>";
    return 1;
  }

  const absl::Status status = Render(argv[1], argv[2]);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
