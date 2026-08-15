#include "resources/level_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "objects/level.h"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/levels";

// Helper for TileChunk
void ToJson(nlohmann::json& j, const TileChunk& chunk) { j["tiles"] = chunk.tiles; }

void FromJson(const nlohmann::json& j, TileChunk& chunk) { j.at("tiles").get_to(chunk.tiles); }

// Helper for ParallaxLayer
void ToJson(nlohmann::json& j, const ParallaxLayer& layer) {
  j = nlohmann::json{
      {"name", layer.name},
      {"texture_id", layer.texture_id},
      {"scroll_factor_x", layer.scroll_factor.x},
      {"scroll_factor_y", layer.scroll_factor.y},
      {"offset_x", layer.offset.x},
      {"offset_y", layer.offset.y},
      {"repeat_x", layer.repeat_x},
      {"repeat_y", layer.repeat_y},
      {"base_scale", layer.base_scale},
  };
}

void FromJson(const nlohmann::json& j, ParallaxLayer& layer) {
  j.at("name").get_to(layer.name);
  j.at("texture_id").get_to(layer.texture_id);
  j.at("scroll_factor_x").get_to(layer.scroll_factor.x);
  j.at("scroll_factor_y").get_to(layer.scroll_factor.y);
  j.at("offset_x").get_to(layer.offset.x);
  j.at("offset_y").get_to(layer.offset.y);
  j.at("repeat_x").get_to(layer.repeat_x);
  j.at("repeat_y").get_to(layer.repeat_y);
  j.at("base_scale").get_to(layer.base_scale);
}

// Helper for ParallaxTheme
void ToJson(nlohmann::json& j, const ParallaxTheme& theme) {
  j["id"] = theme.id;
  j["name"] = theme.name;
  std::vector<nlohmann::json> layers_json;
  for (const auto& layer : theme.layers) {
    nlohmann::json layer_j;
    ToJson(layer_j, layer);
    layers_json.push_back(layer_j);
  }
  j["layers"] = layers_json;
}

void FromJson(const nlohmann::json& j, ParallaxTheme& theme) {
  j.at("id").get_to(theme.id);
  j.at("name").get_to(theme.name);
  for (const auto& item : j.at("layers")) {
    ParallaxLayer layer;
    FromJson(item, layer);
    theme.layers.push_back(layer);
  }
}

// Helper for ParallaxZone
void ToJson(nlohmann::json& j, const ParallaxZone& zone) {
  j = nlohmann::json{
      {"id", zone.id},
      {"name", zone.name},
      {"theme_id", zone.theme_id},
      {"min_x", zone.min_point.x},
      {"min_y", zone.min_point.y},
      {"max_x", zone.max_point.x},
      {"max_y", zone.max_point.y},
      {"fade_x", zone.fade_length.x},
      {"fade_y", zone.fade_length.y},
  };
}

void FromJson(const nlohmann::json& j, ParallaxZone& zone) {
  j.at("id").get_to(zone.id);
  j.at("name").get_to(zone.name);
  j.at("theme_id").get_to(zone.theme_id);
  j.at("min_x").get_to(zone.min_point.x);
  j.at("min_y").get_to(zone.min_point.y);
  j.at("max_x").get_to(zone.max_point.x);
  j.at("max_y").get_to(zone.max_point.y);
  j.at("fade_x").get_to(zone.fade_length.x);
  j.at("fade_y").get_to(zone.fade_length.y);
}

void ToJson(nlohmann::json& j, const Entity& entity) {
  j = nlohmann::json{
      {"id", entity.id},
      {"active", entity.active},
      {"blueprint_id", entity.blueprint_id},
      {"blueprint_state_index", entity.blueprint_state_index},
      {"sort_order", entity.sort_order},
      {"transform",
       {
           {"x", entity.transform.position.x},
           {"y", entity.transform.position.y},
           {"rotation", entity.transform.rotation},
       }},
      // Only authored properties are persisted. Velocity, acceleration, and
      // animation playback are simulation state and deliberately never written.
      {"body",
       {
           {"drag_x", entity.body.drag.x},
           {"drag_y", entity.body.drag.y},
           {"is_static", entity.body.is_static},
           {"mass", entity.body.mass},
       }},
  };
  // Written even when empty. An unbound reference is a state the level means to
  // record, not one to leave the reader inferring from an absent key.
  j["sprite_id"] = entity.sprite_id;
  j["collider_id"] = entity.collider_id;
}

