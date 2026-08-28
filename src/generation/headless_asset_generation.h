#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "generation/image_generation_service.h"

namespace zebes {

struct HeadlessAssetGenerationRequest {
  std::string kind;
  std::string template_recipe_id;
  std::string name;
  std::string prompt;
  std::string output_path;
};

struct HeadlessAssetGenerationResult {
  std::string asset_id;
  std::string candidate_path;
  std::string manifest_path;
};

struct HeadlessAssetRedrawRequest {
  std::string asset_id;
  std::string prompt;
  std::string output_path;
};

// Preflights all process arguments, including the never-replace output
// contract, before a provider service is composed or a remote request starts.
absl::Status ValidateHeadlessAssetGenerationRequest(const HeadlessAssetGenerationRequest& request);

// Generates one source, passes it through the shared postprocessor, and
// atomically publishes a strict new-asset candidate bundle. `template_recipe`
// supplies domain settings only; the output always receives fresh IDs.
absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetGenerationRequest& request);

absl::Status ValidateHeadlessAssetRedrawRequest(const HeadlessAssetRedrawRequest& request);

// Generates an edit from the asset's retained source and publishes a redraw
// candidate under the same stable IDs. The candidate records the exact source
// and derived digests it read so later review rejects stale work.
absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetRedrawCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetRedrawRequest& request);

}  // namespace zebes
