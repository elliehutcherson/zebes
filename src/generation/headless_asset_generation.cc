#include "generation/headless_asset_generation.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "artwork/generated_artwork_postprocessor.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prop_recipe.h"
#include "artwork/source_artwork.h"
#include "common/atomic_directory_publisher.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "common/utils.h"
#include "generation/artwork_generation_prompts.h"
#include "generation/generated_asset_candidate.h"
#include "generation/image_generation.h"
#include "generation/image_generation_engine.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr absl::Duration kGenerationTimeout = absl::Minutes(10);
constexpr char kCandidateFilename[] = "candidate.json";
constexpr char kOriginalFilename[] = "generated-source.png";
constexpr char kProcessedFilename[] = "processed-source.png";
constexpr char kReferenceFilename[] = "reference-source.png";
constexpr char kManifestFilename[] = "manifest.json";
constexpr int kReferenceManifestSchemaVersion = 1;
constexpr size_t kMaximumReferenceManifestBytes = 1024 * 1024;
constexpr int64_t kMaximumReferenceFileBytes = 64 * 1024 * 1024;

struct CandidatePlan {
  std::string asset_id;
  ImageGenerationSpec spec;
  nlohmann::json candidate_template;
};

bool IsWithin(const std::filesystem::path& parent, const std::filesystem::path& child) {
  auto parent_component = parent.begin();
  auto child_component = child.begin();
  for (; parent_component != parent.end() && child_component != child.end();
       ++parent_component, ++child_component) {
    if (*parent_component != *child_component) return false;
  }
  return parent_component == parent.end();
}

absl::StatusOr<std::filesystem::path> CanonicalPath(const std::filesystem::path& path,
                                                    std::string_view subject) {
  std::error_code error;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve ", subject, ": ", error.message()));
  }
  return canonical;
}

absl::Status ValidateReferenceManifest(const HeadlessImageReferenceManifest& manifest) {
  if (manifest.manifest_path.empty()) {
    return absl::InvalidArgumentError("headless reference manifest path is empty");
  }
  if (manifest.references.size() != 2) {
    return absl::InvalidArgumentError(
        "pose-conditioned headless generation needs exactly two references");
  }
  for (const HeadlessImageReferenceSource& source : manifest.references) {
    RETURN_IF_ERROR(ValidateHeadlessImageReferenceSource(source));
  }
  if (manifest.references[0].role != ImageGenerationReferenceRole::kSubjectIdentity ||
      manifest.references[1].role != ImageGenerationReferenceRole::kPose) {
    return absl::InvalidArgumentError(
        "pose-conditioned headless references must be subject-identity then pose");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::filesystem::path> ResolveConfinedReferencePath(
    const std::filesystem::path& reference_root, const std::filesystem::path& relative_path) {
  if (reference_root.empty()) {
    return absl::InvalidArgumentError("path-backed reference needs a manifest directory");
  }
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_root,
                   CanonicalPath(reference_root, "reference manifest directory"));
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_source,
                   CanonicalPath(canonical_root / relative_path, "reference image"));
  if (!IsWithin(canonical_root, canonical_source)) {
    return absl::InvalidArgumentError("reference image resolves outside its manifest directory");
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(canonical_source, error) || error) {
    return absl::NotFoundError(
        absl::StrCat("reference image is not a regular file: ", relative_path.string()));
  }
  return canonical_source;
}

absl::StatusOr<RgbaImage> ReadBoundedReferenceImage(const std::filesystem::path& path,
                                                    int64_t maximum_pixels) {
  std::error_code error;
  const uintmax_t file_bytes = std::filesystem::file_size(path, error);
  if (error) {
    return absl::NotFoundError(
        absl::StrCat("could not inspect reference image: ", error.message()));
  }
  if (file_bytes == 0 || file_bytes > static_cast<uintmax_t>(kMaximumReferenceFileBytes)) {
    return absl::ResourceExhaustedError(
        absl::StrCat("reference image encoded size must be between 1 and ",
                     kMaximumReferenceFileBytes, " bytes"));
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open reference image: ", path.string()));
  }
  std::vector<uint8_t> encoded(static_cast<size_t>(file_bytes));
  stream.read(reinterpret_cast<char*>(encoded.data()),
              static_cast<std::streamsize>(encoded.size()));
  if (!stream.good() && !stream.eof()) {
    return absl::DataLossError(absl::StrCat("could not read reference image: ", path.string()));
  }
  if (static_cast<size_t>(stream.gcount()) != encoded.size()) {
    return absl::DataLossError(
        absl::StrCat("reference image changed while reading: ", path.string()));
  }
  return DecodeImage(encoded, maximum_pixels);
}

std::string ReferenceArtifactFilename(size_t index, ImageGenerationReferenceRole role) {
  return absl::StrFormat("reference-%02d-%s.png", static_cast<int>(index),
                         ImageGenerationReferenceRoleName(role));
}

nlohmann::json ReferenceOriginJson(const HeadlessImageReferenceSource& source) {
  if (source.path.has_value()) {
    return {{"kind", "path"}, {"path", *source.path}};
  }
  return {{"kind", "source-artwork"}, {"source_artwork_id", *source.source_artwork_id}};
}

