#include "authoring/environment_builder.h"

#include <algorithm>
#include <cmath>
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
#include "editor/level_editor/derived_terrain_session.h"
#include "editor/level_editor/terrain_brush.h"
#include "nlohmann/json.hpp"
#include "objects/entity_factory.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/tileset.h"

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

absl::StatusOr<Vec> VecFromJson(const nlohmann::json& json, std::string_view context) {
  if (!json.is_array() || json.size() != 2 || !json[0].is_number() || !json[1].is_number()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be a two-number array"));
  }
  const Vec value{json[0].get<double>(), json[1].get<double>()};
  if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must contain finite numbers"));
  }
  return value;
}

absl::StatusOr<EnvironmentElementSpec> ElementFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment parallax element";
  RETURN_IF_ERROR(
      RequireExactObject(json, {"name", "artwork_recipe_name", "position", "scale"}, kContext));
  EnvironmentElementSpec element;
  ASSIGN_OR_RETURN(element.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(element.artwork_recipe_name,
                   Required<std::string>(json, "artwork_recipe_name", kContext));
  ASSIGN_OR_RETURN(element.position, VecFromJson(json.at("position"), "element position"));
  ASSIGN_OR_RETURN(element.scale, Required<float>(json, "scale", kContext));
  return element;
}

absl::StatusOr<EnvironmentParallaxLayerSpec> ParallaxLayerFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment parallax layer";
  RETURN_IF_ERROR(RequireExactObject(
      json, {"name", "scroll_factor", "offset", "repeat_period", "elements"}, kContext));
  EnvironmentParallaxLayerSpec layer;
  ASSIGN_OR_RETURN(layer.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(layer.scroll_factor,
                   VecFromJson(json.at("scroll_factor"), "layer scroll factor"));
  ASSIGN_OR_RETURN(layer.offset, VecFromJson(json.at("offset"), "layer offset"));
  ASSIGN_OR_RETURN(layer.repeat_period,
                   VecFromJson(json.at("repeat_period"), "layer repeat period"));
  ASSIGN_OR_RETURN(const nlohmann::json elements,
                   Required<nlohmann::json>(json, "elements", kContext));
  if (!elements.is_array()) {
    return absl::InvalidArgumentError("environment parallax layer elements must be an array");
  }
  for (const nlohmann::json& item : elements) {
    ASSIGN_OR_RETURN(EnvironmentElementSpec element, ElementFromJson(item));
    layer.elements.push_back(std::move(element));
  }
  return layer;
}

absl::StatusOr<EnvironmentThemeSpec> ThemeFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment theme";
  RETURN_IF_ERROR(RequireExactObject(json, {"name", "layers"}, kContext));
  EnvironmentThemeSpec theme;
  ASSIGN_OR_RETURN(theme.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json layers, Required<nlohmann::json>(json, "layers", kContext));
  if (!layers.is_array()) return absl::InvalidArgumentError("environment layers must be an array");
  for (const nlohmann::json& item : layers) {
    ASSIGN_OR_RETURN(EnvironmentParallaxLayerSpec layer, ParallaxLayerFromJson(item));
    theme.layers.push_back(std::move(layer));
  }
  return theme;
}

absl::StatusOr<EnvironmentZoneSpec> ZoneFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment zone";
  RETURN_IF_ERROR(RequireExactObject(
      json, {"name", "theme_name", "min_pixels", "max_pixels", "fade_pixels"}, kContext));
  EnvironmentZoneSpec zone;
  ASSIGN_OR_RETURN(zone.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(zone.theme_name, Required<std::string>(json, "theme_name", kContext));
  ASSIGN_OR_RETURN(zone.min_point, VecFromJson(json.at("min_pixels"), "zone minimum"));
  ASSIGN_OR_RETURN(zone.max_point, VecFromJson(json.at("max_pixels"), "zone maximum"));
  ASSIGN_OR_RETURN(zone.fade_length, VecFromJson(json.at("fade_pixels"), "zone fade"));
  return zone;
}

