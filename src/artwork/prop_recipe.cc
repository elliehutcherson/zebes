#include "artwork/prop_recipe.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat("prop recipe is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("prop recipe field '", key, "' is invalid: ", error.what()));
  }
}

bool IsSha256(std::string_view digest) {
  if (digest.size() != 64) return false;
  for (const char character : digest) {
    if (!std::isdigit(static_cast<unsigned char>(character)) &&
        (character < 'a' || character > 'f')) {
      return false;
    }
  }
  return true;
}

nlohmann::json ColorToJson(const RgbaColor& color) {
  return nlohmann::json::array({color.r, color.g, color.b, color.a});
}

absl::StatusOr<RgbaColor> ColorFromJson(const nlohmann::json& json, size_t index) {
  if (!json.is_array() || json.size() != 4) {
    return absl::InvalidArgumentError(
        absl::StrCat("prop recipe palette color ", index, " must contain RGBA"));
  }
  std::array<int, 4> channels;
  try {
    for (size_t channel = 0; channel < channels.size(); ++channel) {
      channels[channel] = json.at(channel).get<int>();
      if (channels[channel] < 0 || channels[channel] > 255) {
        return absl::InvalidArgumentError(
            absl::StrCat("prop recipe palette color ", index, " has an invalid channel"));
      }
    }
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("prop recipe palette color ", index, " is invalid: ", error.what()));
  }
  return RgbaColor{
      .r = static_cast<uint8_t>(channels[0]),
      .g = static_cast<uint8_t>(channels[1]),
      .b = static_cast<uint8_t>(channels[2]),
      .a = static_cast<uint8_t>(channels[3]),
  };
}

nlohmann::json StyleToJson(const PropArtworkStyle& style) {
  nlohmann::json palette = nlohmann::json::array();
  for (const RgbaColor& color : style.palette.colors) palette.push_back(ColorToJson(color));
  return {
      {"tile_size", style.tile_size},
      {"pixel_block_size", style.pixel_block_size},
      {"palette", std::move(palette)},
  };
}

absl::StatusOr<PropArtworkStyle> StyleFromJson(const nlohmann::json& json) {
  PropArtworkStyle style;
  ASSIGN_OR_RETURN(style.tile_size, Required<int>(json, "tile_size"));
  ASSIGN_OR_RETURN(style.pixel_block_size, Required<int>(json, "pixel_block_size"));
  ASSIGN_OR_RETURN(const nlohmann::json palette, Required<nlohmann::json>(json, "palette"));
  if (!palette.is_array() || palette.size() != kTerrainPaletteColorCount) {
    return absl::InvalidArgumentError(
        absl::StrCat("prop recipe palette must contain ", kTerrainPaletteColorCount, " roles"));
  }
  for (size_t index = 0; index < style.palette.colors.size(); ++index) {
    ASSIGN_OR_RETURN(style.palette.colors[index], ColorFromJson(palette.at(index), index));
  }
  RETURN_IF_ERROR(ValidatePropArtworkStyle(style));
  return style;
}

const char* AttachmentModeToString(PropAttachmentMode mode) {
  switch (mode) {
    case PropAttachmentMode::kGrounded:
      return "grounded";
    case PropAttachmentMode::kCeiling:
      return "ceiling";
    case PropAttachmentMode::kFree:
      return "free";
  }
  return "invalid";
}

absl::StatusOr<PropAttachmentMode> AttachmentModeFromString(const std::string& value) {
  if (value == "grounded") return PropAttachmentMode::kGrounded;
  if (value == "ceiling") return PropAttachmentMode::kCeiling;
  if (value == "free") return PropAttachmentMode::kFree;
  return absl::InvalidArgumentError(absl::StrCat("prop attachment mode is invalid: ", value));
}

nlohmann::json AttachmentToJson(const PropAttachmentConfig& attachment) {
  nlohmann::json free_anchor = nullptr;
  if (attachment.free_anchor.has_value()) {
    free_anchor = {{"x", attachment.free_anchor->x}, {"y", attachment.free_anchor->y}};
  }
  return {
      {"mode", AttachmentModeToString(attachment.mode)},
      {"free_anchor", std::move(free_anchor)},
  };
}