nlohmann::json ReferenceRecordJson(const ResolvedHeadlessImageReference& reference, size_t index,
                                   std::string_view artifact_path) {
  return {
      {"order", index},
      {"role", std::string(ImageGenerationReferenceRoleName(reference.source.role))},
      {"artifact", artifact_path},
      {"origin", ReferenceOriginJson(reference.source)},
      {"width", reference.image.width},
      {"height", reference.image.height},
      {"rgba_sha256", reference.content_digest},
  };
}

absl::StatusOr<std::vector<ResolvedHeadlessImageReference>> ResolveReferenceManifest(
    Api& api, const HeadlessImageReferenceManifest& manifest,
    const ImageGenerationCapabilities& capabilities) {
  RETURN_IF_ERROR(ValidateReferenceManifest(manifest));
  if (capabilities.maximum_reference_images < static_cast<int>(manifest.references.size()) ||
      capabilities.maximum_reference_pixels <= 0) {
    return absl::FailedPreconditionError(
        "selected image provider cannot accept the headless reference manifest");
  }
  const std::filesystem::path reference_root = manifest.manifest_path.parent_path();
  int64_t remaining_pixels = capabilities.maximum_reference_pixels;
  std::vector<ResolvedHeadlessImageReference> resolved;
  resolved.reserve(manifest.references.size());
  for (const HeadlessImageReferenceSource& source : manifest.references) {
    ASSIGN_OR_RETURN(ResolvedHeadlessImageReference reference,
                     ResolveHeadlessImageReference(api, source, reference_root, remaining_pixels));
    remaining_pixels -= static_cast<int64_t>(reference.image.width) * reference.image.height;
    resolved.push_back(std::move(reference));
  }
  return resolved;
}

std::vector<ImageGenerationReference> ProviderReferences(
    const std::vector<ResolvedHeadlessImageReference>& resolved) {
  std::vector<ImageGenerationReference> references;
  references.reserve(resolved.size());
  for (const ResolvedHeadlessImageReference& reference : resolved) {
    references.push_back({.role = reference.source.role, .image = reference.image});
  }
  return references;
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not open JSON output: ", path.string()));
  }
  stream << json.dump(2) << '\n';
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write JSON output: ", path.string()));
  }
  return absl::OkStatus();
}

std::string ParallaxInstructions(const ParallaxArtworkRecipe& recipe,
                                 const ImageGenerationCapabilities& capabilities) {
  std::string role;
  if (recipe.pipeline.alpha_role == ParallaxArtworkAlphaRole::kOpaquePlate) {
    role = kOpaquePlateGenerationGuidance;
  } else if (capabilities.supports_transparency) {
    role = kTransparentOverlayGenerationGuidance;
  } else {
    role = absl::StrFormat(
        "%s Use exactly the recipe matte color #%02X%02X%02X (RGBA %d, %d, %d, %d) for "
        "every background pixel; do not substitute another chroma-key color.",
        kMatteOverlayGenerationGuidance, static_cast<int>(recipe.pipeline.matte_color.r),
        static_cast<int>(recipe.pipeline.matte_color.g),
        static_cast<int>(recipe.pipeline.matte_color.b),
        static_cast<int>(recipe.pipeline.matte_color.r),
        static_cast<int>(recipe.pipeline.matte_color.g),
        static_cast<int>(recipe.pipeline.matte_color.b),
        static_cast<int>(recipe.pipeline.matte_color.a));
  }
  const char* repetition = recipe.pipeline.review_repeat_x
                               ? kSeamlessHorizontalGenerationGuidance
                               : kNonRepeatingHorizontalGenerationGuidance;
  return absl::StrCat(kDefaultParallaxGenerationInstructions, "\n\n", role, "\n",
                      kDefaultBackgroundPaletteGuidance, "\n", repetition);
}

std::string ParallaxRedrawInstructions(const ParallaxArtworkRecipe& recipe,
                                       const ImageGenerationCapabilities& capabilities) {
  return absl::StrCat(
      ParallaxInstructions(recipe, capabilities),
      "\n\nEdit the supplied reference image. Preserve its subject identity, palette, pixel-art "
      "language, lighting, scale, and layer role. Change only what the subject request asks for; "
      "do not replace it with an unrelated composition.");
}

std::string PropInstructions(const PropRecipe& recipe) {
  const int output_width = recipe.style.tile_size * recipe.pipeline.composition.canvas_tiles_wide;
  const int output_height = recipe.style.tile_size * recipe.pipeline.composition.canvas_tiles_high;
  return absl::StrFormat(
      "%s\n\nProduction target:\n%s The deterministic asset pipeline will reduce and "
      "quantize the source to an exact %d x %d pixel runtime texture. Design for that final "
      "size with broad value groups, crisp hard-edged shapes, and details large enough to "
      "survive reduction. Do not reinterpret the request as hand-painted, photorealistic, or "
      "concept art, and avoid soft gradients and subpixel texture. This is a side-view game: "
      "for scale, the player hitbox is %d pixels wide by %d pixels tall.",
      kDefaultPropGenerationInstructions, kModernPixelArtStyleGuidance, output_width, output_height,
      recipe.style.tile_size, recipe.style.tile_size * 2);
}