absl::Status FromJson(const nlohmann::json& j, Entity& entity) {
  j.at("id").get_to(entity.id);
  j.at("active").get_to(entity.active);
  j.at("blueprint_id").get_to(entity.blueprint_id);
  j.at("blueprint_state_index").get_to(entity.blueprint_state_index);
  j.at("sort_order").get_to(entity.sort_order);

  const nlohmann::json& t = j.at("transform");
  t.at("x").get_to(entity.transform.position.x);
  t.at("y").get_to(entity.transform.position.y);
  t.at("rotation").get_to(entity.transform.rotation);

  // Levels written before the split carry current_frame_index alongside the
  // body's vx/vy/ax/ay. Those keys are simulation state; extra keys are ignored
  // here rather than rejected, since the writer stopped emitting them and
  // nothing reads them.
  const nlohmann::json& b = j.at("body");
  b.at("drag_x").get_to(entity.body.drag.x);
  b.at("drag_y").get_to(entity.body.drag.y);
  b.at("is_static").get_to(entity.body.is_static);
  b.at("mass").get_to(entity.body.mass);

  // Asset references are kept as IDs. Resolving them is the renderer's job, so
  // a level can be loaded without the sprite or collider managers.
  j.at("sprite_id").get_to(entity.sprite_id);
  j.at("collider_id").get_to(entity.collider_id);
  return absl::OkStatus();
}

nlohmann::json ToJson(const Level& level) {
  nlohmann::json j;
  j["id"] = level.id;
  j["name"] = level.name;
  j["tileset_id"] = level.tileset_id;
  j["width"] = level.width;
  j["height"] = level.height;
  j["tile_render_width"] = level.tile_render_width;
  j["tile_render_height"] = level.tile_render_height;
  j["spawn_point"] = {{"x", level.spawn_point.x}, {"y", level.spawn_point.y}};

  // Parallax
  std::vector<nlohmann::json> parallax_json;
  for (const ParallaxLayer& layer : level.parallax_layers) {
    nlohmann::json layer_j;
    ToJson(layer_j, layer);
    parallax_json.push_back(layer_j);
  }
  j["parallax_layers"] = parallax_json;

  // Themes
  std::vector<nlohmann::json> themes_json;
  for (const auto& [id, theme] : level.themes) {
    nlohmann::json theme_j;
    ToJson(theme_j, theme);
    themes_json.push_back(theme_j);
  }
  j["themes"] = themes_json;

  // Zones
  std::vector<nlohmann::json> zones_json;
  for (const auto& zone : level.zones) {
    nlohmann::json zone_j;
    ToJson(zone_j, zone);
    zones_json.push_back(zone_j);
  }
  j["zones"] = zones_json;

  // Tile Chunks
  std::vector<nlohmann::json> chunks_json;
  for (const auto& [id, chunk] : level.tile_chunks) {
    nlohmann::json chunk_j;
    chunk_j["chunk_id"] = id;
    ToJson(chunk_j, chunk);
    chunks_json.push_back(chunk_j);
  }
  j["tile_chunks"] = chunks_json;

  // Entities
  std::vector<nlohmann::json> entities_json;
  for (const auto& [id, entity] : level.entities) {
    nlohmann::json entity_j;
    ToJson(entity_j, entity);
    entities_json.push_back(entity_j);
  }
  j["entities"] = entities_json;

  return j;
}

