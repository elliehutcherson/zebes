#include "terrain/terrain_review_scene.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "objects/tileset.h"
#include "terrain/terrain_generator.h"

namespace zebes {
namespace {

struct JoinCase {
  std::string_view name;
  std::vector<std::string> rows;
};

const std::vector<JoinCase>& MatrixCases() {
  static const std::vector<JoinCase> kCases = {
      {"ramp into ground", {"../#", "####"}},
      {"ramp ending at air", {"../.", "###."}},
      {"peak: two ramps meet", {"./\\.", "####"}},
      {"valley: two ramps meet at the bottom", {"#\\./#", "#####"}},
      {"gentle ramp into ground", {".ab#", "####"}},
      {"gentle ramp with a landing", {".ahhb.", "######"}},
      {"gentle ramp ending at air", {".ab..", "####."}},
      {"steep ramp into ground", {"..t#", "..u#", "####"}},
  };
  return kCases;
}

absl::StatusOr<TileShape> ShapeFromChar(char character) {
  switch (character) {
    case '.':
      return TileShape::kNone;
    case '#':
      return TileShape::kFullBlock;
    case 'h':
      return TileShape::kHalfBlockBottom;
    case '/':
      return TileShape::kSlope45FloorTallRight;
    case '\\':
      return TileShape::kSlope45FloorTallLeft;
    case 'a':
      return TileShape::kGentleSlopeFloorTallRightLower;
    case 'b':
      return TileShape::kGentleSlopeFloorTallRightUpper;
    case 't':
      return TileShape::kSteepSlopeFloorTallRightTop;
    case 'u':
      return TileShape::kSteepSlopeFloorTallRightBottom;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("unknown terrain review scene character '", std::string(1, character), "'"));
  }
}

absl::StatusOr<ShapeScene> BuildScene() {
  constexpr int kGutter = 2;
  int width = 0;
  int height = 0;
  for (const JoinCase& join : MatrixCases()) {
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
  for (const JoinCase& join : MatrixCases()) {
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

}  // namespace

std::vector<std::string_view> TerrainSlopeReviewBandNames() {
  std::vector<std::string_view> names;
  names.reserve(MatrixCases().size());
  for (const JoinCase& join : MatrixCases()) names.push_back(join.name);
  return names;
}

absl::StatusOr<RgbaImage> RenderTerrainSlopeReviewMatrix(TerrainGenConfig config) {
  config.variant_period = 1;
  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(config));
  ASSIGN_OR_RETURN(const ShapeScene scene, BuildScene());
  return RenderShapeScene(renderer, scene);
}

}  // namespace zebes
