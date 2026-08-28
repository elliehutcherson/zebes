#include "generation/generated_asset_candidate.h"

#include <exception>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key, std::string_view context) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", key, "' is invalid: ", error.what()));
  }
}

absl::Status RequireExactObject(const nlohmann::json& json, std::initializer_list<const char*> keys,
                                std::string_view context) {
  if (!json.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object"));
  }
  std::set<std::string> expected;
  for (const char* key : keys) expected.emplace(key);
  for (const auto& [key, unused] : json.items()) {
    static_cast<void>(unused);
    if (!expected.contains(key)) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, " contains unknown field '", key, "'"));
    }
  }
  for (const std::string& key : expected) {
    if (!json.contains(key)) {
      return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
    }
  }
  return absl::OkStatus();
}

nlohmann::json OptionalStringToJson(const std::optional<std::string>& value) {
  return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

absl::StatusOr<std::optional<std::string>> OptionalStringFromJson(const nlohmann::json& json,
                                                                  const char* key,
                                                                  std::string_view context) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  if (json.at(key).is_null()) return std::nullopt;
  ASSIGN_OR_RETURN(std::string value, Required<std::string>(json, key, context));
  return value;
}

nlohmann::json ProvenanceToJson(const GeneratedArtworkProvenance& provenance) {
  return {
      {"provider", provenance.provider},
      {"model", provenance.model},
      {"submitted_prompt", provenance.submitted_prompt},
      {"revised_prompt", OptionalStringToJson(provenance.revised_prompt)},
      {"provider_request_id", OptionalStringToJson(provenance.provider_request_id)},
      {"generated_at_utc", provenance.generated_at_utc},
  };
}

absl::StatusOr<GeneratedArtworkProvenance> ProvenanceFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "generated asset candidate provenance";
  RETURN_IF_ERROR(RequireExactObject(json,
                                     {"provider", "model", "submitted_prompt", "revised_prompt",
                                      "provider_request_id", "generated_at_utc"},
                                     kContext));
  GeneratedArtworkProvenance provenance;
  ASSIGN_OR_RETURN(provenance.provider, Required<std::string>(json, "provider", kContext));
  ASSIGN_OR_RETURN(provenance.model, Required<std::string>(json, "model", kContext));
  ASSIGN_OR_RETURN(provenance.submitted_prompt,
                   Required<std::string>(json, "submitted_prompt", kContext));
  ASSIGN_OR_RETURN(provenance.revised_prompt,
                   OptionalStringFromJson(json, "revised_prompt", kContext));
  ASSIGN_OR_RETURN(provenance.provider_request_id,
                   OptionalStringFromJson(json, "provider_request_id", kContext));
  ASSIGN_OR_RETURN(provenance.generated_at_utc,
                   Required<std::string>(json, "generated_at_utc", kContext));
  return provenance;
}

nlohmann::json SourceToJson(const GeneratedAssetSourceCandidate& source) {
  return {
      {"path", source.relative_path},
      {"width", source.width},
      {"height", source.height},
      {"rgba_sha256", source.content_digest},
      {"provenance", ProvenanceToJson(source.provenance)},
  };
}

absl::StatusOr<GeneratedAssetSourceCandidate> SourceFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "generated asset candidate source";
  RETURN_IF_ERROR(
      RequireExactObject(json, {"path", "width", "height", "rgba_sha256", "provenance"}, kContext));
  GeneratedAssetSourceCandidate source;
  ASSIGN_OR_RETURN(source.relative_path, Required<std::string>(json, "path", kContext));
  ASSIGN_OR_RETURN(source.width, Required<int>(json, "width", kContext));
  ASSIGN_OR_RETURN(source.height, Required<int>(json, "height", kContext));
  ASSIGN_OR_RETURN(source.content_digest, Required<std::string>(json, "rgba_sha256", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json provenance,
                   Required<nlohmann::json>(json, "provenance", kContext));
  ASSIGN_OR_RETURN(source.provenance, ProvenanceFromJson(provenance));
  RETURN_IF_ERROR(ValidateGeneratedAssetSourceCandidate(source));
  return source;
}

