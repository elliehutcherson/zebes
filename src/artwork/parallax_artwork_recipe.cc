#include "artwork/parallax_artwork_recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
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
  for (const auto& [key, unused_value] : json.items()) {
    static_cast<void>(unused_value);
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

nlohmann::json ColorToJson(const RgbaColor& color) {
  return nlohmann::json::array({color.r, color.g, color.b, color.a});
}

absl::StatusOr<RgbaColor> ColorFromJson(const nlohmann::json& json, std::string_view context) {
  if (!json.is_array() || json.size() != 4) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must contain four RGBA channels"));
  }
  std::array<int, 4> channels;
  try {
    for (size_t channel = 0; channel < channels.size(); ++channel) {
      channels[channel] = json.at(channel).get<int>();
      if (channels[channel] < 0 || channels[channel] > 255) {
        return absl::InvalidArgumentError(absl::StrCat(context, " has an invalid channel"));
      }
    }
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is invalid: ", error.what()));
  }
  return RgbaColor{
      .r = static_cast<uint8_t>(channels[0]),
      .g = static_cast<uint8_t>(channels[1]),
      .b = static_cast<uint8_t>(channels[2]),
      .a = static_cast<uint8_t>(channels[3]),
  };
}

const char* FramePolicyToString(ParallaxArtworkFramePolicy policy) {
  switch (policy) {
    case ParallaxArtworkFramePolicy::kCropToFill:
      return "crop_to_fill";
    case ParallaxArtworkFramePolicy::kFitInside:
      return "fit_inside";
  }
  return "invalid";
}

absl::StatusOr<ParallaxArtworkFramePolicy> FramePolicyFromString(const std::string& value) {
  if (value == "crop_to_fill") return ParallaxArtworkFramePolicy::kCropToFill;
  if (value == "fit_inside") return ParallaxArtworkFramePolicy::kFitInside;
  return absl::InvalidArgumentError(absl::StrCat("parallax frame policy is invalid: ", value));
}

const char* AlphaRoleToString(ParallaxArtworkAlphaRole role) {
  switch (role) {
    case ParallaxArtworkAlphaRole::kOpaquePlate:
      return "opaque_plate";
    case ParallaxArtworkAlphaRole::kTransparentOverlay:
      return "transparent_overlay";
  }
  return "invalid";
}

absl::StatusOr<ParallaxArtworkAlphaRole> AlphaRoleFromString(const std::string& value) {
  if (value == "opaque_plate") return ParallaxArtworkAlphaRole::kOpaquePlate;
  if (value == "transparent_overlay") return ParallaxArtworkAlphaRole::kTransparentOverlay;
  return absl::InvalidArgumentError(absl::StrCat("parallax alpha role is invalid: ", value));
}

const char* ExtractionToString(ParallaxArtworkOverlayExtraction extraction) {
  switch (extraction) {
    case ParallaxArtworkOverlayExtraction::kPreserveAlpha:
      return "preserve_alpha";
    case ParallaxArtworkOverlayExtraction::kRemoveSolidMatte:
      return "remove_solid_matte";
  }
  return "invalid";
}

absl::StatusOr<ParallaxArtworkOverlayExtraction> ExtractionFromString(const std::string& value) {
  if (value == "preserve_alpha") return ParallaxArtworkOverlayExtraction::kPreserveAlpha;
  if (value == "remove_solid_matte") {
    return ParallaxArtworkOverlayExtraction::kRemoveSolidMatte;
  }
  return absl::InvalidArgumentError(absl::StrCat("parallax extraction is invalid: ", value));
}

const char* OverlayAlphaToString(ParallaxArtworkOverlayAlphaPolicy policy) {
  switch (policy) {
    case ParallaxArtworkOverlayAlphaPolicy::kPreserve:
      return "preserve";
    case ParallaxArtworkOverlayAlphaPolicy::kBinary:
      return "binary";
  }
  return "invalid";
}

