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
#include "terrain/terrain_review_scene.h"
#include "terrain/terrain_style.h"

namespace {

using ::zebes::BuiltInTerrainPresets;
using ::zebes::RenderTerrainSlopeReviewMatrix;
using ::zebes::RgbaImage;
using ::zebes::TerrainGenConfig;
using ::zebes::TerrainPreset;
using ::zebes::TerrainSlopeReviewBandNames;
using ::zebes::WritePng;

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

  TerrainGenConfig config = preset->config;
  ASSIGN_OR_RETURN(const RgbaImage sheet, RenderTerrainSlopeReviewMatrix(config));
  RETURN_IF_ERROR(WritePng(output_path, sheet.width, sheet.height, sheet.pixels));

  LOG(INFO) << "preset '" << preset->name << "' at tile size " << config.tile_size;
  LOG(INFO) << "wrote " << output_path;
  int band = 0;
  for (const std::string_view name : TerrainSlopeReviewBandNames()) {
    LOG(INFO) << "  band " << ++band << ": " << name;
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