absl::Status ValidateEnvelope(const nlohmann::json& json, std::string_view expected_kind,
                              std::string_view context) {
  RETURN_IF_ERROR(RequireExactObject(json,
                                     {"schema_version", "operation", "kind", "asset_id", "name",
                                      "source", "template_recipe", "output_ids"},
                                     context));
  ASSIGN_OR_RETURN(const int schema, Required<int>(json, "schema_version", context));
  if (schema != kGeneratedAssetCandidateSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat(context, " schema version ", schema,
                                                      " is not supported version ",
                                                      kGeneratedAssetCandidateSchemaVersion));
  }
  ASSIGN_OR_RETURN(const std::string operation, Required<std::string>(json, "operation", context));
  if (operation != "create") {
    return absl::InvalidArgumentError(absl::StrCat(context, " operation must be 'create'"));
  }
  ASSIGN_OR_RETURN(const std::string kind, Required<std::string>(json, "kind", context));
  if (kind != expected_kind) {
    return absl::InvalidArgumentError(absl::StrCat(context, " kind must be '", expected_kind, "'"));
  }
  return absl::OkStatus();
}

bool HasOperation(const nlohmann::json& json, std::string_view expected) {
  if (!json.is_object() || !json.contains("operation") || !json.at("operation").is_string()) {
    return false;
  }
  return json.at("operation").get<std::string>() == expected;
}

absl::Status ValidateCommon(std::string_view asset_id, std::string_view name,
                            const GeneratedAssetSourceCandidate& source,
                            std::string_view recipe_id) {
  if (asset_id.empty()) return absl::InvalidArgumentError("generated candidate asset ID is empty");
  if (name.empty()) return absl::InvalidArgumentError("generated candidate name is empty");
  if (recipe_id != asset_id) {
    return absl::InvalidArgumentError(
        "generated candidate asset ID must equal its output recipe ID");
  }
  return ValidateGeneratedAssetSourceCandidate(source);
}

}  // namespace

bool IsGeneratedAssetCreationCandidate(const nlohmann::json& json) {
  return HasOperation(json, "create");
}

bool IsGeneratedAssetRedrawCandidate(const nlohmann::json& json) {
  return HasOperation(json, "redraw");
}

absl::Status ValidateGeneratedAssetSourceCandidate(const GeneratedAssetSourceCandidate& candidate) {
  const std::filesystem::path path(candidate.relative_path);
  if (candidate.relative_path.empty() || path.is_absolute() || path.has_root_path() ||
      path.lexically_normal() != path || path.extension() != ".png") {
    return absl::InvalidArgumentError(
        "generated candidate source path must be a normalized relative PNG path");
  }
  for (const std::filesystem::path& component : path) {
    if (component == "..") {
      return absl::InvalidArgumentError("generated candidate source path cannot escape its bundle");
    }
  }
  SourceArtwork source{
      .id = "candidate-source",
      .name = "Generated candidate source",
      .source_path = candidate.relative_path,
      .provenance = candidate.provenance,
      .width = candidate.width,
      .height = candidate.height,
      .content_digest = candidate.content_digest,
  };
  return ValidateSourceArtwork(source);
}

