#pragma once

#include <filesystem>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "artwork/prepare_prop_asset.h"
#include "artwork/prop_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "nlohmann/json_fwd.hpp"

namespace zebes {

inline constexpr int kGeneratedAssetCandidateSchemaVersion = 1;

struct GeneratedAssetSourceCandidate {
  std::string relative_path;
  int width = 0;
  int height = 0;
  std::string content_digest;
  GeneratedArtworkProvenance provenance;
};

struct GeneratedPropCreationCandidate {
  std::string asset_id;
  std::string name;
  GeneratedAssetSourceCandidate source;
  PropRecipe template_recipe;
  PropAssetIds ids;
};

struct GeneratedParallaxArtworkCreationCandidate {
  std::string asset_id;
  std::string name;
  GeneratedAssetSourceCandidate source;
  ParallaxArtworkRecipe template_recipe;
  ParallaxArtworkAssetIds ids;
};

// A redraw changes the retained pixels of an existing generated parallax
// bundle. The current recipe supplies the immutable IDs and processing
// settings; the candidate supplies only the new, reviewable source envelope.
struct GeneratedParallaxArtworkRedrawCandidate {
  std::string asset_id;
  // Optimistic-concurrency tokens captured before generation begins. A
  // reviewed candidate cannot overwrite a source or derived texture that was
  // updated while an agent was working.
  std::string expected_source_digest;
  std::string expected_final_pixel_digest;
  GeneratedAssetSourceCandidate source;
};

// Creation candidates are deliberately distinct from recipe candidates. The
// marker selects the new-source/new-bundle transaction; an existing recipe
// object continues to select settings-only regeneration.
bool IsGeneratedAssetCreationCandidate(const nlohmann::json& json);
bool IsGeneratedAssetRedrawCandidate(const nlohmann::json& json);

absl::Status ValidateGeneratedAssetSourceCandidate(const GeneratedAssetSourceCandidate& candidate);
absl::StatusOr<RgbaImage> ReadGeneratedAssetSourceCandidate(
    const std::filesystem::path& candidate_root, const GeneratedAssetSourceCandidate& candidate);

nlohmann::json GeneratedPropCreationCandidateToJson(
    const GeneratedPropCreationCandidate& candidate);
absl::StatusOr<GeneratedPropCreationCandidate> GeneratedPropCreationCandidateFromJson(
    const nlohmann::json& json);

nlohmann::json GeneratedParallaxArtworkCreationCandidateToJson(
    const GeneratedParallaxArtworkCreationCandidate& candidate);
absl::StatusOr<GeneratedParallaxArtworkCreationCandidate>
GeneratedParallaxArtworkCreationCandidateFromJson(const nlohmann::json& json);

nlohmann::json GeneratedParallaxArtworkRedrawCandidateToJson(
    const GeneratedParallaxArtworkRedrawCandidate& candidate);
absl::StatusOr<GeneratedParallaxArtworkRedrawCandidate>
GeneratedParallaxArtworkRedrawCandidateFromJson(const nlohmann::json& json);

}  // namespace zebes
