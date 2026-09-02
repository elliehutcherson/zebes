#include "artwork/animation_frame_set_recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"

namespace zebes {
namespace {

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(
        absl::StrCat("animation frame-set recipe is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("animation frame-set recipe field '", key, "' is invalid: ", error.what()));
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

absl::StatusOr<RgbaColor> ColorFromJson(const nlohmann::json& json, size_t index) {
  if (!json.is_array() || json.size() != 4) {
    return absl::InvalidArgumentError(
        absl::StrCat("animation frame-set recipe palette color ", index, " must contain RGBA"));
  }
  std::array<int, 4> channels;
  try {
    for (size_t channel = 0; channel < channels.size(); ++channel) {
      channels[channel] = json.at(channel).get<int>();
      if (channels[channel] < 0 || channels[channel] > 255) {
        return absl::InvalidArgumentError(absl::StrCat("animation frame-set recipe palette color ",
                                                       index, " has an invalid channel"));
      }
    }
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("animation frame-set recipe palette color ",
                                                   index, " is invalid: ", error.what()));
  }
  return RgbaColor{
      .r = static_cast<uint8_t>(channels[0]),
      .g = static_cast<uint8_t>(channels[1]),
      .b = static_cast<uint8_t>(channels[2]),
      .a = static_cast<uint8_t>(channels[3]),
  };
}

const char* ExtractionToString(AnimationFrameSetExtraction extraction) {
  switch (extraction) {
    case AnimationFrameSetExtraction::kPreserveAlpha:
      return "preserve-alpha";
    case AnimationFrameSetExtraction::kRemoveSolidMatte:
      return "remove-solid-matte";
  }
  return "invalid";
}

absl::StatusOr<AnimationFrameSetExtraction> ExtractionFromString(const std::string& value) {
  if (value == "preserve-alpha") return AnimationFrameSetExtraction::kPreserveAlpha;
  if (value == "remove-solid-matte") return AnimationFrameSetExtraction::kRemoveSolidMatte;
  return absl::InvalidArgumentError(
      absl::StrCat("animation frame-set extraction is invalid: ", value));
}

const char* PlaybackModeToString(SpritePlaybackMode mode) {
  switch (mode) {
    case SpritePlaybackMode::kLoop:
      return "loop";
    case SpritePlaybackMode::kHoldLast:
      return "hold-last";
  }
  return "invalid";
}

absl::StatusOr<SpritePlaybackMode> PlaybackModeFromString(const std::string& value) {
  if (value == "loop") return SpritePlaybackMode::kLoop;
  if (value == "hold-last") return SpritePlaybackMode::kHoldLast;
  return absl::InvalidArgumentError(
      absl::StrCat("animation frame-set playback mode is invalid: ", value));
}

nlohmann::json StyleToJson(const AnimationFrameSetStyle& style) {
  nlohmann::json palette = nlohmann::json::array();
  for (const RgbaColor& color : style.palette) palette.push_back(ColorToJson(color));
  return {
      {"extraction", ExtractionToString(style.extraction)},
      {"matte", ColorToJson(style.matte)},
      {"transparent_matte_distance", style.transparent_matte_distance},
      {"opaque_matte_distance", style.opaque_matte_distance},
      {"alpha_threshold", style.alpha_threshold},
      {"palette", std::move(palette)},
  };
}

absl::StatusOr<AnimationFrameSetStyle> StyleFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(json,
                                     {"extraction", "matte", "transparent_matte_distance",
                                      "opaque_matte_distance", "alpha_threshold", "palette"},
                                     "animation frame-set recipe style"));
  AnimationFrameSetStyle style;
  ASSIGN_OR_RETURN(const std::string extraction, Required<std::string>(json, "extraction"));
  ASSIGN_OR_RETURN(style.extraction, ExtractionFromString(extraction));
  ASSIGN_OR_RETURN(const nlohmann::json matte, Required<nlohmann::json>(json, "matte"));
  ASSIGN_OR_RETURN(style.matte, ColorFromJson(matte, 0));
  ASSIGN_OR_RETURN(style.transparent_matte_distance,
                   Required<float>(json, "transparent_matte_distance"));
  ASSIGN_OR_RETURN(style.opaque_matte_distance, Required<float>(json, "opaque_matte_distance"));
  ASSIGN_OR_RETURN(style.alpha_threshold, Required<int>(json, "alpha_threshold"));
  ASSIGN_OR_RETURN(const nlohmann::json palette, Required<nlohmann::json>(json, "palette"));
  if (!palette.is_array()) {
    return absl::InvalidArgumentError("animation frame-set recipe palette must be an array");
  }
  style.palette.reserve(palette.size());
  for (size_t index = 0; index < palette.size(); ++index) {
    ASSIGN_OR_RETURN(RgbaColor color, ColorFromJson(palette.at(index), index));
    style.palette.push_back(color);
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetStyle(style));
  return style;
}

nlohmann::json PipelineToJson(const AnimationFrameSetPipelineConfig& pipeline) {
  return {
      {"source_limits",
       {{"maximum_width", pipeline.source_limits.maximum_width},
        {"maximum_height", pipeline.source_limits.maximum_height},
        {"maximum_pixels", pipeline.source_limits.maximum_pixels},
        {"maximum_bytes", pipeline.source_limits.maximum_bytes},
        {"maximum_encoded_bytes", pipeline.source_limits.maximum_encoded_bytes}}},
      {"sheet",
       {{"grid_x", pipeline.sheet.grid_x},
        {"grid_y", pipeline.sheet.grid_y},
        {"cell_width", pipeline.sheet.cell_width},
        {"cell_height", pipeline.sheet.cell_height},
        {"column_gap", pipeline.sheet.column_gap},
        {"row_gap", pipeline.sheet.row_gap},
        {"columns", pipeline.sheet.columns},
        {"rows", pipeline.sheet.rows}}},
      {"output_width", pipeline.output_width},
      {"output_height", pipeline.output_height},
      {"origin_x", pipeline.origin_x},
      {"origin_y", pipeline.origin_y},
      {"contact_line_y", pipeline.contact_line_y},
      {"render_scale", pipeline.render_scale},
      {"contact_tolerance", pipeline.contact_tolerance},
      {"minimum_visible_pixels", pipeline.minimum_visible_pixels},
      {"maximum_horizontal_anchor_drift", pipeline.maximum_horizontal_anchor_drift},
      {"maximum_vertical_anchor_drift", pipeline.maximum_vertical_anchor_drift},
      {"packing_columns", pipeline.packing_columns},
      {"playback_mode", PlaybackModeToString(pipeline.playback_mode)},
      {"frames_per_cycle", pipeline.frames_per_cycle},
      {"planted_frames", pipeline.planted_frames},
  };
}

absl::StatusOr<AnimationFrameSetPipelineConfig> PipelineFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(
      json,
      {"source_limits", "sheet", "output_width", "output_height", "origin_x", "origin_y",
       "contact_line_y", "render_scale", "contact_tolerance", "minimum_visible_pixels",
       "maximum_horizontal_anchor_drift", "maximum_vertical_anchor_drift", "packing_columns",
       "playback_mode", "frames_per_cycle", "planted_frames"},
      "animation frame-set recipe pipeline"));
  AnimationFrameSetPipelineConfig pipeline;
  ASSIGN_OR_RETURN(const nlohmann::json limits, Required<nlohmann::json>(json, "source_limits"));
  RETURN_IF_ERROR(RequireExactObject(limits,
                                     {"maximum_width", "maximum_height", "maximum_pixels",
                                      "maximum_bytes", "maximum_encoded_bytes"},
                                     "animation frame-set recipe source limits"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_width, Required<int>(limits, "maximum_width"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_height, Required<int>(limits, "maximum_height"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_pixels,
                   Required<size_t>(limits, "maximum_pixels"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_bytes, Required<size_t>(limits, "maximum_bytes"));
  ASSIGN_OR_RETURN(pipeline.source_limits.maximum_encoded_bytes,
                   Required<size_t>(limits, "maximum_encoded_bytes"));

  ASSIGN_OR_RETURN(const nlohmann::json sheet, Required<nlohmann::json>(json, "sheet"));
  RETURN_IF_ERROR(RequireExactObject(
      sheet,
      {"grid_x", "grid_y", "cell_width", "cell_height", "column_gap", "row_gap", "columns", "rows"},
      "animation frame-set recipe sheet"));
  ASSIGN_OR_RETURN(pipeline.sheet.grid_x, Required<int>(sheet, "grid_x"));
  ASSIGN_OR_RETURN(pipeline.sheet.grid_y, Required<int>(sheet, "grid_y"));
  ASSIGN_OR_RETURN(pipeline.sheet.cell_width, Required<int>(sheet, "cell_width"));
  ASSIGN_OR_RETURN(pipeline.sheet.cell_height, Required<int>(sheet, "cell_height"));
  ASSIGN_OR_RETURN(pipeline.sheet.column_gap, Required<int>(sheet, "column_gap"));
  ASSIGN_OR_RETURN(pipeline.sheet.row_gap, Required<int>(sheet, "row_gap"));
  ASSIGN_OR_RETURN(pipeline.sheet.columns, Required<int>(sheet, "columns"));
  ASSIGN_OR_RETURN(pipeline.sheet.rows, Required<int>(sheet, "rows"));

  ASSIGN_OR_RETURN(pipeline.output_width, Required<int>(json, "output_width"));
  ASSIGN_OR_RETURN(pipeline.output_height, Required<int>(json, "output_height"));
  ASSIGN_OR_RETURN(pipeline.origin_x, Required<int>(json, "origin_x"));
  ASSIGN_OR_RETURN(pipeline.origin_y, Required<int>(json, "origin_y"));
  ASSIGN_OR_RETURN(pipeline.contact_line_y, Required<int>(json, "contact_line_y"));
  ASSIGN_OR_RETURN(pipeline.render_scale, Required<int>(json, "render_scale"));
  ASSIGN_OR_RETURN(pipeline.contact_tolerance, Required<int>(json, "contact_tolerance"));
  ASSIGN_OR_RETURN(pipeline.minimum_visible_pixels, Required<int>(json, "minimum_visible_pixels"));
  ASSIGN_OR_RETURN(pipeline.maximum_horizontal_anchor_drift,
                   Required<int>(json, "maximum_horizontal_anchor_drift"));
  ASSIGN_OR_RETURN(pipeline.maximum_vertical_anchor_drift,
                   Required<int>(json, "maximum_vertical_anchor_drift"));
  ASSIGN_OR_RETURN(pipeline.packing_columns, Required<int>(json, "packing_columns"));
  ASSIGN_OR_RETURN(const std::string playback_mode, Required<std::string>(json, "playback_mode"));
  ASSIGN_OR_RETURN(pipeline.playback_mode, PlaybackModeFromString(playback_mode));
  ASSIGN_OR_RETURN(pipeline.frames_per_cycle, Required<std::vector<int>>(json, "frames_per_cycle"));
  ASSIGN_OR_RETURN(pipeline.planted_frames, Required<std::vector<bool>>(json, "planted_frames"));
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
  RETURN_IF_ERROR(
      RequireExactObject(json,
                         {"index", "texture_x", "texture_y", "texture_w", "texture_h", "render_w",
                          "render_h", "frames_per_cycle", "offset_x", "offset_y"},
                         "animation frame-set recipe expected frame"));
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

nlohmann::json BindingToJson(const AnimationFrameSetBlueprintBinding& binding) {
  return {
      {"state_key", binding.state_key},
      {"previous_sprite_id", binding.previous_sprite_id},
  };
}

absl::StatusOr<AnimationFrameSetBlueprintBinding> BindingFromJson(const nlohmann::json& json) {
  RETURN_IF_ERROR(RequireExactObject(json, {"state_key", "previous_sprite_id"},
                                     "animation frame-set recipe Blueprint binding"));
  AnimationFrameSetBlueprintBinding binding;
  ASSIGN_OR_RETURN(binding.state_key, Required<std::string>(json, "state_key"));
  ASSIGN_OR_RETURN(binding.previous_sprite_id, Required<std::string>(json, "previous_sprite_id"));
  return binding;
}

absl::Status ValidateExpectedFrames(const AnimationFrameSetRecipe& recipe) {
  const size_t frame_count = recipe.pipeline.frames_per_cycle.size();
  if (recipe.expected_frames.size() != frame_count) {
    return absl::InvalidArgumentError(
        "animation frame-set recipe expected frames do not match its frame count");
  }
  const int frame_width = recipe.pipeline.output_width * recipe.pipeline.render_scale;
  const int frame_height = recipe.pipeline.output_height * recipe.pipeline.render_scale;
  for (size_t index = 0; index < frame_count; ++index) {
    const SpriteFrame expected{
        .index = static_cast<int>(index),
        .texture_x = static_cast<int>(index % recipe.pipeline.packing_columns) * frame_width,
        .texture_y = static_cast<int>(index / recipe.pipeline.packing_columns) * frame_height,
        .texture_w = frame_width,
        .texture_h = frame_height,
        .render_w = frame_width,
        .render_h = frame_height,
        .frames_per_cycle = recipe.pipeline.frames_per_cycle[index],
        .offset_x = -recipe.pipeline.origin_x * recipe.pipeline.render_scale,
        .offset_y = -recipe.pipeline.origin_y * recipe.pipeline.render_scale,
    };
    if (recipe.expected_frames[index] != expected) {
      return absl::InvalidArgumentError(
          absl::StrCat("animation frame-set recipe expected frame ", index, " is inconsistent"));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateAnimationFrameSetRecipe(const AnimationFrameSetRecipe& recipe) {
  if (recipe.id.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe ID is empty");
  }
  if (recipe.name.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe name is empty");
  }
  if (recipe.source_artwork_id.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe source artwork ID is empty");
  }
  if (recipe.texture_id.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe texture ID is empty");
  }
  if (recipe.sprite_id.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe sprite ID is empty");
  }
  if (recipe.blueprint_id.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe blueprint ID is empty");
  }
  const std::set<std::string> owned_ids = {
      recipe.id,
      recipe.texture_id,
      recipe.sprite_id,
  };
  if (owned_ids.size() != 3) {
    return absl::InvalidArgumentError("animation frame-set recipe owned IDs must be distinct");
  }
  if (recipe.blueprint_bindings.empty()) {
    return absl::InvalidArgumentError(
        "animation frame-set recipe must bind at least one Blueprint state");
  }
  std::set<std::string> state_keys;
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    if (!IsValidBlueprintStateKey(binding.state_key)) {
      return absl::InvalidArgumentError(
          "animation frame-set recipe Blueprint state key is invalid");
    }
    if (!state_keys.insert(binding.state_key).second) {
      return absl::InvalidArgumentError(
          "animation frame-set recipe Blueprint state keys must be unique");
    }
    if (binding.previous_sprite_id == recipe.sprite_id) {
      return absl::InvalidArgumentError(
          "animation frame-set recipe cannot restore its owned Sprite");
    }
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetPipelineConfig(recipe.pipeline, recipe.style));
  if (recipe.pipeline_version != kAnimationFrameSetPipelineVersion) {
    return absl::FailedPreconditionError(
        absl::StrCat("animation frame-set recipe pipeline version ", recipe.pipeline_version,
                     " is not supported version ", kAnimationFrameSetPipelineVersion));
  }
  RETURN_IF_ERROR(ValidateExpectedFrames(recipe));
  if (!IsLowercaseSha256Digest(recipe.final_pixel_digest)) {
    return absl::InvalidArgumentError(
        "animation frame-set recipe final pixel digest is not lowercase SHA-256");
  }
  return absl::OkStatus();
}

nlohmann::json AnimationFrameSetRecipeToJson(const AnimationFrameSetRecipe& recipe) {
  nlohmann::json bindings = nlohmann::json::array();
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    bindings.push_back(BindingToJson(binding));
  }
  nlohmann::json expected_frames = nlohmann::json::array();
  for (const SpriteFrame& frame : recipe.expected_frames) {
    expected_frames.push_back(FrameToJson(frame));
  }
  return {
      {"schema_version", kAnimationFrameSetRecipeSchemaVersion},
      {"id", recipe.id},
      {"name", recipe.name},
      {"source_artwork_id", recipe.source_artwork_id},
      {"style", StyleToJson(recipe.style)},
      {"pipeline", PipelineToJson(recipe.pipeline)},
      {"texture_id", recipe.texture_id},
      {"sprite_id", recipe.sprite_id},
      {"blueprint_id", recipe.blueprint_id},
      {"blueprint_bindings", std::move(bindings)},
      {"expected_frames", std::move(expected_frames)},
      {"final_pixel_digest", recipe.final_pixel_digest},
      {"pipeline_version", recipe.pipeline_version},
  };
}

absl::StatusOr<AnimationFrameSetRecipe> AnimationFrameSetRecipeFromJson(
    const nlohmann::json& json) {
  RETURN_IF_ERROR(
      RequireExactObject(json,
                         {"schema_version", "id", "name", "source_artwork_id", "style", "pipeline",
                          "texture_id", "sprite_id", "blueprint_id", "blueprint_bindings",
                          "expected_frames", "final_pixel_digest", "pipeline_version"},
                         "animation frame-set recipe"));
  ASSIGN_OR_RETURN(const int schema_version, Required<int>(json, "schema_version"));
  if (schema_version != kAnimationFrameSetRecipeSchemaVersion) {
    return absl::FailedPreconditionError(
        absl::StrCat("animation frame-set recipe schema version ", schema_version,
                     " is not version ", kAnimationFrameSetRecipeSchemaVersion,
                     "; run scripts/migrate_definitions.py to bring it forward"));
  }

  AnimationFrameSetRecipe recipe;
  ASSIGN_OR_RETURN(recipe.id, Required<std::string>(json, "id"));
  ASSIGN_OR_RETURN(recipe.name, Required<std::string>(json, "name"));
  ASSIGN_OR_RETURN(recipe.source_artwork_id, Required<std::string>(json, "source_artwork_id"));
  ASSIGN_OR_RETURN(const nlohmann::json style, Required<nlohmann::json>(json, "style"));
  ASSIGN_OR_RETURN(recipe.style, StyleFromJson(style));
  ASSIGN_OR_RETURN(const nlohmann::json pipeline, Required<nlohmann::json>(json, "pipeline"));
  ASSIGN_OR_RETURN(recipe.pipeline, PipelineFromJson(pipeline));
  ASSIGN_OR_RETURN(recipe.texture_id, Required<std::string>(json, "texture_id"));
  ASSIGN_OR_RETURN(recipe.sprite_id, Required<std::string>(json, "sprite_id"));
  ASSIGN_OR_RETURN(recipe.blueprint_id, Required<std::string>(json, "blueprint_id"));

  ASSIGN_OR_RETURN(const nlohmann::json bindings,
                   Required<nlohmann::json>(json, "blueprint_bindings"));
  if (!bindings.is_array()) {
    return absl::InvalidArgumentError(
        "animation frame-set recipe Blueprint bindings must be an array");
  }
  recipe.blueprint_bindings.reserve(bindings.size());
  for (const nlohmann::json& binding_json : bindings) {
    ASSIGN_OR_RETURN(AnimationFrameSetBlueprintBinding binding, BindingFromJson(binding_json));
    recipe.blueprint_bindings.push_back(std::move(binding));
  }

  ASSIGN_OR_RETURN(const nlohmann::json expected_frames,
                   Required<nlohmann::json>(json, "expected_frames"));
  if (!expected_frames.is_array()) {
    return absl::InvalidArgumentError(
        "animation frame-set recipe expected frames must be an array");
  }
  recipe.expected_frames.reserve(expected_frames.size());
  for (const nlohmann::json& frame_json : expected_frames) {
    ASSIGN_OR_RETURN(SpriteFrame frame, FrameFromJson(frame_json));
    recipe.expected_frames.push_back(frame);
  }
  ASSIGN_OR_RETURN(recipe.final_pixel_digest, Required<std::string>(json, "final_pixel_digest"));
  ASSIGN_OR_RETURN(recipe.pipeline_version, Required<int>(json, "pipeline_version"));
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(recipe));
  return recipe;
}

}  // namespace zebes