absl::StatusOr<ParallaxArtworkOverlayAlphaPolicy> OverlayAlphaFromString(const std::string& value) {
  if (value == "preserve") return ParallaxArtworkOverlayAlphaPolicy::kPreserve;
  if (value == "binary") return ParallaxArtworkOverlayAlphaPolicy::kBinary;
  return absl::InvalidArgumentError(absl::StrCat("parallax overlay alpha is invalid: ", value));
}

nlohmann::json StyleToJson(const ParallaxArtworkStyle& style) {
  nlohmann::json palette = nlohmann::json::array();
  for (const RgbaColor& color : style.palette) palette.push_back(ColorToJson(color));
  return {
      {"pixel_block_size", style.pixel_block_size},
      {"quantize_to_palette", style.quantize_to_palette},
      {"palette", std::move(palette)},
  };
}

absl::StatusOr<ParallaxArtworkStyle> StyleFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(json, {"pixel_block_size", "quantize_to_palette", "palette"},
                                     "parallax artwork recipe style"));
  ParallaxArtworkStyle style;
  ASSIGN_OR_RETURN(style.pixel_block_size,
                   Required<int>(json, "pixel_block_size", "parallax artwork recipe style"));
  ASSIGN_OR_RETURN(style.quantize_to_palette,
                   Required<bool>(json, "quantize_to_palette", "parallax artwork recipe style"));
  ASSIGN_OR_RETURN(const nlohmann::json palette,
                   Required<nlohmann::json>(json, "palette", "parallax artwork recipe style"));
  if (!palette.is_array()) {
    return absl::InvalidArgumentError("parallax artwork recipe palette must be an array");
  }
  style.palette.reserve(palette.size());
  for (size_t index = 0; index < palette.size(); ++index) {
    ASSIGN_OR_RETURN(
        RgbaColor color,
        ColorFromJson(palette.at(index), absl::StrCat("parallax artwork palette color ", index)));
    style.palette.push_back(color);
  }
  return style;
}

nlohmann::json LimitsToJson(const SourceArtworkLimits& limits) {
  return {
      {"maximum_width", limits.maximum_width},
      {"maximum_height", limits.maximum_height},
      {"maximum_pixels", limits.maximum_pixels},
      {"maximum_bytes", limits.maximum_bytes},
  };
}

absl::StatusOr<SourceArtworkLimits> LimitsFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(
      json, {"maximum_width", "maximum_height", "maximum_pixels", "maximum_bytes"},
      "parallax artwork recipe source limits"));
  SourceArtworkLimits limits;
  ASSIGN_OR_RETURN(limits.maximum_width,
                   Required<int>(json, "maximum_width", "parallax artwork recipe source limits"));
  ASSIGN_OR_RETURN(limits.maximum_height,
                   Required<int>(json, "maximum_height", "parallax artwork recipe source limits"));
  ASSIGN_OR_RETURN(
      limits.maximum_pixels,
      Required<size_t>(json, "maximum_pixels", "parallax artwork recipe source limits"));
  ASSIGN_OR_RETURN(limits.maximum_bytes, Required<size_t>(json, "maximum_bytes",
                                                          "parallax artwork recipe source limits"));
  return limits;
}

nlohmann::json PipelineToJson(const ParallaxArtworkPipelineConfig& pipeline) {
  return {
      {"source_limits", LimitsToJson(pipeline.source_limits)},
      {"target_width", pipeline.target_width},
      {"target_height", pipeline.target_height},
      {"frame_policy", FramePolicyToString(pipeline.frame_policy)},
      {"alpha_role", AlphaRoleToString(pipeline.alpha_role)},
      {"overlay_extraction", ExtractionToString(pipeline.overlay_extraction)},
      {"overlay_alpha_policy", OverlayAlphaToString(pipeline.overlay_alpha_policy)},
      {"matte_color", ColorToJson(pipeline.matte_color)},
      {"matte_transparent_distance", pipeline.matte_transparent_distance},
      {"matte_opaque_distance", pipeline.matte_opaque_distance},
      {"binary_alpha_threshold", pipeline.binary_alpha_threshold},
      {"review_repeat_x", pipeline.review_repeat_x},
      {"review_repeat_y", pipeline.review_repeat_y},
  };
}