absl::StatusOr<CandidatePlan> BuildPlan(Api& api, const ImageGenerationCapabilities& capabilities,
                                        const HeadlessAssetGenerationRequest& request) {
  const GeneratedArtworkProvenance placeholder_provenance{
      .provider = "pending",
      .model = "pending",
      .submitted_prompt = request.prompt,
      .generated_at_utc = "1970-01-01T00:00:00Z",
  };
  const GeneratedAssetSourceCandidate placeholder_source{
      .relative_path = kProcessedFilename,
      .width = 1,
      .height = 1,
      .content_digest = std::string(64, '0'),
      .provenance = placeholder_provenance,
  };
  if (request.kind == "prop") {
    ASSIGN_OR_RETURN(PropRecipe * recipe, api.GetPropRecipe(request.template_recipe_id));
    if (recipe == nullptr) {
      return absl::FailedPreconditionError("prop template recipe lookup returned null");
    }
    RETURN_IF_ERROR(ValidatePropRecipe(*recipe));
    PropRecipe template_recipe = *recipe;
    if (request.prop_canvas_tiles_wide.has_value()) {
      template_recipe.pipeline.composition.canvas_tiles_wide = *request.prop_canvas_tiles_wide;
      template_recipe.pipeline.composition.canvas_tiles_high = *request.prop_canvas_tiles_high;
    }
    if (request.prop_attachment_mode.has_value()) {
      template_recipe.pipeline.composition.attachment = {
          .mode = *request.prop_attachment_mode,
          .free_anchor = request.prop_free_anchor,
      };
    }
    RETURN_IF_ERROR(
        ValidatePropArtworkPipelineConfig(template_recipe.pipeline, template_recipe.style));
    const int output_width =
        template_recipe.style.tile_size * template_recipe.pipeline.composition.canvas_tiles_wide;
    const int output_height =
        template_recipe.style.tile_size * template_recipe.pipeline.composition.canvas_tiles_high;
    template_recipe.expected_frame = {
        .index = 0,
        .texture_x = 0,
        .texture_y = 0,
        .texture_w = output_width,
        .texture_h = output_height,
        .render_w = output_width,
        .render_h = output_height,
        .frames_per_cycle = 0,
        .offset_x =
            template_recipe.pipeline.composition.attachment.mode == PropAttachmentMode::kFree
                ? -template_recipe.pipeline.composition.attachment.free_anchor->x
                : -output_width / 2,
        .offset_y =
            template_recipe.pipeline.composition.attachment.mode == PropAttachmentMode::kFree
                ? -template_recipe.pipeline.composition.attachment.free_anchor->y
            : template_recipe.pipeline.composition.attachment.mode == PropAttachmentMode::kCeiling
                ? 0
                : -(output_height - 1),
    };
    RETURN_IF_ERROR(ValidatePropRecipe(template_recipe));
    const PropAssetIds ids{
        .texture_id = GenerateGuid(),
        .sprite_id = GenerateGuid(),
        .blueprint_id = GenerateGuid(),
        .recipe_id = GenerateGuid(),
    };
    const GeneratedPropCreationCandidate candidate{
        .asset_id = ids.recipe_id,
        .name = request.name,
        .source = placeholder_source,
        .template_recipe = template_recipe,
        .ids = ids,
    };
    return CandidatePlan{
        .asset_id = ids.recipe_id,
        .spec =
            {
                .prompt = request.prompt,
                .instructions = PropInstructions(template_recipe),
                .requested_candidates = 1,
                .target_aspect =
                    {
                        .width = template_recipe.pipeline.composition.canvas_tiles_wide,
                        .height = template_recipe.pipeline.composition.canvas_tiles_high,
                    },
                .transparency = capabilities.supports_transparency
                                    ? ImageTransparencyPreference::kPreferTransparent
                                    : ImageTransparencyPreference::kNoPreference,
            },
        .candidate_template = GeneratedPropCreationCandidateToJson(candidate),
    };
  }

  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * recipe,
                   api.GetParallaxArtworkRecipe(request.template_recipe_id));
  if (recipe == nullptr) {
    return absl::FailedPreconditionError("parallax template recipe lookup returned null");
  }
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(*recipe));
  if (recipe->pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
      !capabilities.supports_transparency &&
      recipe->pipeline.overlay_extraction != ParallaxArtworkOverlayExtraction::kRemoveSolidMatte) {
    return absl::FailedPreconditionError(
        "image provider cannot supply transparency and the template recipe cannot remove a "
        "solid matte");
  }
  const ParallaxArtworkAssetIds ids{
      .texture_id = GenerateGuid(),
      .recipe_id = GenerateGuid(),
  };
  const GeneratedParallaxArtworkCreationCandidate candidate{
      .asset_id = ids.recipe_id,
      .name = request.name,
      .source = placeholder_source,
      .template_recipe = *recipe,
      .ids = ids,
  };
  return CandidatePlan{
      .asset_id = ids.recipe_id,
      .spec =
          {
              .prompt = request.prompt,
              .instructions = ParallaxInstructions(*recipe, capabilities),
              .requested_candidates = 1,
              .target_aspect = {.width = recipe->pipeline.target_width,
                                .height = recipe->pipeline.target_height},
              .transparency =
                  recipe->pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
                          capabilities.supports_transparency
                      ? ImageTransparencyPreference::kPreferTransparent
                      : ImageTransparencyPreference::kNoPreference,
          },
      .candidate_template = GeneratedParallaxArtworkCreationCandidateToJson(candidate),
  };
}