absl::StatusOr<PropAttachmentConfig> AttachmentFromJson(const nlohmann::json& json) {
  PropAttachmentConfig attachment;
  ASSIGN_OR_RETURN(const std::string mode, Required<std::string>(json, "mode"));
  ASSIGN_OR_RETURN(attachment.mode, AttachmentModeFromString(mode));
  if (!json.contains("free_anchor")) {
    return absl::InvalidArgumentError("prop recipe is missing 'free_anchor'");
  }
  if (!json.at("free_anchor").is_null()) {
    ASSIGN_OR_RETURN(const nlohmann::json free_anchor,
                     Required<nlohmann::json>(json, "free_anchor"));
    PropFreeAnchor parsed;
    ASSIGN_OR_RETURN(parsed.x, Required<int>(free_anchor, "x"));
    ASSIGN_OR_RETURN(parsed.y, Required<int>(free_anchor, "y"));
    attachment.free_anchor = parsed;
  }
  return attachment;
}

nlohmann::json PipelineToJson(const PropArtworkPipelineConfig& pipeline) {
  return {
      {"source_limits",
       {{"maximum_width", pipeline.source_limits.maximum_width},
        {"maximum_height", pipeline.source_limits.maximum_height},
        {"maximum_pixels", pipeline.source_limits.maximum_pixels},
        {"maximum_bytes", pipeline.source_limits.maximum_bytes}}},
      {"isolation",
       {{"alpha_threshold", pipeline.isolation.alpha_threshold},
        {"background_distance", pipeline.isolation.background_distance},
        {"enclosed_background_distance", pipeline.isolation.enclosed_background_distance},
        {"minimum_subject_area", pipeline.isolation.minimum_subject_area},
        {"competing_subject_ratio", pipeline.isolation.competing_subject_ratio}}},
      {"composition",
       {{"canvas_tiles_wide", pipeline.composition.canvas_tiles_wide},
        {"canvas_tiles_high", pipeline.composition.canvas_tiles_high},
        {"padding_fraction", pipeline.composition.padding_fraction},
        {"attachment", AttachmentToJson(pipeline.composition.attachment)}}},
      {"edge",
       {{"width", pipeline.edge.width}, {"alpha_threshold", pipeline.edge.alpha_threshold}}},
      {"cleanup",
       {{"alpha_threshold", pipeline.cleanup.alpha_threshold},
        {"minimum_component_area", pipeline.cleanup.minimum_component_area},
        {"contact_tolerance", pipeline.cleanup.contact_tolerance}}},
  };
}