absl::StatusOr<EnvironmentTerrainRectangle> RectangleFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment terrain rectangle";
  RETURN_IF_ERROR(RequireExactObject(json, {"x", "y", "width", "height", "solid"}, kContext));
  EnvironmentTerrainRectangle rectangle;
  ASSIGN_OR_RETURN(rectangle.x, Required<int>(json, "x", kContext));
  ASSIGN_OR_RETURN(rectangle.y, Required<int>(json, "y", kContext));
  ASSIGN_OR_RETURN(rectangle.width, Required<int>(json, "width", kContext));
  ASSIGN_OR_RETURN(rectangle.height, Required<int>(json, "height", kContext));
  ASSIGN_OR_RETURN(rectangle.solid, Required<bool>(json, "solid", kContext));
  return rectangle;
}

absl::StatusOr<EnvironmentEntitySpec> EntityFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment entity";
  RETURN_IF_ERROR(RequireExactObject(json,
                                     {"id", "layer_name", "blueprint_name", "state_key", "active",
                                      "position_pixels", "sort_order"},
                                     kContext));
  EnvironmentEntitySpec entity;
  ASSIGN_OR_RETURN(entity.id, Required<uint64_t>(json, "id", kContext));
  ASSIGN_OR_RETURN(entity.layer_name, Required<std::string>(json, "layer_name", kContext));
  ASSIGN_OR_RETURN(entity.blueprint_name, Required<std::string>(json, "blueprint_name", kContext));
  ASSIGN_OR_RETURN(entity.state_key, Required<std::string>(json, "state_key", kContext));
  ASSIGN_OR_RETURN(entity.active, Required<bool>(json, "active", kContext));
  ASSIGN_OR_RETURN(entity.position, VecFromJson(json.at("position_pixels"), "entity position"));
  ASSIGN_OR_RETURN(entity.sort_order, Required<int>(json, "sort_order", kContext));
  return entity;
}

