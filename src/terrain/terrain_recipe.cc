#include "terrain/terrain_recipe.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
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
    return absl::InvalidArgumentError(absl::StrCat("terrain recipe is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain recipe field '", key, "' is invalid: ", error.what()));
  }
}

template <typename Enum>
absl::StatusOr<Enum> RequiredEnum(const nlohmann::json& json, const char* key) {
  ASSIGN_OR_RETURN(const int value, Required<int>(json, key));
  if (value < 0 || value > std::numeric_limits<uint8_t>::max()) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain recipe field '", key, "' has unknown value ", value));
  }
  return static_cast<Enum>(value);
}

nlohmann::json ConfigToJson(const TerrainGenConfig& config) {
  return {
      {"tile_size", config.tile_size},
      {"supersample", config.supersample},
      {"variant_period", config.variant_period},
      {"pixel_profile", static_cast<int>(config.pixel_profile)},
      {"surface",
       {{"top_depth", config.surface.top_depth},
        {"side_depth", config.surface.side_depth},
        {"underside_depth", config.surface.underside_depth},
        {"ruffle_amplitude", config.surface.ruffle_amplitude},
        {"ruffle_density", config.surface.ruffle_density},
        {"ruffle_sharpness", config.surface.ruffle_sharpness},
        {"ruffle_octaves", config.surface.ruffle_octaves},
        {"outline_depth", config.surface.outline_depth},
        {"highlight_depth", config.surface.highlight_depth},
        {"shade_depth", config.surface.shade_depth},
        {"contact_depth", config.surface.contact_depth},
        {"wall_depth", config.surface.wall_depth},
        {"wall_darkness", config.surface.wall_darkness},
        {"texture_size", config.surface.texture_size},
        {"texture_amount", config.surface.texture_amount},
        {"edge_detail",
         {{"family", static_cast<int>(config.surface.edge_detail.family)},
          {"amount", config.surface.edge_detail.amount},
          {"length", config.surface.edge_detail.length},
          {"clump_size", config.surface.edge_detail.clump_size},
          {"lean", config.surface.edge_detail.lean},
          {"highlight", config.surface.edge_detail.highlight}}}}},
      {"interior",
       {{"base",
         {{"style", static_cast<int>(config.interior.base.style)},
          {"mottle_density", config.interior.base.mottle_density},
          {"mottle_coverage", config.interior.base.mottle_coverage},
          {"feature_size", config.interior.base.feature_size},
          {"relief", config.interior.base.relief}}},
        {"pattern",
         {{"family", static_cast<int>(config.interior.pattern.family)},
          {"density", config.interior.pattern.density},
          {"spacing", config.interior.pattern.spacing},
          {"margin", config.interior.pattern.margin},
          {"contrast", config.interior.pattern.contrast},
          {"scale", config.interior.pattern.scale},
          {"accent_mode", static_cast<int>(config.interior.pattern.accent_mode)}}},
        {"details",
         {{"family", static_cast<int>(config.interior.details.family)},
          {"density", config.interior.details.density},
          {"spacing", config.interior.details.spacing},
          {"margin", config.interior.details.margin},
          {"scale", config.interior.details.scale},
          {"accent_mode", static_cast<int>(config.interior.details.accent_mode)}}}}},
      {"seed", config.seed},
      {"material",
       {{"name", config.material.name},
        {"surface", config.material.surface},
        {"substrate", config.material.substrate},
        {"outline", config.material.outline},
        {"accent_primary", config.material.accent_primary},
        {"accent_secondary", config.material.accent_secondary},
        {"hue_shift", config.material.hue_shift},
        {"contrast", config.material.contrast},
        {"surface_style", static_cast<int>(config.material.surface_style)}}},
  };
}