// Reads a level document, assuming every field the writer emits is present.
// Throws nlohmann::json::exception when one is not; GetLevelFromJson turns that
// into a Status so a stale definition names the missing field instead of
// terminating the editor.
absl::StatusOr<Level> ParseLevel(const nlohmann::json& j) {
  Level level;
  j.at("id").get_to(level.id);
  j.at("name").get_to(level.name);
  j.at("width").get_to(level.width);
  j.at("height").get_to(level.height);
  j.at("tile_render_width").get_to(level.tile_render_width);
  j.at("tile_render_height").get_to(level.tile_render_height);
  j.at("tileset_id").get_to(level.tileset_id);
  j.at("spawn_point").at("x").get_to(level.spawn_point.x);
  j.at("spawn_point").at("y").get_to(level.spawn_point.y);

  // Validation
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("Tile render dimensions must be positive.");
  }
  if (static_cast<int>(level.width) % level.tile_render_width != 0 ||
      static_cast<int>(level.height) % level.tile_render_height != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Level boundaries must be multiples of tile render size (",
                     level.tile_render_width, " x ", level.tile_render_height, ")"));
  }

  {
    absl::flat_hash_set<std::string> existing_names;
    int index = 0;
    for (const nlohmann::json& item : j.at("parallax_layers")) {
      ParallaxLayer layer;
      FromJson(item, layer);

      if (layer.name.empty()) {
        layer.name = absl::StrCat("Layer ", index);
      }

      std::string unique_name = layer.name;
      int duplicate_count = 1;
      while (existing_names.contains(unique_name)) {
        unique_name = absl::StrCat(layer.name, " (", duplicate_count, ")");
        duplicate_count++;
      }
      layer.name = unique_name;
      existing_names.insert(layer.name);

      level.parallax_layers.push_back(layer);
      index++;
    }
  }

  {
    for (const auto& item : j.at("themes")) {
      ParallaxTheme theme;
      FromJson(item, theme);
      if (theme.id < 0) {
        return absl::InvalidArgumentError("Theme must have a valid non-negative integer ID.");
      }
      level.themes[theme.id] = theme;
    }
  }

  {
    absl::flat_hash_set<int> zone_ids;
    for (const auto& item : j.at("zones")) {
      ParallaxZone zone;
      try {
        FromJson(item, zone);
      } catch (const nlohmann::json::exception& e) {
        return absl::InvalidArgumentError(absl::StrCat("Failed to parse zone: ", e.what()));
      }
      if (zone.name.empty()) {
        return absl::InvalidArgumentError("Zone name cannot be empty.");
      }
      if (zone.id < 0) {
        return absl::InvalidArgumentError("Zone must have a valid non-negative integer ID.");
      }
      if (zone_ids.contains(zone.id)) {
        return absl::InvalidArgumentError(absl::StrCat("Duplicate zone ID found: '", zone.id, "'"));
      }
      zone_ids.insert(zone.id);
      level.zones.push_back(zone);
    }
  }

  // Validate zone boundaries fit within level.
  for (const ParallaxZone& zone : level.zones) {
    if (zone.min_point.x < 0 || zone.min_point.y < 0 || zone.max_point.x > level.width ||
        zone.max_point.y > level.height) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' extends outside level boundaries."));
    }
    if (zone.min_point.x >= zone.max_point.x || zone.min_point.y >= zone.max_point.y) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' has invalid dimensions (min >= max)."));
    }
  }

  // Validate spawn point is within level bounds.
  if (level.spawn_point.x < 0 || level.spawn_point.y < 0 || level.spawn_point.x > level.width ||
      level.spawn_point.y > level.height) {
    return absl::InvalidArgumentError("Spawn point is outside level boundaries.");
  }

  for (const nlohmann::json& item : j.at("tile_chunks")) {
    const int64_t id = item.at("chunk_id").get<int64_t>();
    TileChunk chunk;
    FromJson(item, chunk);
    level.tile_chunks[id] = chunk;
  }

  for (const nlohmann::json& item : j.at("entities")) {
    Entity entity;
    RETURN_IF_ERROR(FromJson(item, entity));
    level.AddEntity(std::move(entity));
  }

  return level;
}

