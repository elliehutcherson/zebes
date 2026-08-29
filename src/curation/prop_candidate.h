#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "artwork/prop_recipe.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"
#include "objects/sprite.h"

namespace zebes {

enum class PropCandidateOperation {
  kCreate,
  kRegenerate,
};

// Platform-neutral deterministic output shared by standalone prop evidence and
// transient level composition. It owns every definition and pixel buffer its
// consumers reference, but has no authority to register or persist them.
struct PreparedPropCandidate {
  PropRecipe recipe;
  RgbaImage texture;
  Sprite sprite;
  BlueprintPlacementMode placement_mode = BlueprintPlacementMode::kFree;
  PropCandidateOperation operation = PropCandidateOperation::kCreate;
  bool matches_deterministic_output = false;
  std::string source_content_digest;
  nlohmann::json requested_candidate;
};

std::string_view PropCandidateOperationId(PropCandidateOperation operation);

absl::StatusOr<BlueprintPlacementMode> ResolvePropPlacementMode(const Blueprint& blueprint,
                                                                std::string_view sprite_id);

// Prepares either a generated-creation envelope or a recipe-regeneration
// candidate through the same production artwork pipeline used at commit. An
// absent expected ID is appropriate for composition into another asset, where
// the candidate document itself owns its prop identity.
absl::StatusOr<PreparedPropCandidate> PreparePropCandidateForReview(
    Api& api, const std::filesystem::path& candidate_root,
    std::optional<std::string_view> expected_asset_id, const nlohmann::json& candidate);

absl::Status ValidatePreparedPropCandidate(const PreparedPropCandidate& prepared);

// Re-prepares against live inputs and crosses the persistence boundary. Review
// output is deliberately not accepted here: regeneration must recheck
// optimistic state, and creation must acquire a retained source identity first.
absl::Status CommitPropCandidate(Api& api, const std::filesystem::path& candidate_root,
                                 std::string_view expected_asset_id,
                                 const nlohmann::json& candidate);

}  // namespace zebes
