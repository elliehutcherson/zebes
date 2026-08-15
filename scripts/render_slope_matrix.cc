// Renders the slope-join matrix twice and reports where the two disagree.
//
// The generator bakes slope artwork before any level exists, so it infers each
// slope's neighbourhood from the shape's own polygon (AutoContext). That
// inference is right for some joins and wrong for others, and until now nothing
// said which. This tool renders every join the level editor can produce both
// ways -- as the shipped atlas draws it, and against the neighbours actually
// present -- and prints the cells where they differ.
//
// A differing cell is a join the atlas cannot currently express. An identical
// one is a join the inference already gets right and that needs no new artwork.


#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "objects/tileset.h"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_style.h"

namespace {

using ::zebes::BuiltInTerrainPresets;
using ::zebes::RenderSceneCell;
using ::zebes::RenderShapeScene;
using ::zebes::RgbaImage;
using ::zebes::SceneContext;
using ::zebes::ShapeScene;
using ::zebes::TerrainGenConfig;
using ::zebes::TerrainPreset;
using ::zebes::TerrainRenderer;
using ::zebes::TileShape;
using ::zebes::WritePng;

// Scene shorthand. Only the shapes the matrix exercises are spelled; an
// unknown character is an error rather than air, so a typo in a case below
// fails instead of quietly rendering a hole.
absl::StatusOr<TileShape> ShapeFromChar(char c) {
  switch (c) {
    case '.': return TileShape::kNone;
    case '#': return TileShape::kFullBlock;
    // 45-degree floor units. '/' rises to the right, '\' rises to the left.
    case '/': return TileShape::kSlope45BottomLeft;
    case '\\': return TileShape::kSlope45BottomRight;
    // Gentle floor ramp rising to the right: the Lower half leads.
    case 'a': return TileShape::kGentleSlopeBottomLeft_Lower;
    case 'b': return TileShape::kGentleSlopeBottomLeft_Upper;
    // Gentle floor ramp falling to the right: the Upper half leads.
    case 'c': return TileShape::kGentleSlopeBottomRight_Upper;
    case 'd': return TileShape::kGentleSlopeBottomRight_Lower;
    // Steep floor ramp rising to the right, stacked Top over Bottom.
    case 't': return TileShape::kSteepSlopeBottomLeft_Top;
    case 'u': return TileShape::kSteepSlopeBottomLeft_Bottom;
    default:
      return absl::InvalidArgumentError(absl::StrCat("unknown scene character '", std::string(1, c),
                                                     "'"));
  }
}

// One join worth looking at, drawn bottom-aligned in its own band.
struct JoinCase {
  std::string name;
  // What the atlas is expected to get right, stated so the report reads as a
  // check rather than as a dump.
  std::string expectation;
  std::vector<std::string> rows;
};

std::vector<JoinCase> MatrixCases() {
  return {
      {.name = "flat_ground",
       .expectation = "identical: no slope involved",
       .rows = {"....", "####"}},
      {.name = "ramp_into_ground",
       .expectation = "identical: AutoContext infers a solid uphill face",
       .rows = {"../#", "####"}},
      // The pair that isolates the south-west corner: the same ramp over ground
      // that does, and does not, continue past its toe. AutoContext marks SW
      // open in both, because it takes a corner only when both flanking edges
      // are solid and the ramp's west face is a point.
      {.name = "ramp_toe_over_continuing_ground",
       .expectation = "SW is solid in truth, open to AutoContext",
       .rows = {"../#", "####"}},
      {.name = "ramp_toe_over_ending_ground",
       .expectation = "SW is genuinely open; should match ramp_toe_over_continuing_ground",
       .rows = {"../#", "..##"}},
      {.name = "peak",
       .expectation = "differs: the uphill face meets a descending ramp, not a wall",
       .rows = {"./\\.", "####"}},
      {.name = "ledge",
       .expectation = "differs: the uphill face meets air",
       .rows = {"../.", "###."}},
      {.name = "valley",
       .expectation = "two ramps meeting at the bottom",
       .rows = {"#\\./#", "#####"}},
      {.name = "gentle_ramp_into_ground",
       .expectation = "ApplyPartner already joins the two halves",
       .rows = {".ab#", "####"}},
      {.name = "gentle_ledge",
       .expectation = "differs: the upper half's uphill face meets air",
       .rows = {".ab..", "####."}},
      {.name = "gentle_peak",
       .expectation = "the uphill face meets a descending ramp, not a wall",
       .rows = {".abcd.", "######"}},
      {.name = "steep_ramp_into_ground",
       .expectation = "ApplyPartner already joins the two halves",
       .rows = {"..t#", "..u#", "####"}},
  };
}

// Lays the cases out in vertical bands, two air rows apart. One gutter row
// would be enough to keep neighbourhoods from touching; two makes the image
// readable.
absl::StatusOr<ShapeScene> BuildScene(const std::vector<JoinCase>& cases,
                                      std::vector<int>& band_origins) {
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
    band_origins.push_back(row_origin);
    for (size_t y = 0; y < join.rows.size(); ++y) {
      const std::string& row = join.rows[y];
      for (size_t x = 0; x < row.size(); ++x) {
        ASSIGN_OR_RETURN(const TileShape shape, ShapeFromChar(row[x]));
        scene.cells[(row_origin + y) * width + x] = shape;
      }
    }
    row_origin += static_cast<int>(join.rows.size()) + kGutter;
  }
  return scene;
}