absl::StatusOr<EnvironmentLevelSpec> LevelFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment level";
  RETURN_IF_ERROR(RequireExactObject(
      json,
      {"name", "tileset_name", "terrain_name", "tile_render_size", "grid_size", "spawn_pixels",
       "world_layers", "gameplay_layer", "zones", "entities", "terrain_rectangles"},
      kContext));
  EnvironmentLevelSpec level;
  ASSIGN_OR_RETURN(level.name, Required<std::string>(json, "name", kContext));
  ASSIGN_OR_RETURN(level.tileset_name, Required<std::string>(json, "tileset_name", kContext));
  ASSIGN_OR_RETURN(level.terrain_name, Required<std::string>(json, "terrain_name", kContext));
  ASSIGN_OR_RETURN(const Vec tile_size,
                   VecFromJson(json.at("tile_render_size"), "tile render size"));
  ASSIGN_OR_RETURN(const Vec grid_size, VecFromJson(json.at("grid_size"), "grid size"));
  if (std::floor(tile_size.x) != tile_size.x || std::floor(tile_size.y) != tile_size.y ||
      std::floor(grid_size.x) != grid_size.x || std::floor(grid_size.y) != grid_size.y) {
    return absl::InvalidArgumentError("tile render size and grid size must contain integers");
  }
  level.tile_render_width = static_cast<int>(tile_size.x);
  level.tile_render_height = static_cast<int>(tile_size.y);
  level.columns = static_cast<int>(grid_size.x);
  level.rows = static_cast<int>(grid_size.y);
  ASSIGN_OR_RETURN(level.spawn_point, VecFromJson(json.at("spawn_pixels"), "level spawn point"));
  ASSIGN_OR_RETURN(level.world_layers,
                   Required<std::vector<std::string>>(json, "world_layers", kContext));
  ASSIGN_OR_RETURN(level.gameplay_layer, Required<std::string>(json, "gameplay_layer", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json zones, Required<nlohmann::json>(json, "zones", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json entities,
                   Required<nlohmann::json>(json, "entities", kContext));
  ASSIGN_OR_RETURN(const nlohmann::json rectangles,
                   Required<nlohmann::json>(json, "terrain_rectangles", kContext));
  if (!zones.is_array() || !entities.is_array() || !rectangles.is_array()) {
    return absl::InvalidArgumentError(
        "environment zones, entities, and terrain rectangles must be arrays");
  }
  for (const nlohmann::json& item : zones) {
    ASSIGN_OR_RETURN(EnvironmentZoneSpec zone, ZoneFromJson(item));
    level.zones.push_back(std::move(zone));
  }
  for (const nlohmann::json& item : entities) {
    ASSIGN_OR_RETURN(EnvironmentEntitySpec entity, EntityFromJson(item));
    level.entities.push_back(std::move(entity));
  }
  for (const nlohmann::json& item : rectangles) {
    ASSIGN_OR_RETURN(EnvironmentTerrainRectangle rectangle, RectangleFromJson(item));
    level.terrain_rectangles.push_back(rectangle);
  }
  return level;
}

absl::Status ValidateSpec(const EnvironmentBuildSpec& spec) {
  if (spec.theme.name.empty() || spec.level.name.empty() || spec.level.tileset_name.empty() ||
      spec.level.terrain_name.empty()) {
    return absl::InvalidArgumentError("environment resource names must be non-empty");
  }
  if (spec.theme.layers.empty()) {
    return absl::InvalidArgumentError("environment theme needs at least one parallax layer");
  }
  for (const EnvironmentParallaxLayerSpec& layer : spec.theme.layers) {
    if (layer.name.empty() || layer.elements.empty() || layer.repeat_period.x < 0.0 ||
        layer.repeat_period.y < 0.0) {
      return absl::InvalidArgumentError("environment parallax layers must be complete");
    }
    for (const EnvironmentElementSpec& element : layer.elements) {
      if (element.name.empty() || element.artwork_recipe_name.empty() || element.scale <= 0.0f ||
          !std::isfinite(element.scale)) {
        return absl::InvalidArgumentError("environment parallax elements must be complete");
      }
    }
  }
  if (spec.level.tile_render_width <= 0 || spec.level.tile_render_height <= 0 ||
      spec.level.columns <= 0 || spec.level.rows <= 0) {
    return absl::InvalidArgumentError("environment level dimensions must be positive");
  }
  if (spec.level.world_layers.empty() || spec.level.gameplay_layer.empty()) {
    return absl::InvalidArgumentError("environment level needs a gameplay world layer");
  }
  std::set<std::string> layer_names;
  for (const std::string& name : spec.level.world_layers) {
    if (name.empty() || !layer_names.insert(name).second) {
      return absl::InvalidArgumentError("environment world layer names must be unique and set");
    }
  }
  if (!layer_names.contains(spec.level.gameplay_layer)) {
    return absl::InvalidArgumentError("environment gameplay layer is not in world_layers");
  }
  std::set<uint64_t> entity_ids;
  const double level_width = static_cast<double>(spec.level.columns) * spec.level.tile_render_width;
  const double level_height = static_cast<double>(spec.level.rows) * spec.level.tile_render_height;
  for (const EnvironmentEntitySpec& entity : spec.level.entities) {
    if (entity.id == 0 || !entity_ids.insert(entity.id).second) {
      return absl::InvalidArgumentError("environment entity IDs must be unique and non-zero");
    }
    if (!layer_names.contains(entity.layer_name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("environment entity layer is not in world_layers: ", entity.layer_name));
    }
    if (entity.blueprint_name.empty() || entity.state_key.empty()) {
      return absl::InvalidArgumentError(
          "environment entity blueprint name and state key must be non-empty");
    }
    if (entity.position.x < 0.0 || entity.position.y < 0.0 || entity.position.x > level_width ||
        entity.position.y > level_height) {
      return absl::InvalidArgumentError("environment entity position is outside the level");
    }
  }
  for (const EnvironmentTerrainRectangle& rectangle : spec.level.terrain_rectangles) {
    if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width <= 0 || rectangle.height <= 0 ||
        rectangle.x + rectangle.width > spec.level.columns ||
        rectangle.y + rectangle.height > spec.level.rows) {
      return absl::InvalidArgumentError("environment terrain rectangle is outside the level");
    }
  }
  return absl::OkStatus();
}

template <typename Resource>
absl::StatusOr<std::optional<Resource>> FindUniqueByName(const std::vector<Resource>& resources,
                                                         std::string_view name,
                                                         std::string_view kind) {
  std::optional<Resource> match;
  for (const Resource& resource : resources) {
    if (resource.name != name) continue;
    if (match.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("more than one ", kind, " is named '", name, "'"));
    }
    match = resource;
  }
  return match;
}

