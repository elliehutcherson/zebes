#include "objects/parallax_theme.h"

#include <cmath>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

bool Finite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

}  // namespace

absl::Status ValidateParallaxTheme(const ParallaxTheme& theme) {
  if (theme.id.empty()) return absl::InvalidArgumentError("Parallax theme must have an ID.");
  if (theme.name.empty()) return absl::InvalidArgumentError("Parallax theme name cannot be empty.");
  if (theme.layers.empty()) {
    return absl::InvalidArgumentError("Parallax theme must contain at least one layer.");
  }
  for (size_t index = 0; index < theme.layers.size(); ++index) {
    const ParallaxLayer& layer = theme.layers[index];
    if (layer.name.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer ", index, " name cannot be empty."));
    }
    if (layer.texture_id.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' must select a texture."));
    }
    if (!Finite(layer.scroll_factor) || !Finite(layer.offset) || !std::isfinite(layer.base_scale)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' values must be finite."));
    }
    if (layer.base_scale <= 0.0f) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' scale must be positive."));
    }
  }
  return absl::OkStatus();
}

nlohmann::json ParallaxThemeToJson(const ParallaxTheme& theme) {
  nlohmann::json layers = nlohmann::json::array();
  for (const ParallaxLayer& layer : theme.layers) {
    layers.push_back({
        {"name", layer.name},
        {"texture_id", layer.texture_id},
        {"scroll_factor_x", layer.scroll_factor.x},
        {"scroll_factor_y", layer.scroll_factor.y},
        {"offset_x", layer.offset.x},
        {"offset_y", layer.offset.y},
        {"repeat_x", layer.repeat_x},
        {"repeat_y", layer.repeat_y},
        {"base_scale", layer.base_scale},
    });
  }
  return {
      {"id", theme.id},
      {"name", theme.name},
      {"layers", std::move(layers)},
  };
}

absl::StatusOr<ParallaxTheme> ParallaxThemeFromJson(const nlohmann::json& json) {
  try {
    ParallaxTheme theme;
    json.at("id").get_to(theme.id);
    json.at("name").get_to(theme.name);
    for (const nlohmann::json& item : json.at("layers")) {
      ParallaxLayer layer;
      item.at("name").get_to(layer.name);
      item.at("texture_id").get_to(layer.texture_id);
      item.at("scroll_factor_x").get_to(layer.scroll_factor.x);
      item.at("scroll_factor_y").get_to(layer.scroll_factor.y);
      item.at("offset_x").get_to(layer.offset.x);
      item.at("offset_y").get_to(layer.offset.y);
      item.at("repeat_x").get_to(layer.repeat_x);
      item.at("repeat_y").get_to(layer.repeat_y);
      item.at("base_scale").get_to(layer.base_scale);
      theme.layers.push_back(std::move(layer));
    }
    RETURN_IF_ERROR(ValidateParallaxTheme(theme));
    return theme;
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("invalid parallax theme JSON: ", error.what()));
  }
}

}  // namespace zebes