// Pixels that differ between the two renderings of one cell.
int CountDifferences(const RgbaImage& left, const RgbaImage& right) {
  int different = 0;
  for (size_t i = 0; i + 3 < left.pixels.size(); i += 4) {
    const bool same = left.pixels[i + 0] == right.pixels[i + 0] &&
                      left.pixels[i + 1] == right.pixels[i + 1] &&
                      left.pixels[i + 2] == right.pixels[i + 2] &&
                      left.pixels[i + 3] == right.pixels[i + 3];
    if (!same) ++different;
  }
  return different;
}

absl::Status Report(const TerrainRenderer& renderer, const ShapeScene& scene,
                    const std::vector<JoinCase>& cases, const std::vector<int>& band_origins) {
  const int tile_pixels = renderer.config().tile_size * renderer.config().tile_size;

  for (size_t c = 0; c < cases.size(); ++c) {
    const JoinCase& join = cases[c];
    const int origin = band_origins[c];

    std::vector<std::string> differing;
    for (int y = 0; y < static_cast<int>(join.rows.size()); ++y) {
      for (int x = 0; x < scene.width; ++x) {
        const int scene_y = origin + y;
        if (scene.At(x, scene_y) == TileShape::kNone) continue;

        ASSIGN_OR_RETURN(const RgbaImage as_atlas,
                         RenderSceneCell(renderer, scene, x, scene_y, SceneContext::kAsAtlas));
        ASSIGN_OR_RETURN(const RgbaImage truth,
                         RenderSceneCell(renderer, scene, x, scene_y, SceneContext::kTrueNeighbors));

        const int different = CountDifferences(as_atlas, truth);
        if (different == 0) continue;
        // Exact pixel counts, not a percentage. A truncated percent reads "0%"
        // for a handful of differing pixels, which is how a join that is merely
        // very close came to be described as identical.
        differing.push_back(absl::StrCat("(", x, ",", y, ")=", std::string(1, join.rows[y][x]), " ",
                                         different, "/", tile_pixels, "px"));
      }
    }

    LOG(INFO) << join.name << ": "
              << (differing.empty() ? std::string("identical")
                                    : absl::StrCat(differing.size(), " cell(s) differ: ",
                                                   absl::StrJoin(differing, " ")));
    LOG(INFO) << "    expected: " << join.expectation;
  }
  return absl::OkStatus();
}

absl::Status Render(const std::string& preset_name, const std::string& output_prefix) {
  const TerrainPreset* preset = nullptr;
  std::vector<std::string> known;
  for (const TerrainPreset& candidate : BuiltInTerrainPresets()) {
    known.push_back(candidate.name);
    if (candidate.name == preset_name) preset = &candidate;
  }
  if (preset == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown preset '", preset_name, "'; known presets are ",
                     absl::StrJoin(known, ", ")));
  }

  // One phase only. The atlas holds a single drawing per slope regardless of
  // period, so a periodic terrain would mix a phase difference into every
  // comparison and hide the neighbourhood difference this tool is for.
  TerrainGenConfig config = preset->config;
  config.variant_period = 1;

  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(config));

  std::vector<int> band_origins;
  ASSIGN_OR_RETURN(const ShapeScene scene, BuildScene(MatrixCases(), band_origins));

  ASSIGN_OR_RETURN(const RgbaImage as_atlas,
                   RenderShapeScene(renderer, scene, SceneContext::kAsAtlas));
  ASSIGN_OR_RETURN(const RgbaImage truth,
                   RenderShapeScene(renderer, scene, SceneContext::kTrueNeighbors));

  const std::string atlas_path = absl::StrCat(output_prefix, "_atlas.png");
  const std::string truth_path = absl::StrCat(output_prefix, "_true.png");
  RETURN_IF_ERROR(WritePng(atlas_path, as_atlas.width, as_atlas.height, as_atlas.pixels));
  RETURN_IF_ERROR(WritePng(truth_path, truth.width, truth.height, truth.pixels));

  LOG(INFO) << "preset '" << preset->name << "' at tile size " << config.tile_size;
  LOG(INFO) << "as the atlas draws it:   " << atlas_path;
  LOG(INFO) << "against real neighbours: " << truth_path;
  return Report(renderer, scene, MatrixCases(), band_origins);
}

}  // namespace

int main(int argc, char* argv[]) {
  // Without this absl drops INFO before it reaches stderr, and the report --
  // which is the whole point of running the tool -- goes nowhere.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  if (argc != 3) {
    LOG(ERROR) << "Usage: " << argv[0] << " <preset_name> <output_prefix>";
    return 1;
  }

  const absl::Status status = Render(argv[1], argv[2]);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