absl::StatusOr<PropArtworkPipelineConfig> PipelineFromJson(const nlohmann::json& json) {
  PropArtworkPipelineConfig pipeline;
  ASSIGN_OR_RETURN(const nlohmann::json limits, Required<nlohmann::json>(json, "source_limits"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_width, Required<int>(limits, "maximum_width"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_height, Required<int>(limits, "maximum_height"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_pixels,
                   Required<size_t>(limits, "maximum_pixels"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_bytes, Required<size_t>(limits, "maximum_bytes"));

  ASSIGN_OR_RETURN(const nlohmann::json isolation, Required<nlohmann::json>(json, "isolation"));
  ASSIGN_OR_RETURN(pipeline.isolation.alpha_threshold, Required<int>(isolation, "alpha_threshold"));
  ASSIGN_OR_RETURN(pipeline.isolation.background_distance,
                   Required<float>(isolation, "background_distance"));
  ASSIGN_OR_RETURN(pipeline.isolation.enclosed_background_distance,
                   Required<float>(isolation, "enclosed_background_distance"));
  ASSIGN_OR_RETURN(pipeline.isolation.minimum_subject_area,
                   Required<int>(isolation, "minimum_subject_area"));
  ASSIGN_OR_RETURN(pipeline.isolation.competing_subject_ratio,
                   Required<float>(isolation, "competing_subject_ratio"));

  ASSIGN_OR_RETURN(const nlohmann::json composition, Required<nlohmann::json>(json, "composition"));
  ASSIGN_OR_RETURN(pipeline.composition.canvas_tiles_wide,
                   Required<int>(composition, "canvas_tiles_wide"));
  ASSIGN_OR_RETURN(pipeline.composition.canvas_tiles_high,
                   Required<int>(composition, "canvas_tiles_high"));
  ASSIGN_OR_RETURN(pipeline.composition.padding_fraction,
                   Required<float>(composition, "padding_fraction"));
  ASSIGN_OR_RETURN(const nlohmann::json attachment,
                   Required<nlohmann::json>(composition, "attachment"));
  ASSIGN_OR_RETURN(pipeline.composition.attachment, AttachmentFromJson(attachment));

  ASSIGN_OR_RETURN(const nlohmann::json edge, Required<nlohmann::json>(json, "edge"));
  ASSIGN_OR_RETURN(pipeline.edge.width, Required<int>(edge, "width"));
  ASSIGN_OR_RETURN(pipeline.edge.alpha_threshold, Required<int>(edge, "alpha_threshold"));

  ASSIGN_OR_RETURN(const nlohmann::json cleanup, Required<nlohmann::json>(json, "cleanup"));
  ASSIGN_OR_RETURN(pipeline.cleanup.alpha_threshold, Required<int>(cleanup, "alpha_threshold"));
  ASSIGN_OR_RETURN(pipeline.cleanup.minimum_component_area,
                   Required<int>(cleanup, "minimum_component_area"));
  ASSIGN_OR_RETURN(pipeline.cleanup.contact_tolerance, Required<int>(cleanup, "contact_tolerance"));
  return pipeline;
}

nlohmann::json FrameToJson(const SpriteFrame& frame) {
  return {
      {"index", frame.index},         {"texture_x", frame.texture_x},
      {"texture_y", frame.texture_y}, {"texture_w", frame.texture_w},
      {"texture_h", frame.texture_h}, {"render_w", frame.render_w},
      {"render_h", frame.render_h},   {"frames_per_cycle", frame.frames_per_cycle},
      {"offset_x", frame.offset_x},   {"offset_y", frame.offset_y},
  };
}

absl::StatusOr<SpriteFrame> FrameFromJson(const nlohmann::json& json) {
  SpriteFrame frame;
  ASSIGN_OR_RETURN(frame.index, Required<int>(json, "index"));
  ASSIGN_OR_RETURN(frame.texture_x, Required<int>(json, "texture_x"));
  ASSIGN_OR_RETURN(frame.texture_y, Required<int>(json, "texture_y"));
  ASSIGN_OR_RETURN(frame.texture_w, Required<int>(json, "texture_w"));
  ASSIGN_OR_RETURN(frame.texture_h, Required<int>(json, "texture_h"));
  ASSIGN_OR_RETURN(frame.render_w, Required<int>(json, "render_w"));
  ASSIGN_OR_RETURN(frame.render_h, Required<int>(json, "render_h"));
  ASSIGN_OR_RETURN(frame.frames_per_cycle, Required<int>(json, "frames_per_cycle"));
  ASSIGN_OR_RETURN(frame.offset_x, Required<int>(json, "offset_x"));
  ASSIGN_OR_RETURN(frame.offset_y, Required<int>(json, "offset_y"));
  return frame;
}

absl::Status ValidatePipelineSettings(const PropArtworkPipelineConfig& pipeline,
                                      const PropArtworkStyle& style) {
  const PropSourceLimits& limits = pipeline.source_limits;
  if (limits.maximum_width <= 0 || limits.maximum_height <= 0 || limits.maximum_pixels == 0 ||
      limits.maximum_bytes == 0) {
    return absl::InvalidArgumentError("prop recipe source limits must be positive");
  }
  const SubjectIsolationConfig& isolation = pipeline.isolation;
  if (isolation.alpha_threshold < 0 || isolation.alpha_threshold > 255 ||
      !std::isfinite(isolation.background_distance) ||
      !std::isfinite(isolation.enclosed_background_distance) ||
      !std::isfinite(isolation.competing_subject_ratio) || isolation.background_distance < 0.0f ||
      isolation.enclosed_background_distance < 0.0f || isolation.minimum_subject_area <= 0 ||
      isolation.competing_subject_ratio < 0.0f || isolation.competing_subject_ratio > 1.0f) {
    return absl::InvalidArgumentError("prop recipe isolation settings are invalid");
  }
  const PropCompositionConfig& composition = pipeline.composition;
  if (composition.canvas_tiles_wide <= 0 || composition.canvas_tiles_high <= 0 ||
      !std::isfinite(composition.padding_fraction) || composition.padding_fraction < 0.0f ||
      composition.padding_fraction >= 0.5f) {
    return absl::InvalidArgumentError("prop recipe composition settings are invalid");
  }
  if (pipeline.edge.width < 0 || pipeline.edge.alpha_threshold < 0 ||
      pipeline.edge.alpha_threshold > 255) {
    return absl::InvalidArgumentError("prop recipe edge settings are invalid");
  }
  if (pipeline.cleanup.alpha_threshold < 0 || pipeline.cleanup.alpha_threshold > 255 ||
      pipeline.cleanup.minimum_component_area <= 0 || pipeline.cleanup.contact_tolerance < 0) {
    return absl::InvalidArgumentError("prop recipe cleanup settings are invalid");
  }
  const int64_t output_width =
      static_cast<int64_t>(style.tile_size) * composition.canvas_tiles_wide;
  const int64_t output_height =
      static_cast<int64_t>(style.tile_size) * composition.canvas_tiles_high;
  if (output_width > std::numeric_limits<int>::max() ||
      output_height > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError("prop recipe canvas dimensions overflow integer storage");
  }
  RETURN_IF_ERROR(ValidatePropAttachment(composition.attachment, static_cast<int>(output_width),
                                         static_cast<int>(output_height)));
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidatePropRecipe(const PropRecipe& recipe) {
  if (recipe.id.empty() || recipe.name.empty() || recipe.source_artwork_id.empty() ||
      recipe.texture_id.empty() || recipe.sprite_id.empty() || recipe.blueprint_id.empty()) {
    return absl::InvalidArgumentError("prop recipe needs a name and every stable asset ID");
  }
  if (recipe.terrain_recipe_id.has_value() && recipe.terrain_recipe_id->empty()) {
    return absl::InvalidArgumentError("attached terrain recipe ID cannot be empty");
  }
  RETURN_IF_ERROR(ValidatePropArtworkStyle(recipe.style));
  RETURN_IF_ERROR(ValidatePipelineSettings(recipe.pipeline, recipe.style));
  if (recipe.pipeline_version != kPropArtworkPipelineVersion) {
    return absl::FailedPreconditionError(
        absl::StrCat("prop recipe pipeline version ", recipe.pipeline_version,
                     " is not supported version ", kPropArtworkPipelineVersion));
  }
  const int64_t expected_width =
      static_cast<int64_t>(recipe.style.tile_size) * recipe.pipeline.composition.canvas_tiles_wide;
  const int64_t expected_height =
      static_cast<int64_t>(recipe.style.tile_size) * recipe.pipeline.composition.canvas_tiles_high;
  if (expected_width > std::numeric_limits<int>::max() ||
      expected_height > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError("prop recipe canvas dimensions overflow integer storage");
  }
  const SpriteFrame& frame = recipe.expected_frame;
  if (frame.index != 0 || frame.texture_x != 0 || frame.texture_y != 0 ||
      frame.texture_w != expected_width || frame.texture_h != expected_height ||
      frame.render_w != expected_width || frame.render_h != expected_height ||
      frame.frames_per_cycle != 0) {
    return absl::InvalidArgumentError(
        "prop recipe expected frame must be one full-size, 1:1 static frame");
  }
  const PropAttachmentConfig& attachment = recipe.pipeline.composition.attachment;
  if (attachment.mode == PropAttachmentMode::kFree &&
      (frame.offset_x != -attachment.free_anchor->x ||
       frame.offset_y != -attachment.free_anchor->y)) {
    return absl::InvalidArgumentError(
        "prop recipe frame offset does not match its explicit free anchor");
  }
  if (!IsSha256(recipe.final_pixel_digest)) {
    return absl::InvalidArgumentError("prop recipe final pixel digest is not lowercase SHA-256");
  }
  return absl::OkStatus();
}

nlohmann::json PropRecipeToJson(const PropRecipe& recipe) {
  return {
      {"schema_version", kPropRecipeSchemaVersion},
      {"id", recipe.id},
      {"name", recipe.name},
      {"source_artwork_id", recipe.source_artwork_id},
      {"terrain_recipe_id", recipe.terrain_recipe_id.has_value()
                                ? nlohmann::json(*recipe.terrain_recipe_id)
                                : nlohmann::json(nullptr)},
      {"style", StyleToJson(recipe.style)},
      {"pipeline", PipelineToJson(recipe.pipeline)},
      {"texture_id", recipe.texture_id},
      {"sprite_id", recipe.sprite_id},
      {"blueprint_id", recipe.blueprint_id},
      {"expected_frame", FrameToJson(recipe.expected_frame)},
      {"final_pixel_digest", recipe.final_pixel_digest},
      {"pipeline_version", recipe.pipeline_version},
  };
}

absl::StatusOr<PropRecipe> PropRecipeFromJson(const nlohmann::json& json) {
  ASSIGN_OR_RETURN(const int schema_version, Required<int>(json, "schema_version"));
  if (schema_version != kPropRecipeSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat(
        "prop recipe schema version ", schema_version, " is not version ", kPropRecipeSchemaVersion,
        "; run scripts/migrate_definitions.py to bring it forward"));
  }
  PropRecipe recipe;
  ASSIGN_OR_RETURN(recipe.id, Required<std::string>(json, "id"));
  ASSIGN_OR_RETURN(recipe.name, Required<std::string>(json, "name"));
  ASSIGN_OR_RETURN(recipe.source_artwork_id, Required<std::string>(json, "source_artwork_id"));
  if (!json.contains("terrain_recipe_id")) {
    return absl::InvalidArgumentError("prop recipe is missing 'terrain_recipe_id'");
  }
  if (!json.at("terrain_recipe_id").is_null()) {
    ASSIGN_OR_RETURN(std::string terrain_recipe_id,
                     Required<std::string>(json, "terrain_recipe_id"));
    recipe.terrain_recipe_id = std::move(terrain_recipe_id);
  }
  ASSIGN_OR_RETURN(const nlohmann::json style, Required<nlohmann::json>(json, "style"));
  ASSIGN_OR_RETURN(recipe.style, StyleFromJson(style));
  ASSIGN_OR_RETURN(const nlohmann::json pipeline, Required<nlohmann::json>(json, "pipeline"));
  ASSIGN_OR_RETURN(recipe.pipeline, PipelineFromJson(pipeline));
  ASSIGN_OR_RETURN(recipe.texture_id, Required<std::string>(json, "texture_id"));
  ASSIGN_OR_RETURN(recipe.sprite_id, Required<std::string>(json, "sprite_id"));
  ASSIGN_OR_RETURN(recipe.blueprint_id, Required<std::string>(json, "blueprint_id"));
  ASSIGN_OR_RETURN(const nlohmann::json expected_frame,
                   Required<nlohmann::json>(json, "expected_frame"));
  ASSIGN_OR_RETURN(recipe.expected_frame, FrameFromJson(expected_frame));
  ASSIGN_OR_RETURN(recipe.final_pixel_digest, Required<std::string>(json, "final_pixel_digest"));
  ASSIGN_OR_RETURN(recipe.pipeline_version, Required<int>(json, "pipeline_version"));
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  return recipe;
}

}  // namespace zebes