absl::StatusOr<TerrainGenConfig> ConfigFromJson(const nlohmann::json& json) {
  TerrainGenConfig config;
  ASSIGN_OR_RETURN(config.tile_size, Required<int>(json, "tile_size"));
  ASSIGN_OR_RETURN(config.supersample, Required<int>(json, "supersample"));
  ASSIGN_OR_RETURN(config.variant_period, Required<int>(json, "variant_period"));
  ASSIGN_OR_RETURN(config.pixel_profile, RequiredEnum<TerrainPixelProfile>(json, "pixel_profile"));

  ASSIGN_OR_RETURN(const nlohmann::json surface, Required<nlohmann::json>(json, "surface"));
  ASSIGN_OR_RETURN(config.surface.top_depth, Required<float>(surface, "top_depth"));
  ASSIGN_OR_RETURN(config.surface.side_depth, Required<float>(surface, "side_depth"));
  ASSIGN_OR_RETURN(config.surface.underside_depth, Required<float>(surface, "underside_depth"));
  ASSIGN_OR_RETURN(config.surface.outline_depth, Required<int>(surface, "outline_depth"));
  ASSIGN_OR_RETURN(config.surface.highlight_depth, Required<int>(surface, "highlight_depth"));
  ASSIGN_OR_RETURN(config.surface.shade_depth, Required<int>(surface, "shade_depth"));
  ASSIGN_OR_RETURN(config.surface.wall_depth, Required<int>(surface, "wall_depth"));
  ASSIGN_OR_RETURN(config.surface.wall_darkness, Required<float>(surface, "wall_darkness"));
  ASSIGN_OR_RETURN(config.surface.ruffle_amplitude, Required<float>(surface, "ruffle_amplitude"));
  ASSIGN_OR_RETURN(config.surface.ruffle_density, Required<float>(surface, "ruffle_density"));
  ASSIGN_OR_RETURN(config.surface.ruffle_sharpness, Required<float>(surface, "ruffle_sharpness"));
  ASSIGN_OR_RETURN(config.surface.ruffle_octaves, Required<int>(surface, "ruffle_octaves"));
  ASSIGN_OR_RETURN(config.surface.contact_depth, Required<int>(surface, "contact_depth"));
  ASSIGN_OR_RETURN(config.surface.texture_size, Required<float>(surface, "texture_size"));
  ASSIGN_OR_RETURN(config.surface.texture_amount, Required<float>(surface, "texture_amount"));
  ASSIGN_OR_RETURN(const nlohmann::json edge, Required<nlohmann::json>(surface, "edge_detail"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.family,
                   RequiredEnum<TerrainEdgeDetailSet>(edge, "family"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.amount, Required<float>(edge, "amount"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.length, Required<int>(edge, "length"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.clump_size, Required<int>(edge, "clump_size"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.lean, Required<float>(edge, "lean"));
  ASSIGN_OR_RETURN(config.surface.edge_detail.highlight, Required<float>(edge, "highlight"));

  ASSIGN_OR_RETURN(const nlohmann::json interior, Required<nlohmann::json>(json, "interior"));
  ASSIGN_OR_RETURN(const nlohmann::json base, Required<nlohmann::json>(interior, "base"));
  ASSIGN_OR_RETURN(config.interior.base.style, RequiredEnum<TerrainInteriorStyle>(base, "style"));
  ASSIGN_OR_RETURN(config.interior.base.mottle_density, Required<float>(base, "mottle_density"));
  ASSIGN_OR_RETURN(config.interior.base.mottle_coverage, Required<float>(base, "mottle_coverage"));
  ASSIGN_OR_RETURN(config.interior.base.feature_size, Required<float>(base, "feature_size"));
  ASSIGN_OR_RETURN(config.interior.base.relief, Required<float>(base, "relief"));

  ASSIGN_OR_RETURN(const nlohmann::json pattern, Required<nlohmann::json>(interior, "pattern"));
  ASSIGN_OR_RETURN(config.interior.pattern.family,
                   RequiredEnum<TerrainSubstratePattern>(pattern, "family"));
  ASSIGN_OR_RETURN(config.interior.pattern.density, Required<int>(pattern, "density"));
  ASSIGN_OR_RETURN(config.interior.pattern.spacing, Required<int>(pattern, "spacing"));
  ASSIGN_OR_RETURN(config.interior.pattern.margin, Required<int>(pattern, "margin"));
  ASSIGN_OR_RETURN(config.interior.pattern.contrast, Required<float>(pattern, "contrast"));
  ASSIGN_OR_RETURN(config.interior.pattern.scale, Required<int>(pattern, "scale"));
  ASSIGN_OR_RETURN(config.interior.pattern.accent_mode,
                   RequiredEnum<TerrainAccentMode>(pattern, "accent_mode"));

  ASSIGN_OR_RETURN(const nlohmann::json details, Required<nlohmann::json>(interior, "details"));
  ASSIGN_OR_RETURN(config.interior.details.family,
                   RequiredEnum<TerrainDetailSet>(details, "family"));
  ASSIGN_OR_RETURN(config.interior.details.density, Required<int>(details, "density"));
  ASSIGN_OR_RETURN(config.interior.details.spacing, Required<int>(details, "spacing"));
  ASSIGN_OR_RETURN(config.interior.details.margin, Required<int>(details, "margin"));
  ASSIGN_OR_RETURN(config.interior.details.scale, Required<int>(details, "scale"));
  ASSIGN_OR_RETURN(config.interior.details.accent_mode,
                   RequiredEnum<TerrainAccentMode>(details, "accent_mode"));

  ASSIGN_OR_RETURN(config.seed, Required<uint64_t>(json, "seed"));
  ASSIGN_OR_RETURN(const nlohmann::json material, Required<nlohmann::json>(json, "material"));
  ASSIGN_OR_RETURN(config.material.name, Required<std::string>(material, "name"));
  ASSIGN_OR_RETURN(config.material.surface, Required<uint32_t>(material, "surface"));
  ASSIGN_OR_RETURN(config.material.substrate, Required<uint32_t>(material, "substrate"));
  ASSIGN_OR_RETURN(config.material.outline, Required<uint32_t>(material, "outline"));
  ASSIGN_OR_RETURN(config.material.accent_primary, Required<uint32_t>(material, "accent_primary"));
  ASSIGN_OR_RETURN(config.material.accent_secondary,
                   Required<uint32_t>(material, "accent_secondary"));
  ASSIGN_OR_RETURN(config.material.hue_shift, Required<float>(material, "hue_shift"));
  ASSIGN_OR_RETURN(config.material.contrast, Required<float>(material, "contrast"));
  ASSIGN_OR_RETURN(config.material.surface_style,
                   RequiredEnum<TerrainSurfaceStyle>(material, "surface_style"));

  // This also rejects non-finite values and inconsistent ranges, so a recipe
  // cannot enter the editor in a state the renderer could never consume.
  RETURN_IF_ERROR(ResolveTerrainStyle(config).status());
  return config;
}

}  // namespace

nlohmann::json TerrainGenConfigToJson(const TerrainGenConfig& config) {
  return ConfigToJson(config);
}

absl::StatusOr<TerrainGenConfig> TerrainGenConfigFromJson(const nlohmann::json& json) {
  return ConfigFromJson(json);
}

nlohmann::json TerrainRecipeToJson(const TerrainRecipe& recipe) {
  nlohmann::json json = {
      {"schema_version", kTerrainRecipeSchemaVersion},
      {"id", recipe.id},
      {"name", recipe.name},
      {"tileset_id", recipe.tileset_id},
      {"texture_id", recipe.texture_id},
      {"terrain_id", recipe.terrain_id},
      {"config", TerrainGenConfigToJson(recipe.config)},
  };
  if (recipe.source_preset.has_value()) json["source_preset"] = *recipe.source_preset;
  return json;
}

absl::StatusOr<TerrainRecipe> TerrainRecipeFromJson(const nlohmann::json& json) {
  ASSIGN_OR_RETURN(const int schema_version, Required<int>(json, "schema_version"));
  if (schema_version != kTerrainRecipeSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat(
        "terrain recipe schema version ", schema_version, " is not version ",
        kTerrainRecipeSchemaVersion, "; run scripts/migrate_definitions.py to bring it forward"));
  }

  TerrainRecipe recipe;
  ASSIGN_OR_RETURN(recipe.id, Required<std::string>(json, "id"));
  ASSIGN_OR_RETURN(recipe.name, Required<std::string>(json, "name"));
  ASSIGN_OR_RETURN(recipe.tileset_id, Required<std::string>(json, "tileset_id"));
  ASSIGN_OR_RETURN(recipe.texture_id, Required<std::string>(json, "texture_id"));
  ASSIGN_OR_RETURN(recipe.terrain_id, Required<int>(json, "terrain_id"));
  if (json.contains("source_preset")) {
    ASSIGN_OR_RETURN(std::string source_preset, Required<std::string>(json, "source_preset"));
    recipe.source_preset = std::move(source_preset);
  }
  ASSIGN_OR_RETURN(const nlohmann::json config, Required<nlohmann::json>(json, "config"));
  ASSIGN_OR_RETURN(recipe.config, TerrainGenConfigFromJson(config));

  if (recipe.id.empty() || recipe.name.empty() || recipe.tileset_id.empty() ||
      recipe.texture_id.empty() || recipe.terrain_id <= 0) {
    return absl::InvalidArgumentError(
        "terrain recipe needs non-empty asset IDs, a name, and a positive terrain ID");
  }
  return recipe;
}

}  // namespace zebes