absl::StatusOr<ImageGenerationResult> AwaitGeneration(ImageGenerationService& service,
                                                      ImageGenerationSpec spec) {
  ASSIGN_OR_RETURN(const uint64_t id, service.engine().Submit(std::move(spec)));
  const absl::Time deadline = absl::Now() + kGenerationTimeout;
  while (absl::Now() < deadline) {
    std::optional<GenerationEvent> event = service.engine().NextEvent(id);
    if (event.has_value()) return std::move(event->result);
    absl::SleepFor(absl::Milliseconds(2));
  }
  const absl::Status cancel = service.engine().Cancel(id);
  if (!cancel.ok()) {
    return absl::DeadlineExceededError(
        absl::StrCat("image generation timed out and cancellation failed: ", cancel.message()));
  }
  return absl::DeadlineExceededError("image generation timed out");
}

GeneratedArtworkPostprocessConfig PreservationConfig(const RgbaImage& image) {
  return GeneratedArtworkPostprocessConfig{
      .output_width = image.width,
      .output_height = image.height,
      .pixel_block_size = 1,
      .background_policy = GeneratedArtworkBackgroundPolicy::kPreserve,
      .palette_policy = GeneratedArtworkPalettePolicy::kPreserve,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kPreserve,
      .minimum_visible_pixels = 1,
      .minimum_transparent_border = 0,
  };
}

absl::StatusOr<nlohmann::json> FinalizeCandidate(const HeadlessAssetGenerationRequest& request,
                                                 const CandidatePlan& plan,
                                                 const GeneratedAssetSourceCandidate& source) {
  if (request.kind == "prop") {
    ASSIGN_OR_RETURN(GeneratedPropCreationCandidate candidate,
                     GeneratedPropCreationCandidateFromJson(plan.candidate_template));
    candidate.source = source;
    return GeneratedPropCreationCandidateToJson(candidate);
  }
  ASSIGN_OR_RETURN(GeneratedParallaxArtworkCreationCandidate candidate,
                   GeneratedParallaxArtworkCreationCandidateFromJson(plan.candidate_template));
  candidate.source = source;
  return GeneratedParallaxArtworkCreationCandidateToJson(candidate);
}

absl::StatusOr<HeadlessAssetGenerationResult> PublishCreationCandidateBundle(
    const HeadlessAssetGenerationRequest& request, const CandidatePlan& plan,
    const RgbaImage& original, GeneratedArtworkProvenance provenance,
    const std::vector<ResolvedHeadlessImageReference>& references = {}) {
  ASSIGN_OR_RETURN(GeneratedArtworkPostprocessResult processed,
                   PostprocessGeneratedArtwork(original, std::vector<RgbaColor>{},
                                               PreservationConfig(original)));
  ASSIGN_OR_RETURN(const std::string original_digest, RgbaImageDigest(original));
  ASSIGN_OR_RETURN(const std::string processed_digest, RgbaImageDigest(processed.finished));
  const GeneratedAssetSourceCandidate source{
      .relative_path = kProcessedFilename,
      .width = processed.finished.width,
      .height = processed.finished.height,
      .content_digest = processed_digest,
      .provenance = std::move(provenance),
  };
  ASSIGN_OR_RETURN(nlohmann::json candidate, FinalizeCandidate(request, plan, source));
  nlohmann::json artifacts = nlohmann::json::array({{{"id", "generated-source"},
                                                     {"path", kOriginalFilename},
                                                     {"rgba_sha256", original_digest},
                                                     {"width", original.width},
                                                     {"height", original.height}},
                                                    {{"id", "processed-source"},
                                                     {"path", kProcessedFilename},
                                                     {"rgba_sha256", processed_digest},
                                                     {"width", processed.finished.width},
                                                     {"height", processed.finished.height}}});
  nlohmann::json reference_records = nlohmann::json::array();
  for (size_t index = 0; index < references.size(); ++index) {
    const std::string filename = ReferenceArtifactFilename(index, references[index].source.role);
    nlohmann::json record = ReferenceRecordJson(references[index], index, filename);
    reference_records.push_back(record);
    record["id"] = absl::StrCat("reference-", index);
    record["path"] = filename;
    record.erase("artifact");
    artifacts.push_back(std::move(record));
  }
  const nlohmann::json manifest{
      {"schema_version", 1},
      {"bundle", "generated-asset-candidate"},
      {"kind", request.kind},
      {"asset_id", plan.asset_id},
      {"template_recipe_id", request.template_recipe_id},
      {"candidate", kCandidateFilename},
      {"references", reference_records},
      {"artifacts", artifacts},
      {"postprocess",
       {{"background_policy", "preserve"},
        {"palette_policy", "preserve"},
        {"alpha_policy", "preserve"},
        {"visible_pixels", processed.diagnostics.visible_pixels}}},
  };

  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      request.output_path, [&](const std::filesystem::path& staging) -> absl::Status {
        RETURN_IF_ERROR(WritePng((staging / kOriginalFilename).string(), original.width,
                                 original.height, original.pixels));
        RETURN_IF_ERROR(WritePng((staging / kProcessedFilename).string(), processed.finished.width,
                                 processed.finished.height, processed.finished.pixels));
        for (size_t index = 0; index < references.size(); ++index) {
          const ResolvedHeadlessImageReference& reference = references[index];
          const std::string filename = ReferenceArtifactFilename(index, reference.source.role);
          RETURN_IF_ERROR(WritePng((staging / filename).string(), reference.image.width,
                                   reference.image.height, reference.image.pixels));
        }
        RETURN_IF_ERROR(WriteJson(staging / kCandidateFilename, candidate));
        return WriteJson(staging / kManifestFilename, manifest);
      }));
  return HeadlessAssetGenerationResult{
      .asset_id = plan.asset_id,
      .candidate_path = (std::filesystem::path(request.output_path) / kCandidateFilename).string(),
      .manifest_path = (std::filesystem::path(request.output_path) / kManifestFilename).string(),
  };
}

}  // namespace

