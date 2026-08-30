#pragma once

#include <filesystem>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/animation_artwork_feasibility.h"

namespace zebes {

// Locked palette shared by the disposable animation feasibility tools. It is
// intentionally authored rather than inferred from generated candidates.
std::vector<RgbaColor> AnimationArtworkFeasibilityPalette();

struct AnimationArtworkRunManifest {
  AnimationFeasibilityClip clip = AnimationFeasibilityClip::kIdleRight;
  AnimationFeasibilitySheetLayout sheet;
  std::vector<int> frames_per_cycle;
  std::vector<bool> planted_frames;
};

// Loads the strict script-local schema-version-1 animation run manifest. This
// is experiment input, not a serialized asset recipe. Rectangular cells are
// representable here so evidence can record provider-native aspect ratios;
// conversion to the feasibility processor retains its square-cell invariant.
absl::StatusOr<AnimationArtworkRunManifest> LoadAnimationArtworkRunManifest(
    const std::filesystem::path& path);

absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAnimationArtworkRunManifestConfig(
    const AnimationArtworkRunManifest& manifest, std::vector<RgbaColor> palette);

}  // namespace zebes