absl::StatusOr<RgbaImage> ReadGeneratedAssetSourceCandidate(
    const std::filesystem::path& candidate_root, const GeneratedAssetSourceCandidate& candidate) {
  RETURN_IF_ERROR(ValidateGeneratedAssetSourceCandidate(candidate));
  if (candidate_root.empty()) {
    return absl::InvalidArgumentError("generated candidate needs its bundle root");
  }
  std::error_code error;
  const std::filesystem::path canonical_root =
      std::filesystem::weakly_canonical(candidate_root, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve generated candidate bundle root: ", error.message()));
  }
  const std::filesystem::path source_path =
      std::filesystem::weakly_canonical(canonical_root / candidate.relative_path, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve generated candidate source: ", error.message()));
  }
  const std::filesystem::path relative = source_path.lexically_relative(canonical_root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
    return absl::InvalidArgumentError(
        "generated candidate source resolves outside its bundle root");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, ReadPng(source_path.string()));
  if (pixels.width != candidate.width || pixels.height != candidate.height) {
    return absl::FailedPreconditionError(
        "generated candidate source dimensions do not match its document");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  if (digest != candidate.content_digest) {
    return absl::FailedPreconditionError(
        "generated candidate source pixels do not match its digest");
  }
  return pixels;
}

nlohmann::json GeneratedPropCreationCandidateToJson(
    const GeneratedPropCreationCandidate& candidate) {
  return {
      {"schema_version", kGeneratedAssetCandidateSchemaVersion},
      {"operation", "create"},
      {"kind", "prop"},
      {"asset_id", candidate.asset_id},
      {"name", candidate.name},
      {"source", SourceToJson(candidate.source)},
      {"template_recipe", PropRecipeToJson(candidate.template_recipe)},
      {"output_ids",
       {{"texture_id", candidate.ids.texture_id},
        {"sprite_id", candidate.ids.sprite_id},
        {"blueprint_id", candidate.ids.blueprint_id},
        {"recipe_id", candidate.ids.recipe_id}}},
  };
}

absl::StatusOr<GeneratedPropCreationCandidate> GeneratedPropCreationCandidateFromJson(
    const nlohmann::json& json) {
  constexpr std::string_view kContext = "generated prop creation candidate";
  RETURN_IF_ERROR(ValidateEnvelope(json, "prop", kContext));
  GeneratedPropCreationCandidate candidate;
  ASSIGN_OR_RETURN(candidate.asset_id, Required<std::string>(json, "asset_id", kContext));
  ASSIGN_OR_RETURN(candidate.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json source, Required<nlohmann::json>(json, "source", kContext));
  ASSIGN_OR_RETURN(candidate.source, SourceFromJson(source));
  ASSIGN_OR_RETURN(const nlohmann::json template_recipe,
                   Required<nlohmann::json>(json, "template_recipe", kContext));
  ASSIGN_OR_RETURN(candidate.template_recipe, PropRecipeFromJson(template_recipe));
  if (PropRecipeToJson(candidate.template_recipe) != template_recipe) {
    return absl::InvalidArgumentError(
        "generated prop template must be one exact schema-current recipe object");
  }
  ASSIGN_OR_RETURN(const nlohmann::json ids,
                   Required<nlohmann::json>(json, "output_ids", kContext));
  RETURN_IF_ERROR(RequireExactObject(ids, {"texture_id", "sprite_id", "blueprint_id", "recipe_id"},
                                     "generated prop creation candidate output IDs"));
  ASSIGN_OR_RETURN(candidate.ids.texture_id, Required<std::string>(ids, "texture_id", kContext));
  ASSIGN_OR_RETURN(candidate.ids.sprite_id, Required<std::string>(ids, "sprite_id", kContext));
  ASSIGN_OR_RETURN(candidate.ids.blueprint_id,
                   Required<std::string>(ids, "blueprint_id", kContext));
  ASSIGN_OR_RETURN(candidate.ids.recipe_id, Required<std::string>(ids, "recipe_id", kContext));
  RETURN_IF_ERROR(ValidateCommon(candidate.asset_id, candidate.name, candidate.source,
                                 candidate.ids.recipe_id));
  if (candidate.ids.texture_id.empty() || candidate.ids.sprite_id.empty() ||
      candidate.ids.blueprint_id.empty()) {
    return absl::InvalidArgumentError("generated prop output IDs must be non-empty");
  }
  return candidate;
}

nlohmann::json GeneratedParallaxArtworkCreationCandidateToJson(
    const GeneratedParallaxArtworkCreationCandidate& candidate) {
  return {
      {"schema_version", kGeneratedAssetCandidateSchemaVersion},
      {"operation", "create"},
      {"kind", "parallax-artwork"},
      {"asset_id", candidate.asset_id},
      {"name", candidate.name},
      {"source", SourceToJson(candidate.source)},
      {"template_recipe", ParallaxArtworkRecipeToJson(candidate.template_recipe)},
      {"output_ids",
       {{"texture_id", candidate.ids.texture_id}, {"recipe_id", candidate.ids.recipe_id}}},
  };
}

absl::StatusOr<GeneratedParallaxArtworkCreationCandidate>
GeneratedParallaxArtworkCreationCandidateFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "generated parallax artwork creation candidate";
  RETURN_IF_ERROR(ValidateEnvelope(json, "parallax-artwork", kContext));
  GeneratedParallaxArtworkCreationCandidate candidate;
  ASSIGN_OR_RETURN(candidate.asset_id, Required<std::string>(json, "asset_id", kContext));
  ASSIGN_OR_RETURN(candidate.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json source, Required<nlohmann::json>(json, "source", kContext));
  ASSIGN_OR_RETURN(candidate.source, SourceFromJson(source));
  ASSIGN_OR_RETURN(const nlohmann::json template_recipe,
                   Required<nlohmann::json>(json, "template_recipe", kContext));
  ASSIGN_OR_RETURN(candidate.template_recipe, ParallaxArtworkRecipeFromJson(template_recipe));
  if (ParallaxArtworkRecipeToJson(candidate.template_recipe) != template_recipe) {
    return absl::InvalidArgumentError(
        "generated parallax template must be one exact schema-current recipe object");
  }
  ASSIGN_OR_RETURN(const nlohmann::json ids,
                   Required<nlohmann::json>(json, "output_ids", kContext));
  RETURN_IF_ERROR(RequireExactObject(ids, {"texture_id", "recipe_id"},
                                     "generated parallax creation candidate output IDs"));
  ASSIGN_OR_RETURN(candidate.ids.texture_id, Required<std::string>(ids, "texture_id", kContext));
  ASSIGN_OR_RETURN(candidate.ids.recipe_id, Required<std::string>(ids, "recipe_id", kContext));
  RETURN_IF_ERROR(ValidateCommon(candidate.asset_id, candidate.name, candidate.source,
                                 candidate.ids.recipe_id));
  if (candidate.ids.texture_id.empty()) {
    return absl::InvalidArgumentError("generated parallax texture ID must be non-empty");
  }
  return candidate;
}