absl::StatusOr<std::string> ResolveArtworkTexture(const std::vector<ParallaxArtworkRecipe>& recipes,
                                                  std::string_view name) {
  ASSIGN_OR_RETURN(std::optional<ParallaxArtworkRecipe> recipe,
                   FindUniqueByName(recipes, name, "parallax artwork recipe"));
  if (!recipe.has_value()) {
    return absl::NotFoundError(absl::StrCat("parallax artwork recipe not found: ", name));
  }
  if (recipe->texture_id.empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("parallax artwork recipe has no texture: ", name));
  }
  return recipe->texture_id;
}

struct ResolvedEnvironmentEntity {
  EnvironmentEntitySpec placement;
  Blueprint blueprint;
};

absl::StatusOr<std::vector<ResolvedEnvironmentEntity>> ResolveEntityPlacements(
    Api& api, const EnvironmentLevelSpec& spec) {
  const std::vector<Blueprint> blueprints = api.GetAllBlueprints();
  std::vector<ResolvedEnvironmentEntity> resolved;
  resolved.reserve(spec.entities.size());
  for (const EnvironmentEntitySpec& placement : spec.entities) {
    ASSIGN_OR_RETURN(std::optional<Blueprint> blueprint,
                     FindUniqueByName(blueprints, placement.blueprint_name, "blueprint"));
    if (!blueprint.has_value()) {
      return absl::NotFoundError(
          absl::StrCat("environment entity blueprint not found: ", placement.blueprint_name));
    }
    if (!blueprint->state_index(placement.state_key).has_value()) {
      return absl::NotFoundError(
          absl::StrCat("blueprint state not found: ", blueprint->name, " / ", placement.state_key));
    }
    resolved.push_back({
        .placement = placement,
        .blueprint = std::move(*blueprint),
    });
  }
  return resolved;
}

absl::Status PlaceEntities(const std::vector<ResolvedEnvironmentEntity>& placements, Level& level) {
  for (const ResolvedEnvironmentEntity& resolved : placements) {
    const EnvironmentEntitySpec& placement = resolved.placement;
    const auto layer = std::find_if(level.layers.begin(), level.layers.end(),
                                    [&placement](const WorldLayer& candidate) {
                                      return candidate.name == placement.layer_name;
                                    });
    if (layer == level.layers.end()) {
      return absl::FailedPreconditionError(
          absl::StrCat("validated environment entity layer is missing: ", placement.layer_name));
    }
    ASSIGN_OR_RETURN(Entity entity,
                     CreateEntityFromBlueprint(resolved.blueprint, placement.state_key,
                                               placement.position, placement.id));
    entity.active = placement.active;
    entity.sort_order = placement.sort_order;
    RETURN_IF_ERROR(level.AddEntity(layer->id, std::move(entity)));
  }
  return absl::OkStatus();
}

absl::StatusOr<ParallaxTheme> ResolveTheme(Api& api, const EnvironmentThemeSpec& spec) {
  const std::vector<ParallaxArtworkRecipe> recipes = api.GetAllParallaxArtworkRecipes();
  ParallaxTheme theme{.name = spec.name};
  for (const EnvironmentParallaxLayerSpec& layer_spec : spec.layers) {
    ParallaxLayer layer{
        .name = layer_spec.name,
        .scroll_factor = layer_spec.scroll_factor,
        .offset = layer_spec.offset,
        .repeat_period = layer_spec.repeat_period,
    };
    for (size_t index = 0; index < layer_spec.elements.size(); ++index) {
      const EnvironmentElementSpec& element = layer_spec.elements[index];
      ASSIGN_OR_RETURN(const std::string texture_id,
                       ResolveArtworkTexture(recipes, element.artwork_recipe_name));
      layer.elements.push_back({
          .id = static_cast<int>(index),
          .name = element.name,
          .texture_id = texture_id,
          .position = element.position,
          .scale = element.scale,
      });
    }
    theme.layers.push_back(std::move(layer));
  }
  ParallaxTheme validation = theme;
  validation.id = "environment-build-preview";
  RETURN_IF_ERROR(ValidateParallaxTheme(validation));
  return theme;
}

absl::StatusOr<std::string> UpsertTheme(Api& api, ParallaxTheme theme) {
  ASSIGN_OR_RETURN(std::optional<ParallaxTheme> existing,
                   FindUniqueByName(api.GetAllParallaxThemes(), theme.name, "parallax theme"));
  if (!existing.has_value()) return api.CreateParallaxTheme(std::move(theme));
  theme.id = existing->id;
  RETURN_IF_ERROR(api.UpdateParallaxTheme(std::move(theme)));
  return existing->id;
}