absl::Status ValidateHeadlessImageReferenceSource(const HeadlessImageReferenceSource& source) {
  if (ImageGenerationReferenceRoleName(source.role).empty()) {
    return absl::InvalidArgumentError("headless image reference role is invalid");
  }
  if (source.path.has_value() == source.source_artwork_id.has_value()) {
    return absl::InvalidArgumentError(
        "headless image reference must set exactly one of path or source_artwork_id");
  }
  if (source.source_artwork_id.has_value()) {
    if (source.source_artwork_id->empty()) {
      return absl::InvalidArgumentError("headless image reference source_artwork_id is empty");
    }
    return absl::OkStatus();
  }
  const std::filesystem::path path(*source.path);
  if (source.path->empty() || path.is_absolute() || path.has_root_path() ||
      path.lexically_normal() != path || path == ".") {
    return absl::InvalidArgumentError(
        "headless image reference path must be normalized and relative to its manifest");
  }
  for (const std::filesystem::path& component : path) {
    if (component == "..") {
      return absl::InvalidArgumentError(
          "headless image reference path cannot escape its manifest directory");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<HeadlessImageReferenceManifest> LoadHeadlessImageReferenceManifest(
    const std::filesystem::path& path) {
  if (path.empty()) return absl::InvalidArgumentError("headless reference manifest path is empty");
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_path,
                   CanonicalPath(path, "headless reference manifest"));
  std::error_code error;
  if (!std::filesystem::is_regular_file(canonical_path, error) || error) {
    return absl::NotFoundError(
        absl::StrCat("headless reference manifest is not a regular file: ", path.string()));
  }
  const uintmax_t manifest_bytes = std::filesystem::file_size(canonical_path, error);
  if (error) {
    return absl::NotFoundError(
        absl::StrCat("could not size headless reference manifest: ", error.message()));
  }
  if (manifest_bytes == 0 || manifest_bytes > kMaximumReferenceManifestBytes) {
    return absl::ResourceExhaustedError(
        "headless reference manifest must contain between 1 byte and 1 MiB");
  }
  std::ifstream stream(canonical_path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("could not open headless reference manifest: ", path.string()));
  }
  std::string encoded(static_cast<size_t>(manifest_bytes), '\0');
  stream.read(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  if (static_cast<size_t>(stream.gcount()) != encoded.size() ||
      stream.peek() != std::char_traits<char>::eof() || stream.bad()) {
    return absl::DataLossError("headless reference manifest changed while it was being read");
  }
  const nlohmann::json json = nlohmann::json::parse(encoded, nullptr, false);
  if (json.is_discarded()) {
    return absl::InvalidArgumentError("could not parse headless reference manifest");
  }
  if (!json.is_object() || json.size() != 2 || !json.contains("schema_version") ||
      !json.contains("references")) {
    return absl::InvalidArgumentError(
        "headless reference manifest must contain only schema_version and references");
  }
  try {
    if (json.at("schema_version").get<int>() != kReferenceManifestSchemaVersion) {
      return absl::FailedPreconditionError("unsupported headless reference manifest schema");
    }
  } catch (const std::exception& exception) {
    return absl::InvalidArgumentError(
        absl::StrCat("headless reference manifest schema_version is invalid: ", exception.what()));
  }
  if (!json.at("references").is_array()) {
    return absl::InvalidArgumentError("headless reference manifest references must be an array");
  }
  if (json.at("references").size() != 2) {
    return absl::InvalidArgumentError(
        "headless reference manifest must contain exactly two references");
  }

  HeadlessImageReferenceManifest manifest{.manifest_path = canonical_path};
  for (const nlohmann::json& entry : json.at("references")) {
    if (!entry.is_object() || !entry.contains("role")) {
      return absl::InvalidArgumentError(
          "headless reference manifest entry must be an object with a role");
    }
    const bool has_path = entry.contains("path");
    const bool has_source_artwork_id = entry.contains("source_artwork_id");
    if (entry.size() != 2 || has_path == has_source_artwork_id) {
      return absl::InvalidArgumentError(
          "headless reference manifest entry must contain role and exactly one source");
    }
    HeadlessImageReferenceSource source;
    try {
      ASSIGN_OR_RETURN(source.role,
                       ParseImageGenerationReferenceRole(entry.at("role").get<std::string>()));
      if (has_path) {
        source.path = entry.at("path").get<std::string>();
      } else {
        source.source_artwork_id = entry.at("source_artwork_id").get<std::string>();
      }
    } catch (const std::exception& exception) {
      return absl::InvalidArgumentError(
          absl::StrCat("headless reference manifest entry is invalid: ", exception.what()));
    }
    RETURN_IF_ERROR(ValidateHeadlessImageReferenceSource(source));
    manifest.references.push_back(std::move(source));
  }
  RETURN_IF_ERROR(ValidateReferenceManifest(manifest));
  return manifest;
}

absl::StatusOr<ResolvedHeadlessImageReference> ResolveHeadlessImageReference(
    Api& api, const HeadlessImageReferenceSource& source,
    const std::filesystem::path& reference_root, int64_t maximum_pixels) {
  RETURN_IF_ERROR(ValidateHeadlessImageReferenceSource(source));
  if (maximum_pixels <= 0) {
    return absl::InvalidArgumentError("headless image reference pixel limit must be positive");
  }
  SourceArtworkLimits limits;
  limits.maximum_pixels = static_cast<size_t>(
      std::min<int64_t>(maximum_pixels, static_cast<int64_t>(limits.maximum_pixels)));
  limits.maximum_bytes = std::min(limits.maximum_bytes, limits.maximum_pixels * 4);

  RgbaImage image;
  if (source.path.has_value()) {
    ASSIGN_OR_RETURN(const std::filesystem::path resolved_path,
                     ResolveConfinedReferencePath(reference_root, *source.path));
    ASSIGN_OR_RETURN(image, ReadBoundedReferenceImage(resolved_path, limits.maximum_pixels));
  } else {
    ASSIGN_OR_RETURN(SourceArtwork * artwork, api.GetSourceArtwork(*source.source_artwork_id));
    if (artwork == nullptr) {
      return absl::FailedPreconditionError(
          "headless reference source artwork lookup returned null");
    }
    RETURN_IF_ERROR(ValidateSourceArtwork(*artwork));
    if (artwork->id != *source.source_artwork_id) {
      return absl::FailedPreconditionError(
          "headless reference source artwork lookup returned the wrong resource");
    }
    const int64_t defined_pixels = static_cast<int64_t>(artwork->width) * artwork->height;
    if (artwork->width > limits.maximum_width || artwork->height > limits.maximum_height ||
        defined_pixels > static_cast<int64_t>(limits.maximum_pixels)) {
      return absl::ResourceExhaustedError(
          "headless reference source artwork definition exceeds the reference pixel limits");
    }
    ASSIGN_OR_RETURN(image,
                     api.ReadSourceArtworkPixels(artwork->id, static_cast<size_t>(defined_pixels)));
    RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits));
    ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
    if (artwork->width != image.width || artwork->height != image.height ||
        artwork->content_digest != digest) {
      return absl::FailedPreconditionError(
          "headless reference pixels do not match their source artwork definition");
    }
    return ResolvedHeadlessImageReference{
        .source = source,
        .image = std::move(image),
        .content_digest = digest,
    };
  }

  RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  return ResolvedHeadlessImageReference{
      .source = source,
      .image = std::move(image),
      .content_digest = digest,
  };
}

