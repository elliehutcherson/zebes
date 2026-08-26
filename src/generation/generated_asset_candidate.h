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

// Creation candidates are deliberately distinct from recipe candidates. The
// marker selects the new-source/new-bundle transaction; an existing recipe
// object continues to select settings-only regeneration.
bool IsGeneratedAssetCreationCandidate(const nlohmann::json& json);

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

}  // namespace zebes