absl::StatusOr<Level> GetLevelFromJson(const nlohmann::json& j) {
  try {
    return ParseLevel(j);
  } catch (const nlohmann::json::exception& e) {
    return absl::InvalidArgumentError(absl::StrCat("JSON parsing error for Level: ", e.what()));
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<LevelManager>> LevelManager::Create(std::string root_path) {
  return std::unique_ptr<LevelManager>(new LevelManager(root_path));
}

LevelManager::LevelManager(std::string root_path)
    : root_path_(root_path),
      definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

std::string LevelManager::GetDefinitionsPath(const std::string relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

absl::StatusOr<Level*> LevelManager::LoadLevel(const std::string& path_json) {
  const std::string full_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(full_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", full_path));
  }

  std::ifstream stream(full_path);
  nlohmann::json json;
  stream >> json;

  ASSIGN_OR_RETURN(Level level, GetLevelFromJson(json));

  std::string id = level.id;
  levels_[id] = std::make_unique<Level>(std::move(level));
  return levels_[id].get();
}

absl::Status LevelManager::LoadAllLevels() {
  if (!std::filesystem::exists(definitions_path_)) {
    // If directory doesn't exist, maybe just return OK or create it?
    // SpriteManager returns NotFound.
    return absl::NotFoundError(absl::StrCat("Level root directory not found: ", definitions_path_));
  }

  ResourceLoadFailures failures;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadLevel(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load level from " << entry.path() << ": " << status.status();
      failures.Add(entry.path().filename().string(), status.status());
    }
  }
  return failures.ToStatus("level");
}

absl::StatusOr<std::string> LevelManager::CreateLevel(Level level) {
  if (level.name.empty()) {
    return absl::InvalidArgumentError("Levels must have a non-empty name.");
  }

  // Check for uniqueness across ALL levels
  for (const auto& [id, existing_level] : levels_) {
    if (existing_level->name == level.name) {
      return absl::InvalidArgumentError(
          absl::StrCat("Level name '", level.name, "' already exists."));
    }
  }

  // FORCE ID GENERATION
  level.id = GenerateGuid();

  RETURN_IF_ERROR(SaveLevel(level));

  // Reload to ensure it's in memory properly
  std::string filename = absl::StrCat(level.name, "-", level.id, ".json");
  ASSIGN_OR_RETURN(Level * loaded_level, LoadLevel(filename));

  return loaded_level->id;
}

absl::Status LevelManager::SaveLevel(const Level& level) {
  if (level.id.empty()) {
    return absl::InvalidArgumentError("Level must have an ID to be saved.");
  }

  // 1. Validate Level Name
  if (level.name.empty()) {
    return absl::InvalidArgumentError("Level name cannot be empty.");
  }

  // 2. Validate Level Name Uniqueness
  for (const auto& [id, existing_level] : levels_) {
    if (id != level.id && existing_level->name == level.name) {
      return absl::InvalidArgumentError(
          absl::StrCat("Level name '", level.name, "' is already taken by another level."));
    }
  }

  // 3. Validate Parallax Layers
  absl::flat_hash_set<std::string> layer_names;
  for (const ParallaxLayer& layer : level.parallax_layers) {
    if (layer.name.empty()) {
      return absl::InvalidArgumentError("Parallax layer name cannot be empty.");
    }
    if (layer_names.contains(layer.name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate parallax layer name found: '", layer.name, "'"));
    }
    layer_names.insert(layer.name);
  }

  // 4. Validate Themes
  for (const auto& [id, theme] : level.themes) {
    if (theme.id < 0) {
      return absl::InvalidArgumentError("Theme must have a valid non-negative integer ID.");
    }
    if (theme.name.empty()) {
      return absl::InvalidArgumentError("Theme name cannot be empty.");
    }
    if (theme.id != id) {
      return absl::InvalidArgumentError(
          absl::StrCat("Theme map key '", id, "' does not match theme id '", theme.id, "'"));
    }
  }

  // 5. Validate Zones
  absl::flat_hash_set<int> zone_ids;
  for (const auto& zone : level.zones) {
    if (zone.name.empty()) {
      return absl::InvalidArgumentError("Zone name cannot be empty.");
    }
    if (zone.id < 0) {
      return absl::InvalidArgumentError("Zone must have a valid non-negative integer ID.");
    }
    if (zone_ids.contains(zone.id)) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate zone ID found: '", zone.id, "'"));
    }
    zone_ids.insert(zone.id);
    if (!level.themes.contains(zone.theme_id)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone references non-existent theme: '", zone.theme_id, "'"));
    }
    if (zone.min_point.x < 0 || zone.min_point.y < 0 || zone.max_point.x > level.width ||
        zone.max_point.y > level.height) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' extends outside level boundaries."));
    }
    if (zone.min_point.x >= zone.max_point.x || zone.min_point.y >= zone.max_point.y) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' has invalid dimensions (min >= max)."));
    }
  }

  // 6. Validate Spawn Point
  if (level.spawn_point.x < 0 || level.spawn_point.y < 0 || level.spawn_point.x > level.width ||
      level.spawn_point.y > level.height) {
    return absl::InvalidArgumentError("Spawn point is outside level boundaries.");
  }

  // 7. Validate Tile Render Dimensions
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("Tile render dimensions must be positive.");
  }
  if (static_cast<int>(level.width) % level.tile_render_width != 0 ||
      static_cast<int>(level.height) % level.tile_render_height != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Level boundaries must be multiples of tile render size (",
                     level.tile_render_width, " x ", level.tile_render_height, ")"));
  }

  nlohmann::json json = ToJson(level);

  // Handle renaming
  auto it = levels_.find(level.id);
  if (it != levels_.end()) {
    RemoveOldFileIfExists(level.id, it->second->name, level.name, definitions_path_);
  }

  std::string filename = absl::StrCat(level.name, "-", level.id, ".json");
  std::string full_path = GetDefinitionsPath(filename);

  // Ensure directory exists
  std::filesystem::create_directories(definitions_path_);

  std::ofstream file(full_path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file for writing: ", full_path));
  }
  file << json.dump(4);

  // Update cache with new level.
  levels_[level.id] = std::make_unique<Level>(level);

  return absl::OkStatus();
}

absl::StatusOr<Level*> LevelManager::GetLevel(const std::string& id) {
  auto it = levels_.find(id);
  if (it == levels_.end()) {
    return absl::NotFoundError(absl::StrCat("Level with id ", id, " not found."));
  }
  return it->second.get();
}

absl::Status LevelManager::DeleteLevel(const std::string& id) {
  auto it = levels_.find(id);
  if (it == levels_.end()) return absl::NotFoundError("Level not found");

  std::string filename = absl::StrCat(it->second->name, "-", id, ".json");
  std::filesystem::remove(GetDefinitionsPath(filename));

  levels_.erase(it);
  return absl::OkStatus();
}

std::vector<Level> LevelManager::GetAllLevels() const {
  std::vector<Level> levels;
  levels.reserve(levels_.size());
  for (const auto& [id, level_ptr] : levels_) {
    levels.push_back(*level_ptr);
  }
  return levels;
}

}  // namespace zebes