class SolidField {
 public:
  SolidField(int columns, int rows)
      : columns_(columns), rows_(rows), cells_(static_cast<size_t>(columns) * rows, false) {}

  void Fill(const EnvironmentTerrainRectangle& rectangle) {
    for (int y = rectangle.y; y < rectangle.y + rectangle.height; ++y) {
      for (int x = rectangle.x; x < rectangle.x + rectangle.width; ++x) {
        cells_[static_cast<size_t>(y) * columns_ + x] = rectangle.solid;
      }
    }
  }

  bool At(int x, int y) const { return cells_[static_cast<size_t>(y) * columns_ + x]; }

 private:
  int columns_ = 0;
  int rows_ = 0;
  std::vector<bool> cells_;
};

absl::StatusOr<Tileset*> ResolveTileset(Api& api, const EnvironmentLevelSpec& spec) {
  ASSIGN_OR_RETURN(std::optional<Tileset> selected,
                   FindUniqueByName(api.GetAllTilesets(), spec.tileset_name, "tileset"));
  if (!selected.has_value()) {
    return absl::NotFoundError(absl::StrCat("tileset not found: ", spec.tileset_name));
  }
  ASSIGN_OR_RETURN(Tileset * loaded, api.GetTileset(selected->id));
  if (loaded == nullptr) return absl::FailedPreconditionError("tileset lookup returned null");
  return loaded;
}

absl::StatusOr<int> ResolveTerrainId(const Tileset& tileset, std::string_view name) {
  std::optional<int> id;
  for (const Terrain& terrain : tileset.terrains) {
    if (terrain.name != name) continue;
    if (id.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("terrain name is not unique in tileset: ", name));
    }
    id = terrain.id;
  }
  if (!id.has_value()) return absl::NotFoundError(absl::StrCat("terrain not found: ", name));
  return *id;
}

absl::Status PaintTerrainRectangles(Api& api, const EnvironmentLevelSpec& spec, Tileset& tileset,
                                    Level& level, int gameplay_layer_id) {
  ASSIGN_OR_RETURN(const int terrain_id, ResolveTerrainId(tileset, spec.terrain_name));
  DerivedTerrainSession derived;
  RETURN_IF_ERROR(derived.OpenFor(api, tileset));
  ASSIGN_OR_RETURN(TerrainIndex index, TerrainIndex::Build(tileset));
  std::optional<Blob47TileProvider> authored;
  TerrainTileProvider* provider = derived.provider();
  if (provider == nullptr) {
    authored.emplace(index);
    provider = &*authored;
  }
  SolidField field(spec.columns, spec.rows);
  for (const EnvironmentTerrainRectangle& rectangle : spec.terrain_rectangles) {
    field.Fill(rectangle);
  }
  WorldLayer* gameplay = FindWorldLayer(level, gameplay_layer_id);
  if (gameplay == nullptr) {
    return absl::FailedPreconditionError("environment level has no gameplay layer");
  }
  for (int y = 0; y < spec.rows; ++y) {
    for (int x = 0; x < spec.columns; ++x) {
      if (!field.At(x, y)) continue;
      RETURN_IF_ERROR(
          PaintTerrain(level, *gameplay, index, *provider, terrain_id, kFullBlock, x, y));
    }
  }
  return derived.Commit(api);
}

absl::StatusOr<std::string> ResolveZoneThemeId(std::string_view name,
                                               std::string_view built_theme_name,
                                               std::string_view built_theme_id,
                                               const std::vector<ParallaxTheme>& themes) {
  if (name == built_theme_name) return std::string(built_theme_id);
  ASSIGN_OR_RETURN(std::optional<ParallaxTheme> theme,
                   FindUniqueByName(themes, name, "parallax theme"));
  if (!theme.has_value()) {
    return absl::NotFoundError(absl::StrCat("zone parallax theme not found: ", name));
  }
  return theme->id;
}