absl::StatusOr<ParallaxArtworkPipelineConfig> PipelineFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(
      json,
      {"source_limits", "target_width", "target_height", "frame_policy", "alpha_role",
       "overlay_extraction", "overlay_alpha_policy", "matte_color", "matte_transparent_distance",
       "matte_opaque_distance", "binary_alpha_threshold", "review_repeat_x", "review_repeat_y"},
      "parallax artwork recipe pipeline"));
  ParallaxArtworkPipelineConfig pipeline;
  ASSIGN_OR_RETURN(
      const nlohmann::json source_limits,
      Required<nlohmann::json>(json, "source_limits", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.source_limits, LimitsFromJson(source_limits));
  ASSIGN_OR_RETURN(pipeline.target_width,
                   Required<int>(json, "target_width", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.target_height,
                   Required<int>(json, "target_height", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(const std::string frame_policy,
                   Required<std::string>(json, "frame_policy", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.frame_policy, FramePolicyFromString(frame_policy));
  ASSIGN_OR_RETURN(const std::string alpha_role,
                   Required<std::string>(json, "alpha_role", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.alpha_role, AlphaRoleFromString(alpha_role));
  ASSIGN_OR_RETURN(
      const std::string extraction,
      Required<std::string>(json, "overlay_extraction", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.overlay_extraction, ExtractionFromString(extraction));
  ASSIGN_OR_RETURN(
      const std::string overlay_alpha,
      Required<std::string>(json, "overlay_alpha_policy", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.overlay_alpha_policy, OverlayAlphaFromString(overlay_alpha));
  ASSIGN_OR_RETURN(
      const nlohmann::json matte_color,
      Required<nlohmann::json>(json, "matte_color", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.matte_color,
                   ColorFromJson(matte_color, "parallax artwork recipe matte color"));
  ASSIGN_OR_RETURN(
      pipeline.matte_transparent_distance,
      Required<float>(json, "matte_transparent_distance", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(
      pipeline.matte_opaque_distance,
      Required<float>(json, "matte_opaque_distance", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(
      pipeline.binary_alpha_threshold,
      Required<int>(json, "binary_alpha_threshold", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.review_repeat_x,
                   Required<bool>(json, "review_repeat_x", "parallax artwork recipe pipeline"));
  ASSIGN_OR_RETURN(pipeline.review_repeat_y,
                   Required<bool>(json, "review_repeat_y", "parallax artwork recipe pipeline"));
  return pipeline;
}

}  // namespace

absl::Status ValidateParallaxArtworkRecipe(const ParallaxArtworkRecipe& recipe) {
  if (recipe.id.empty()) return absl::InvalidArgumentError("parallax artwork recipe ID is empty");
  if (recipe.name.empty()) {
    return absl::InvalidArgumentError("parallax artwork recipe name is empty");
  }
  if (recipe.source_artwork_id.empty()) {
    return absl::InvalidArgumentError("parallax artwork recipe source artwork ID is empty");
  }
  if (recipe.texture_id.empty()) {
    return absl::InvalidArgumentError("parallax artwork recipe texture ID is empty");
  }
  if (recipe.terrain_recipe_id.has_value() && recipe.terrain_recipe_id->empty()) {
    return absl::InvalidArgumentError("attached terrain recipe ID cannot be empty");
  }
  if (recipe.terrain_recipe_id.has_value() && recipe.style.palette.empty()) {
    return absl::InvalidArgumentError(
        "attached terrain recipe requires a resolved artwork palette snapshot");
  }
  RETURN_IF_ERROR(ValidateParallaxArtworkPipelineConfig(recipe.pipeline, recipe.style));
  if (recipe.pipeline_version != kParallaxArtworkPipelineVersion) {
    return absl::FailedPreconditionError(
        absl::StrCat("parallax artwork recipe pipeline version ", recipe.pipeline_version,
                     " is not supported version ", kParallaxArtworkPipelineVersion));
  }
  if (recipe.expected_width != recipe.pipeline.target_width ||
      recipe.expected_height != recipe.pipeline.target_height) {
    return absl::InvalidArgumentError(
        "parallax artwork recipe output dimensions do not match its pipeline target");
  }
  if (!IsLowercaseSha256Digest(recipe.final_pixel_digest)) {
    return absl::InvalidArgumentError(
        "parallax artwork recipe final pixel digest is not lowercase SHA-256");
  }
  return absl::OkStatus();
}

nlohmann::json ParallaxArtworkRecipeToJson(const ParallaxArtworkRecipe& recipe) {
  return {
      {"schema_version", kParallaxArtworkRecipeSchemaVersion},
      {"id", recipe.id},
      {"name", recipe.name},
      {"source_artwork_id", recipe.source_artwork_id},
      {"terrain_recipe_id", recipe.terrain_recipe_id.has_value()
                                ? nlohmann::json(*recipe.terrain_recipe_id)
                                : nlohmann::json(nullptr)},
      {"style", StyleToJson(recipe.style)},
      {"pipeline", PipelineToJson(recipe.pipeline)},
      {"texture_id", recipe.texture_id},
      {"expected_width", recipe.expected_width},
      {"expected_height", recipe.expected_height},
      {"final_pixel_digest", recipe.final_pixel_digest},
      {"pipeline_version", recipe.pipeline_version},
  };
}

absl::StatusOr<ParallaxArtworkRecipe> ParallaxArtworkRecipeFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(
      RequireExactObject(json,
                         {"schema_version", "id", "name", "source_artwork_id", "terrain_recipe_id",
                          "style", "pipeline", "texture_id", "expected_width", "expected_height",
                          "final_pixel_digest", "pipeline_version"},
                         "parallax artwork recipe"));
  ASSIGN_OR_RETURN(const int schema_version,
                   Required<int>(json, "schema_version", "parallax artwork recipe"));
  if (schema_version != kParallaxArtworkRecipeSchemaVersion) {
    return absl::FailedPreconditionError(
        absl::StrCat("parallax artwork recipe schema version ", schema_version, " is not version ",
                     kParallaxArtworkRecipeSchemaVersion,
                     "; run scripts/migrate_definitions.py to bring it forward"));
  }

  ParallaxArtworkRecipe recipe;
  ASSIGN_OR_RETURN(recipe.id, Required<std::string>(json, "id", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.name, Required<std::string>(json, "name", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.source_artwork_id,
                   Required<std::string>(json, "source_artwork_id", "parallax artwork recipe"));
  if (!json.at("terrain_recipe_id").is_null()) {
    ASSIGN_OR_RETURN(std::string terrain_recipe_id,
                     Required<std::string>(json, "terrain_recipe_id", "parallax artwork recipe"));
    recipe.terrain_recipe_id = std::move(terrain_recipe_id);
  }
  ASSIGN_OR_RETURN(const nlohmann::json style,
                   Required<nlohmann::json>(json, "style", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.style, StyleFromJson(style));
  ASSIGN_OR_RETURN(const nlohmann::json pipeline,
                   Required<nlohmann::json>(json, "pipeline", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.pipeline, PipelineFromJson(pipeline));
  ASSIGN_OR_RETURN(recipe.texture_id,
                   Required<std::string>(json, "texture_id", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.expected_width,
                   Required<int>(json, "expected_width", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.expected_height,
                   Required<int>(json, "expected_height", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.final_pixel_digest,
                   Required<std::string>(json, "final_pixel_digest", "parallax artwork recipe"));
  ASSIGN_OR_RETURN(recipe.pipeline_version,
                   Required<int>(json, "pipeline_version", "parallax artwork recipe"));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  return recipe;
}

}  // namespace zebes
