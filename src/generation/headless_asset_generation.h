#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "artwork/compose_prop.h"
#include "common/image_io.h"
#include "generation/image_generation.h"
#include "generation/image_generation_service.h"

namespace zebes {

// A headless-only reference origin. Exactly one origin is set. Filesystem and
// managed-resource details stop at the resolver and never enter the async
// provider-neutral request contract.
struct HeadlessImageReferenceSource {
  ImageGenerationReferenceRole role = ImageGenerationReferenceRole::kEditSource;
  std::optional<std::string> path;
  std::optional<std::string> source_artwork_id;
};

struct HeadlessImageReferenceManifest {
  // Canonical manifest path. Relative reference paths resolve within its
  // parent directory and cannot escape it.
  std::filesystem::path manifest_path;
  std::vector<HeadlessImageReferenceSource> references;
};

struct ResolvedHeadlessImageReference {
  HeadlessImageReferenceSource source;
  RgbaImage image;
  std::string content_digest;
};

absl::Status ValidateHeadlessImageReferenceSource(const HeadlessImageReferenceSource& source);

// Loads the strict schema-version-1 reference manifest. This workflow is
// intentionally narrower than the core contract: it requires one
// subject-identity reference followed by one pose reference.
absl::StatusOr<HeadlessImageReferenceManifest> LoadHeadlessImageReferenceManifest(
    const std::filesystem::path& path);

// Resolves one descriptor to owned RGBA8 pixels and auditable metadata. A
// positive pixel bound is required before any path-backed image is decoded.
// `reference_root` is ignored for managed SourceArtwork descriptors.
absl::StatusOr<ResolvedHeadlessImageReference> ResolveHeadlessImageReference(
    Api& api, const HeadlessImageReferenceSource& source,
    const std::filesystem::path& reference_root, int64_t maximum_pixels);

struct HeadlessAssetGenerationRequest {
  std::string kind;
  std::string template_recipe_id;
  std::string name;
  std::string prompt;
  std::string output_path;
  std::optional<int> prop_canvas_tiles_wide;
  std::optional<int> prop_canvas_tiles_high;
  std::optional<PropAttachmentMode> prop_attachment_mode;
  std::optional<PropFreeAnchor> prop_free_anchor;
  std::optional<HeadlessImageReferenceManifest> reference_manifest;
};

struct HeadlessAssetGenerationResult {
  std::string asset_id;
  std::string candidate_path;
  std::string manifest_path;
};

struct HeadlessAssetStagingRequest {
  std::string kind;
  std::string template_recipe_id;
  std::string name;
  std::string prompt;
  std::string provider;
  std::string model;
  std::string output_path;
  std::optional<int> prop_canvas_tiles_wide;
  std::optional<int> prop_canvas_tiles_high;
  std::optional<PropAttachmentMode> prop_attachment_mode;
  std::optional<PropFreeAnchor> prop_free_anchor;
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

absl::Status ValidateHeadlessAssetStagingRequest(const HeadlessAssetStagingRequest& request);

// Imports one already-generated image into the same strict creation bundle
// used by provider-backed generation. The image remains a retained source;
// recipe-specific isolation, palette mapping, and sizing happen during review.
absl::StatusOr<HeadlessAssetGenerationResult> StageAssetCandidateBundle(
    Api& api, const RgbaImage& image, const HeadlessAssetStagingRequest& request);

absl::Status ValidateHeadlessAssetRedrawRequest(const HeadlessAssetRedrawRequest& request);

// Generates an edit from the asset's retained source and publishes a redraw
// candidate under the same stable IDs. The candidate records the exact source
// and derived digests it read so later review rejects stale work.
absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetRedrawCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetRedrawRequest& request);

}  // namespace zebes
