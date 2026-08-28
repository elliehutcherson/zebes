#include "authoring/terrain_builder.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "common/status_macros.h"
#include "editor/terrain_editor/terrain_creation.h"
#include "nlohmann/json.hpp"
#include "objects/tileset.h"
#include "terrain/terrain_recipe.h"

namespace zebes {
namespace {

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key, std::string_view context) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const nlohmann::json::exception& error) {
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

absl::Status ValidateSpec(const TerrainBuildSpec& spec) {
  if (spec.name.empty()) return absl::InvalidArgumentError("terrain build spec name is empty");
  if (spec.config.material.name != spec.name) {
    return absl::InvalidArgumentError(absl::StrCat("terrain build spec name '", spec.name,
                                                   "' does not match material name '",
                                                   spec.config.material.name, "'"));
  }
  return ResolveTerrainStyle(spec.config).status();
}

absl::Status RequireExactConfigShape(const nlohmann::json& config) {
  RETURN_IF_ERROR(RequireExactObject(config,
                                     {"tile_size", "supersample", "variant_period", "pixel_profile",
                                      "surface", "interior", "seed", "material"},
                                     "terrain build config"));
  const nlohmann::json& surface = config.at("surface");
  RETURN_IF_ERROR(
      RequireExactObject(surface,
                         {"top_depth", "side_depth", "underside_depth", "ruffle_amplitude",
                          "ruffle_density", "ruffle_sharpness", "ruffle_octaves", "outline_depth",
                          "highlight_depth", "shade_depth", "contact_depth", "wall_depth",
                          "wall_darkness", "texture_size", "texture_amount", "edge_detail"},
                         "terrain build surface config"));
  RETURN_IF_ERROR(RequireExactObject(
      surface.at("edge_detail"), {"family", "amount", "length", "clump_size", "lean", "highlight"},
      "terrain build edge-detail config"));

  const nlohmann::json& interior = config.at("interior");
  RETURN_IF_ERROR(RequireExactObject(interior, {"base", "pattern", "details"},
                                     "terrain build interior config"));
  RETURN_IF_ERROR(RequireExactObject(
      interior.at("base"), {"style", "mottle_density", "mottle_coverage", "feature_size", "relief"},
      "terrain build interior-base config"));
  RETURN_IF_ERROR(RequireExactObject(
      interior.at("pattern"),
      {"family", "density", "spacing", "margin", "contrast", "scale", "accent_mode"},
      "terrain build substrate-pattern config"));
  RETURN_IF_ERROR(RequireExactObject(
      interior.at("details"), {"family", "density", "spacing", "margin", "scale", "accent_mode"},
      "terrain build detail config"));
  return RequireExactObject(config.at("material"),
                            {"name", "surface", "substrate", "outline", "accent_primary",
                             "accent_secondary", "hue_shift", "contrast", "surface_style"},
                            "terrain build material config");
}

absl::StatusOr<std::optional<TerrainRecipe>> FindUniqueRecipeByName(
    const std::vector<TerrainRecipe>& recipes, std::string_view name) {
  std::optional<TerrainRecipe> found;
  for (const TerrainRecipe& recipe : recipes) {
    if (recipe.name != name) continue;
    if (found.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("more than one terrain recipe is named '", name, "'"));
    }
    found = recipe;
  }
  return found;
}

absl::Status ValidateExistingBundle(const TerrainBuildSpec& spec, const TerrainRecipe& recipe,
                                    const Tileset& tileset) {
  if (tileset.name != spec.name) {
    return absl::FailedPreconditionError(
        absl::StrCat("terrain recipe '", spec.name, "' owns tileset '", tileset.name,
                     "'; refusing to update an ambiguously renamed bundle"));
  }
  int matching_terrains = 0;
  for (const Terrain& terrain : tileset.terrains) {
    if (terrain.id == recipe.terrain_id && terrain.name == spec.name) ++matching_terrains;
  }
  if (matching_terrains != 1) {
    return absl::FailedPreconditionError(
        absl::StrCat("terrain recipe '", spec.name,
                     "' does not own exactly one same-named terrain in its tileset"));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TerrainBuildSpec> TerrainBuildSpecFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "terrain build spec";
  RETURN_IF_ERROR(RequireExactObject(json, {"schema_version", "name", "config"}, kContext));
  ASSIGN_OR_RETURN(const int schema, Required<int>(json, "schema_version", kContext));
  if (schema != kTerrainBuildSpecSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat("terrain build schema version ", schema,
                                                      " is not supported version ",
                                                      kTerrainBuildSpecSchemaVersion));
  }
  TerrainBuildSpec spec;
  ASSIGN_OR_RETURN(spec.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json config, Required<nlohmann::json>(json, "config", kContext));
  RETURN_IF_ERROR(RequireExactConfigShape(config));
  ASSIGN_OR_RETURN(spec.config, TerrainGenConfigFromJson(config));
  RETURN_IF_ERROR(ValidateSpec(spec));
  return spec;
}

absl::StatusOr<TerrainBuildSpec> ReadTerrainBuildSpec(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open terrain spec: ", path.string()));
  }
  nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
  if (json.is_discarded()) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain spec is not valid JSON: ", path.string()));
  }
  return TerrainBuildSpecFromJson(json);
}

absl::StatusOr<TerrainBuildResult> BuildTerrain(Api& api, const TerrainBuildSpec& spec) {
  RETURN_IF_ERROR(ValidateSpec(spec));
  ASSIGN_OR_RETURN(std::optional<TerrainRecipe> existing,
                   FindUniqueRecipeByName(api.GetAllTerrainRecipes(), spec.name));
  if (!existing.has_value()) {
    ASSIGN_OR_RETURN(CreatedTerrain created,
                     CreateGeneratedTerrainTileset(api, spec.name, spec.config, std::nullopt));
    return TerrainBuildResult{
        .recipe_id = created.recipe_id,
        .tileset_id = created.tileset_id,
        .texture_id = created.texture_id,
        .tile_count = created.tile_count,
        .created = true,
    };
  }

  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(existing->tileset_id));
  if (tileset == nullptr) {
    return absl::FailedPreconditionError("terrain tileset lookup returned null");
  }
  RETURN_IF_ERROR(ValidateExistingBundle(spec, *existing, *tileset));
  ASSIGN_OR_RETURN(PreparedTerrainRegeneration prepared,
                   PrepareTerrainRegeneration(*tileset, *existing, spec.config));
  const int tile_count = static_cast<int>(tileset->tiles.size());
  RETURN_IF_ERROR(CommitTerrainRegeneration(api, *existing, spec.config, std::move(prepared)));
  return TerrainBuildResult{
      .recipe_id = existing->id,
      .tileset_id = existing->tileset_id,
      .texture_id = existing->texture_id,
      .tile_count = tile_count,
      .created = false,
  };
}

}  // namespace zebes
