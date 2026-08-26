#pragma once

#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_style.h"

namespace zebes {

// Stable top-to-bottom legend for the fixed slope-join review scene.
std::vector<std::string_view> TerrainSlopeReviewBandNames();

// Uses the same contextual terrain renderer as level authoring. Variant period
// is held to one so each band reviews geometry and joins rather than field
// phase selection.
absl::StatusOr<RgbaImage> RenderTerrainSlopeReviewMatrix(TerrainGenConfig config);

}  // namespace zebes