absl::StatusOr<std::string> BuildAndUpsertLevel(
    Api& api, const EnvironmentBuildSpec& spec, std::string_view built_theme_id,
    const std::vector<ResolvedEnvironmentEntity>& entities) {
  ASSIGN_OR_RETURN(Tileset * tileset, ResolveTileset(api, spec.level));
  const std::vector<ParallaxTheme> themes = api.GetAllParallaxThemes();
  Level level{
      .name = spec.level.name,
      .tileset_id = tileset->id,
      .tile_render_width = spec.level.tile_render_width,
      .tile_render_height = spec.level.tile_render_height,
      .width = static_cast<double>(spec.level.columns) * spec.level.tile_render_width,
      .height = static_cast<double>(spec.level.rows) * spec.level.tile_render_height,
      .spawn_point = spec.level.spawn_point,
      .layers = {},
      .zones = {},
  };
  int gameplay_layer_id = -1;
  for (size_t index = 0; index < spec.level.world_layers.size(); ++index) {
    const std::string& name = spec.level.world_layers[index];
    level.layers.push_back({.id = static_cast<int>(index), .name = name});
    if (name == spec.level.gameplay_layer) gameplay_layer_id = static_cast<int>(index);
  }
  for (size_t index = 0; index < spec.level.zones.size(); ++index) {
    const EnvironmentZoneSpec& zone = spec.level.zones[index];
    ASSIGN_OR_RETURN(const std::string theme_id,
                     ResolveZoneThemeId(zone.theme_name, spec.theme.name, built_theme_id, themes));
    level.zones.push_back({
        .id = static_cast<int>(index),
        .name = zone.name,
        .theme_id = theme_id,
        .min_point = zone.min_point,
        .max_point = zone.max_point,
        .fade_length = zone.fade_length,
    });
  }
  RETURN_IF_ERROR(PlaceEntities(entities, level));
  Level validation = level;
  validation.id = "environment-build-preview";
  RETURN_IF_ERROR(ValidateLevel(validation));
  RETURN_IF_ERROR(PaintTerrainRectangles(api, spec.level, *tileset, level, gameplay_layer_id));
  validation = level;
  validation.id = "environment-build-preview";
  RETURN_IF_ERROR(ValidateLevel(validation));
  ASSIGN_OR_RETURN(std::optional<Level> existing,
                   FindUniqueByName(api.GetAllLevels(), level.name, "level"));
  if (!existing.has_value()) return api.CreateLevel(std::move(level));
  level.id = existing->id;
  RETURN_IF_ERROR(api.UpdateLevel(std::move(level)));
  return existing->id;
}

}  // namespace

absl::StatusOr<EnvironmentBuildSpec> EnvironmentBuildSpecFromJson(const nlohmann::json& json) {
  constexpr std::string_view kContext = "environment build spec";
  RETURN_IF_ERROR(RequireExactObject(json, {"schema_version", "theme", "level"}, kContext));
  ASSIGN_OR_RETURN(const int schema, Required<int>(json, "schema_version", kContext));
  if (schema != kEnvironmentBuildSpecSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat("environment build schema version ", schema,
                                                      " is not supported version ",
                                                      kEnvironmentBuildSpecSchemaVersion));
  }
  EnvironmentBuildSpec spec;
  ASSIGN_OR_RETURN(spec.theme, ThemeFromJson(json.at("theme")));
  ASSIGN_OR_RETURN(spec.level, LevelFromJson(json.at("level")));
  RETURN_IF_ERROR(ValidateSpec(spec));
  return spec;
}

absl::StatusOr<EnvironmentBuildSpec> ReadEnvironmentBuildSpec(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open environment spec: ", path.string()));
  }
  nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
  if (json.is_discarded()) {
    return absl::InvalidArgumentError(
        absl::StrCat("environment spec is not valid JSON: ", path.string()));
  }
  return EnvironmentBuildSpecFromJson(json);
}

absl::StatusOr<EnvironmentBuildResult> BuildEnvironment(Api& api,
                                                        const EnvironmentBuildSpec& spec) {
  RETURN_IF_ERROR(ValidateSpec(spec));
  ASSIGN_OR_RETURN(const std::vector<ResolvedEnvironmentEntity> entities,
                   ResolveEntityPlacements(api, spec.level));
  ASSIGN_OR_RETURN(ParallaxTheme theme, ResolveTheme(api, spec.theme));
  ASSIGN_OR_RETURN(const std::string theme_id, UpsertTheme(api, std::move(theme)));
  ASSIGN_OR_RETURN(const std::string level_id, BuildAndUpsertLevel(api, spec, theme_id, entities));
  return EnvironmentBuildResult{.theme_id = theme_id, .level_id = level_id};
}

}  // namespace zebes