absl::Status ValidateHeadlessAssetGenerationRequest(const HeadlessAssetGenerationRequest& request) {
  if (request.kind != "prop" && request.kind != "parallax-artwork") {
    return absl::InvalidArgumentError(
        "headless generation kind must be 'prop' or 'parallax-artwork'");
  }
  if (request.template_recipe_id.empty() || request.name.empty() || request.prompt.empty() ||
      request.output_path.empty()) {
    return absl::InvalidArgumentError(
        "headless generation recipe ID, name, prompt, and output must be non-empty");
  }
  const bool has_prop_width = request.prop_canvas_tiles_wide.has_value();
  const bool has_prop_height = request.prop_canvas_tiles_high.has_value();
  const bool has_prop_overrides = has_prop_width || has_prop_height ||
                                  request.prop_attachment_mode.has_value() ||
                                  request.prop_free_anchor.has_value();
  if (request.kind != "prop" && has_prop_overrides) {
    return absl::InvalidArgumentError("prop composition overrides require kind 'prop'");
  }
  if (has_prop_width != has_prop_height) {
    return absl::InvalidArgumentError(
        "prop canvas width and height overrides must be provided together");
  }
  if (has_prop_width &&
      (*request.prop_canvas_tiles_wide <= 0 || *request.prop_canvas_tiles_high <= 0)) {
    return absl::InvalidArgumentError("prop canvas dimensions must be positive");
  }
  if (request.prop_attachment_mode == PropAttachmentMode::kFree &&
      !request.prop_free_anchor.has_value()) {
    return absl::InvalidArgumentError("free prop attachment requires an explicit anchor");
  }
  if (request.prop_free_anchor.has_value() &&
      request.prop_attachment_mode != PropAttachmentMode::kFree) {
    return absl::InvalidArgumentError("free prop anchor requires attachment mode 'free'");
  }
  if (request.reference_manifest.has_value()) {
    RETURN_IF_ERROR(ValidateReferenceManifest(*request.reference_manifest));
  }
  return ValidateNewDirectoryDestination(request.output_path);
}

absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetGenerationRequest& request) {
  RETURN_IF_ERROR(ValidateHeadlessAssetGenerationRequest(request));
  const ImageGenerationCapabilities capabilities = service.engine().Capabilities();
  ASSIGN_OR_RETURN(CandidatePlan plan, BuildPlan(api, capabilities, request));
  std::vector<ResolvedHeadlessImageReference> references;
  if (request.reference_manifest.has_value()) {
    ASSIGN_OR_RETURN(references,
                     ResolveReferenceManifest(api, *request.reference_manifest, capabilities));
    plan.spec.references = ProviderReferences(references);
  }
  ASSIGN_OR_RETURN(ImageGenerationResult generated, AwaitGeneration(service, std::move(plan.spec)));
  RETURN_IF_ERROR(ValidateImageGenerationResult(generated));
  if (generated.candidates.size() != 1) {
    return absl::FailedPreconditionError(
        "headless generation requires exactly one provider candidate");
  }
  ImageGenerationCandidate provider_candidate = std::move(generated.candidates.front());
  return PublishCreationCandidateBundle(request, plan, provider_candidate.image,
                                        {
                                            .provider = generated.provider,
                                            .model = generated.model,
                                            .submitted_prompt = generated.submitted_prompt,
                                            .revised_prompt = provider_candidate.revised_prompt,
                                            .provider_request_id = generated.provider_request_id,
                                            .generated_at_utc = CurrentUtcTimestamp(),
                                        },
                                        references);
}

absl::Status ValidateHeadlessAssetStagingRequest(const HeadlessAssetStagingRequest& request) {
  RETURN_IF_ERROR(ValidateHeadlessAssetGenerationRequest({
      .kind = request.kind,
      .template_recipe_id = request.template_recipe_id,
      .name = request.name,
      .prompt = request.prompt,
      .output_path = request.output_path,
      .prop_canvas_tiles_wide = request.prop_canvas_tiles_wide,
      .prop_canvas_tiles_high = request.prop_canvas_tiles_high,
      .prop_attachment_mode = request.prop_attachment_mode,
      .prop_free_anchor = request.prop_free_anchor,
  }));
  if (request.provider.empty() || request.model.empty()) {
    return absl::InvalidArgumentError("headless staging provider and model must be non-empty");
  }
  return absl::OkStatus();
}

absl::StatusOr<HeadlessAssetGenerationResult> StageAssetCandidateBundle(
    Api& api, const RgbaImage& image, const HeadlessAssetStagingRequest& request) {
  RETURN_IF_ERROR(ValidateHeadlessAssetStagingRequest(request));
  if (!image.IsValid()) {
    return absl::InvalidArgumentError("headless staging input image is invalid");
  }
  const HeadlessAssetGenerationRequest generation_request{
      .kind = request.kind,
      .template_recipe_id = request.template_recipe_id,
      .name = request.name,
      .prompt = request.prompt,
      .output_path = request.output_path,
      .prop_canvas_tiles_wide = request.prop_canvas_tiles_wide,
      .prop_canvas_tiles_high = request.prop_canvas_tiles_high,
      .prop_attachment_mode = request.prop_attachment_mode,
      .prop_free_anchor = request.prop_free_anchor,
  };
  ASSIGN_OR_RETURN(CandidatePlan plan,
                   BuildPlan(api, ImageGenerationCapabilities{}, generation_request));
  return PublishCreationCandidateBundle(generation_request, plan, image,
                                        {
                                            .provider = request.provider,
                                            .model = request.model,
                                            .submitted_prompt = request.prompt,
                                            .generated_at_utc = CurrentUtcTimestamp(),
                                        });
}

absl::Status ValidateHeadlessAssetRedrawRequest(const HeadlessAssetRedrawRequest& request) {
  if (request.asset_id.empty() || request.prompt.empty() || request.output_path.empty()) {
    return absl::InvalidArgumentError(
        "headless redraw asset ID, prompt, and output must be non-empty");
  }
  return ValidateNewDirectoryDestination(request.output_path);
}

absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetRedrawCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetRedrawRequest& request) {
  RETURN_IF_ERROR(ValidateHeadlessAssetRedrawRequest(request));
  const ImageGenerationCapabilities capabilities = service.engine().Capabilities();
  if (capabilities.maximum_reference_images < 1 || capabilities.maximum_reference_pixels <= 0) {
    return absl::FailedPreconditionError(
        "selected image provider does not support reference-image redraws");
  }

  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * loaded_recipe,
                   api.GetParallaxArtworkRecipe(request.asset_id));
  if (loaded_recipe == nullptr) {
    return absl::FailedPreconditionError("parallax artwork recipe lookup returned null");
  }
  const ParallaxArtworkRecipe recipe = *loaded_recipe;
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  const HeadlessImageReferenceSource reference_source{
      .role = ImageGenerationReferenceRole::kEditSource,
      .source_artwork_id = recipe.source_artwork_id,
  };
  ASSIGN_OR_RETURN(ResolvedHeadlessImageReference resolved_reference,
                   ResolveHeadlessImageReference(api, reference_source, {},
                                                 capabilities.maximum_reference_pixels));
  const RgbaImage& reference = resolved_reference.image;
  const std::string& reference_digest = resolved_reference.content_digest;
  ASSIGN_OR_RETURN(RgbaImage texture, api.ReadTexturePixels(recipe.texture_id));
  ASSIGN_OR_RETURN(const std::string texture_digest, RgbaImageDigest(texture));
  if (recipe.final_pixel_digest != texture_digest) {
    return absl::FailedPreconditionError(
        "generated parallax pixels do not match their recipe definition");
  }

  ImageGenerationSpec spec{
      .prompt = request.prompt,
      .instructions = ParallaxRedrawInstructions(recipe, capabilities),
      .requested_candidates = 1,
      .target_aspect = {.width = recipe.pipeline.target_width,
                        .height = recipe.pipeline.target_height},
      .transparency = recipe.pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
                              capabilities.supports_transparency
                          ? ImageTransparencyPreference::kPreferTransparent
                          : ImageTransparencyPreference::kNoPreference,
      .references = {{.role = ImageGenerationReferenceRole::kEditSource, .image = reference}},
  };
  ASSIGN_OR_RETURN(ImageGenerationResult generated, AwaitGeneration(service, std::move(spec)));
  RETURN_IF_ERROR(ValidateImageGenerationResult(generated));
  if (generated.candidates.size() != 1) {
    return absl::FailedPreconditionError("headless redraw requires exactly one provider candidate");
  }
  ImageGenerationCandidate provider_candidate = std::move(generated.candidates.front());
  ASSIGN_OR_RETURN(GeneratedArtworkPostprocessResult processed,
                   PostprocessGeneratedArtwork(provider_candidate.image, std::vector<RgbaColor>{},
                                               PreservationConfig(provider_candidate.image)));
  ASSIGN_OR_RETURN(const std::string original_digest, RgbaImageDigest(provider_candidate.image));
  ASSIGN_OR_RETURN(const std::string processed_digest, RgbaImageDigest(processed.finished));
  const GeneratedParallaxArtworkRedrawCandidate candidate{
      .asset_id = recipe.id,
      .expected_source_digest = reference_digest,
      .expected_final_pixel_digest = texture_digest,
      .source =
          {
              .relative_path = kProcessedFilename,
              .width = processed.finished.width,
              .height = processed.finished.height,
              .content_digest = processed_digest,
              .provenance =
                  {
                      .provider = generated.provider,
                      .model = generated.model,
                      .submitted_prompt = generated.submitted_prompt,
                      .revised_prompt = provider_candidate.revised_prompt,
                      .provider_request_id = generated.provider_request_id,
                      .generated_at_utc = CurrentUtcTimestamp(),
                  },
          },
  };
  const nlohmann::json candidate_json = GeneratedParallaxArtworkRedrawCandidateToJson(candidate);
  RETURN_IF_ERROR(GeneratedParallaxArtworkRedrawCandidateFromJson(candidate_json).status());
  const nlohmann::json reference_record =
      ReferenceRecordJson(resolved_reference, 0, kReferenceFilename);
  const nlohmann::json manifest{
      {"schema_version", 1},
      {"bundle", "generated-asset-redraw-candidate"},
      {"kind", "parallax-artwork"},
      {"asset_id", recipe.id},
      {"candidate", kCandidateFilename},
      {"base", {{"source_rgba_sha256", reference_digest}, {"final_rgba_sha256", texture_digest}}},
      {"references", nlohmann::json::array({reference_record})},
      {"artifacts",
       {{{"id", "reference-source"},
         {"path", kReferenceFilename},
         {"order", 0},
         {"role", "edit-source"},
         {"origin", ReferenceOriginJson(resolved_reference.source)},
         {"rgba_sha256", reference_digest},
         {"width", reference.width},
         {"height", reference.height}},
        {{"id", "generated-source"},
         {"path", kOriginalFilename},
         {"rgba_sha256", original_digest},
         {"width", provider_candidate.image.width},
         {"height", provider_candidate.image.height}},
        {{"id", "processed-source"},
         {"path", kProcessedFilename},
         {"rgba_sha256", processed_digest},
         {"width", processed.finished.width},
         {"height", processed.finished.height}}}},
  };

  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      request.output_path, [&](const std::filesystem::path& staging) -> absl::Status {
        RETURN_IF_ERROR(WritePng((staging / kReferenceFilename).string(), reference.width,
                                 reference.height, reference.pixels));
        RETURN_IF_ERROR(WritePng((staging / kOriginalFilename).string(),
                                 provider_candidate.image.width, provider_candidate.image.height,
                                 provider_candidate.image.pixels));
        RETURN_IF_ERROR(WritePng((staging / kProcessedFilename).string(), processed.finished.width,
                                 processed.finished.height, processed.finished.pixels));
        RETURN_IF_ERROR(WriteJson(staging / kCandidateFilename, candidate_json));
        return WriteJson(staging / kManifestFilename, manifest);
      }));
  return HeadlessAssetGenerationResult{
      .asset_id = recipe.id,
      .candidate_path = (std::filesystem::path(request.output_path) / kCandidateFilename).string(),
      .manifest_path = (std::filesystem::path(request.output_path) / kManifestFilename).string(),
  };
}

}  // namespace zebes
