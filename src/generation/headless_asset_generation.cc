#include "generation/headless_asset_generation.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "artwork/generated_artwork_postprocessor.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prop_recipe.h"
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

struct CandidatePlan {
  std::string asset_id;
  ImageGenerationSpec spec;
  nlohmann::json candidate_template;
};

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
    role = kMatteOverlayGenerationGuidance;
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

absl::StatusOr<CandidatePlan> BuildPlan(Api& api, ImageGenerationService& service,
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
        .template_recipe = *recipe,
        .ids = ids,
    };
    return CandidatePlan{
        .asset_id = ids.recipe_id,
        .spec =
            {
                .prompt = request.prompt,
                .instructions = kDefaultPropGenerationInstructions,
                .requested_candidates = 1,
                .target_aspect = {.width = 1, .height = 1},
                .transparency = service.engine().Capabilities().supports_transparency
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
              .instructions = ParallaxInstructions(*recipe, service.engine().Capabilities()),
              .requested_candidates = 1,
              .target_aspect = {.width = recipe->pipeline.target_width,
                                .height = recipe->pipeline.target_height},
              .transparency =
                  recipe->pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
                          service.engine().Capabilities().supports_transparency
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

}  // namespace

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
  return ValidateNewDirectoryDestination(request.output_path);
}

absl::StatusOr<HeadlessAssetGenerationResult> GenerateAssetCandidateBundle(
    Api& api, ImageGenerationService& service, const HeadlessAssetGenerationRequest& request) {
  RETURN_IF_ERROR(ValidateHeadlessAssetGenerationRequest(request));
  ASSIGN_OR_RETURN(CandidatePlan plan, BuildPlan(api, service, request));
  ASSIGN_OR_RETURN(ImageGenerationResult generated, AwaitGeneration(service, std::move(plan.spec)));
  RETURN_IF_ERROR(ValidateImageGenerationResult(generated));
  if (generated.candidates.size() != 1) {
    return absl::FailedPreconditionError(
        "headless generation requires exactly one provider candidate");
  }
  ImageGenerationCandidate provider_candidate = std::move(generated.candidates.front());
  ASSIGN_OR_RETURN(GeneratedArtworkPostprocessResult processed,
                   PostprocessGeneratedArtwork(provider_candidate.image, std::vector<RgbaColor>{},
                                               PreservationConfig(provider_candidate.image)));
  ASSIGN_OR_RETURN(const std::string original_digest, RgbaImageDigest(provider_candidate.image));
  ASSIGN_OR_RETURN(const std::string processed_digest, RgbaImageDigest(processed.finished));
  const GeneratedAssetSourceCandidate source{
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
  };
  ASSIGN_OR_RETURN(nlohmann::json candidate, FinalizeCandidate(request, plan, source));
  const nlohmann::json manifest{
      {"schema_version", 1},
      {"bundle", "generated-asset-candidate"},
      {"kind", request.kind},
      {"asset_id", plan.asset_id},
      {"template_recipe_id", request.template_recipe_id},
      {"candidate", kCandidateFilename},
      {"artifacts",
       {{{"id", "generated-source"},
         {"path", kOriginalFilename},
         {"rgba_sha256", original_digest},
         {"width", provider_candidate.image.width},
         {"height", provider_candidate.image.height}},
        {{"id", "processed-source"},
         {"path", kProcessedFilename},
         {"rgba_sha256", processed_digest},
         {"width", processed.finished.width},
         {"height", processed.finished.height}}}},
      {"postprocess",
       {{"background_policy", "preserve"},
        {"palette_policy", "preserve"},
        {"alpha_policy", "preserve"},
        {"visible_pixels", processed.diagnostics.visible_pixels}}},
  };

  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      request.output_path, [&](const std::filesystem::path& staging) -> absl::Status {
        RETURN_IF_ERROR(WritePng((staging / kOriginalFilename).string(),
                                 provider_candidate.image.width, provider_candidate.image.height,
                                 provider_candidate.image.pixels));
        RETURN_IF_ERROR(WritePng((staging / kProcessedFilename).string(), processed.finished.width,
                                 processed.finished.height, processed.finished.pixels));
        RETURN_IF_ERROR(WriteJson(staging / kCandidateFilename, candidate));
        return WriteJson(staging / kManifestFilename, manifest);
      }));
  return HeadlessAssetGenerationResult{
      .asset_id = plan.asset_id,
      .candidate_path = (std::filesystem::path(request.output_path) / kCandidateFilename).string(),
      .manifest_path = (std::filesystem::path(request.output_path) / kManifestFilename).string(),
  };
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
  if (!capabilities.supports_reference_image) {
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
  ASSIGN_OR_RETURN(SourceArtwork * loaded_source, api.GetSourceArtwork(recipe.source_artwork_id));
  if (loaded_source == nullptr) {
    return absl::FailedPreconditionError("parallax artwork source lookup returned null");
  }
  const SourceArtwork source = *loaded_source;
  ASSIGN_OR_RETURN(RgbaImage reference, api.ReadSourceArtworkPixels(source.id));
  ASSIGN_OR_RETURN(const std::string reference_digest, RgbaImageDigest(reference));
  if (source.width != reference.width || source.height != reference.height ||
      source.content_digest != reference_digest) {
    return absl::FailedPreconditionError(
        "retained source pixels do not match their source artwork definition");
  }
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
      .reference_image = reference,
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
  const nlohmann::json manifest{
      {"schema_version", 1},
      {"bundle", "generated-asset-redraw-candidate"},
      {"kind", "parallax-artwork"},
      {"asset_id", recipe.id},
      {"candidate", kCandidateFilename},
      {"base", {{"source_rgba_sha256", reference_digest}, {"final_rgba_sha256", texture_digest}}},
      {"artifacts",
       {{{"id", "reference-source"},
         {"path", kReferenceFilename},
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