nlohmann::json GeneratedParallaxArtworkRedrawCandidateToJson(
    const GeneratedParallaxArtworkRedrawCandidate& candidate) {
  return {
      {"schema_version", kGeneratedAssetCandidateSchemaVersion},
      {"operation", "redraw"},
      {"kind", "parallax-artwork"},
      {"asset_id", candidate.asset_id},
      {"expected_source_rgba_sha256", candidate.expected_source_digest},
      {"expected_final_rgba_sha256", candidate.expected_final_pixel_digest},
      {"source", SourceToJson(candidate.source)},
  };
}

absl::StatusOr<GeneratedParallaxArtworkRedrawCandidate>
GeneratedParallaxArtworkRedrawCandidateFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "generated parallax artwork redraw candidate";
  RETURN_IF_ERROR(
      RequireExactObject(json,
                         {"schema_version", "operation", "kind", "asset_id",
                          "expected_source_rgba_sha256", "expected_final_rgba_sha256", "source"},
                         kContext));
  ASSIGN_OR_RETURN(const int schema, Required<int>(json, "schema_version", kContext));
  if (schema != kGeneratedAssetCandidateSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat(kContext, " schema version ", schema,
                                                      " is not supported version ",
                                                      kGeneratedAssetCandidateSchemaVersion));
  }
  ASSIGN_OR_RETURN(const std::string operation, Required<std::string>(json, "operation", kContext));
  if (operation != "redraw") {
    return absl::InvalidArgumentError(absl::StrCat(kContext, " operation must be 'redraw'"));
  }
  ASSIGN_OR_RETURN(const std::string kind, Required<std::string>(json, "kind", kContext));
  if (kind != "parallax-artwork") {
    return absl::InvalidArgumentError(absl::StrCat(kContext, " kind must be 'parallax-artwork'"));
  }

  GeneratedParallaxArtworkRedrawCandidate candidate;
  ASSIGN_OR_RETURN(candidate.asset_id, Required<std::string>(json, "asset_id", kContext));
  if (candidate.asset_id.empty()) {
    return absl::InvalidArgumentError("generated redraw candidate asset ID is empty");
  }
  ASSIGN_OR_RETURN(candidate.expected_source_digest,
                   Required<std::string>(json, "expected_source_rgba_sha256", kContext));
  ASSIGN_OR_RETURN(candidate.expected_final_pixel_digest,
                   Required<std::string>(json, "expected_final_rgba_sha256", kContext));
  if (!IsLowercaseSha256Digest(candidate.expected_source_digest) ||
      !IsLowercaseSha256Digest(candidate.expected_final_pixel_digest)) {
    return absl::InvalidArgumentError(
        "generated redraw candidate expected digests must be lowercase SHA-256 values");
  }
  ASSIGN_OR_RETURN(const nlohmann::json source, Required<nlohmann::json>(json, "source", kContext));
  ASSIGN_OR_RETURN(candidate.source, SourceFromJson(source));
  return candidate;
}

}  // namespace zebes
