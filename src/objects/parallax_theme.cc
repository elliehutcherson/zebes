#include "objects/parallax_theme.h"

#include <cmath>
#include <set>

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
    if (!Finite(layer.scroll_factor) || !Finite(layer.offset) || !Finite(layer.repeat_period)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' values must be finite."));
    }
    if (layer.repeat_period.x < 0.0 || layer.repeat_period.y < 0.0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' repeat period cannot be negative."));
    }
    if (layer.elements.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parallax layer '", layer.name, "' must contain at least one element."));
    }

    std::set<int> element_ids;
    for (size_t element_index = 0; element_index < layer.elements.size(); ++element_index) {
      const ParallaxElement& element = layer.elements[element_index];
      if (element.id < 0 || !element_ids.insert(element.id).second) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Parallax layer '", layer.name, "' element IDs must be unique and nonnegative."));
      }
      if (element.name.empty()) {
        return absl::InvalidArgumentError(absl::StrCat("Parallax layer '", layer.name, "' element ",
                                                       element_index, " name cannot be empty."));
      }
      if (element.texture_id.empty()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Parallax element '", element.name, "' must select a texture."));
      }
      if (!Finite(element.position) || !std::isfinite(element.scale)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Parallax element '", element.name, "' values must be finite."));
      }
      if (element.scale <= 0.0f) {
        return absl::InvalidArgumentError(
            absl::StrCat("Parallax element '", element.name, "' scale must be positive."));
      }
    }
  }
  return absl::OkStatus();
}

nlohmann::json ParallaxThemeToJson(const ParallaxTheme& theme) {
  nlohmann::json layers = nlohmann::json::array();
  for (const ParallaxLayer& layer : theme.layers) {
    nlohmann::json elements = nlohmann::json::array();
    for (const ParallaxElement& element : layer.elements) {
      elements.push_back({
          {"id", element.id},
          {"name", element.name},
          {"texture_id", element.texture_id},
          {"position_x", element.position.x},
          {"position_y", element.position.y},
          {"scale", element.scale},
      });
    }
    layers.push_back({
        {"name", layer.name},
        {"scroll_factor_x", layer.scroll_factor.x},
        {"scroll_factor_y", layer.scroll_factor.y},
        {"offset_x", layer.offset.x},
        {"offset_y", layer.offset.y},
        {"repeat_period_x", layer.repeat_period.x},
        {"repeat_period_y", layer.repeat_period.y},
        {"elements", std::move(elements)},
    });
  }
  return {
      {"schema_version", kParallaxThemeSchemaVersion},
      {"id", theme.id},
      {"name", theme.name},
      {"layers", std::move(layers)},
  };
}

absl::StatusOr<ParallaxTheme> ParallaxThemeFromJson(const nlohmann::json& json) {
  try {
    ParallaxTheme theme;
    const int schema_version = json.at("schema_version").get<int>();
    if (schema_version != kParallaxThemeSchemaVersion) {
      return absl::InvalidArgumentError(absl::StrCat("unsupported parallax theme schema version ",
                                                     schema_version,
                                                     "; run scripts/migrate_definitions.py"));
    }
    json.at("id").get_to(theme.id);
    json.at("name").get_to(theme.name);
    for (const nlohmann::json& item : json.at("layers")) {
      ParallaxLayer layer;
      item.at("name").get_to(layer.name);
      item.at("scroll_factor_x").get_to(layer.scroll_factor.x);
      item.at("scroll_factor_y").get_to(layer.scroll_factor.y);
      item.at("offset_x").get_to(layer.offset.x);
      item.at("offset_y").get_to(layer.offset.y);
      item.at("repeat_period_x").get_to(layer.repeat_period.x);
      item.at("repeat_period_y").get_to(layer.repeat_period.y);
      for (const nlohmann::json& element_json : item.at("elements")) {
        ParallaxElement element;
        element_json.at("id").get_to(element.id);
        element_json.at("name").get_to(element.name);
        element_json.at("texture_id").get_to(element.texture_id);
        element_json.at("position_x").get_to(element.position.x);
        element_json.at("position_y").get_to(element.position.y);
        element_json.at("scale").get_to(element.scale);
        layer.elements.push_back(std::move(element));
      }
      theme.layers.push_back(std::move(layer));
    }
    RETURN_IF_ERROR(ValidateParallaxTheme(theme));
    return theme;
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("invalid parallax theme JSON: ", error.what()));
  }
}

}  // namespace zebes
