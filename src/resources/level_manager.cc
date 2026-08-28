#include "resources/level_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

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

void ToJson(nlohmann::json& j, const TileChunk& chunk) { j["tiles"] = chunk.tiles; }

void FromJson(const nlohmann::json& j, TileChunk& chunk) { j.at("tiles").get_to(chunk.tiles); }

void ToJson(nlohmann::json& j, const Entity& entity);

void ToJson(nlohmann::json& j, const WorldLayer& layer) {
  j["id"] = layer.id;
  j["name"] = layer.name;

  std::vector<nlohmann::json> chunks_json;
  std::vector<int64_t> chunk_ids;
  chunk_ids.reserve(layer.tile_chunks.size());
  for (const auto& [id, unused] : layer.tile_chunks) {
    static_cast<void>(unused);
    chunk_ids.push_back(id);
  }
  std::sort(chunk_ids.begin(), chunk_ids.end());
  for (const int64_t id : chunk_ids) {
    nlohmann::json chunk_j;
    chunk_j["chunk_id"] = id;
    ToJson(chunk_j, layer.tile_chunks.at(id));
    chunks_json.push_back(std::move(chunk_j));
  }
  j["tile_chunks"] = std::move(chunks_json);

  std::vector<nlohmann::json> entities_json;
  for (const auto& [id, entity] : layer.entities) {
    nlohmann::json entity_j;
    ToJson(entity_j, entity);
    entities_json.push_back(std::move(entity_j));
  }
  j["entities"] = std::move(entities_json);
}

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

absl::StatusOr<WorldLayer> WorldLayerFromJson(const nlohmann::json& j) {
  WorldLayer layer;
  j.at("id").get_to(layer.id);
  j.at("name").get_to(layer.name);

  for (const nlohmann::json& item : j.at("tile_chunks")) {
    const int64_t chunk_id = item.at("chunk_id").get<int64_t>();
    TileChunk chunk;
    FromJson(item, chunk);
    if (!layer.tile_chunks.emplace(chunk_id, std::move(chunk)).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate tile chunk ID in world layer ", layer.id, ": ", chunk_id));
    }
  }

  for (const nlohmann::json& item : j.at("entities")) {
    Entity entity;
    RETURN_IF_ERROR(FromJson(item, entity));
    const uint64_t entity_id = entity.id;
    if (!layer.entities.emplace(entity_id, std::move(entity)).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate entity ID in world layer ", layer.id, ": ", entity_id));
    }
  }
  return layer;
}

nlohmann::json SerializeLevel(const Level& level) {
  nlohmann::json j;
  j["id"] = level.id;
  j["name"] = level.name;
  j["tileset_id"] = level.tileset_id;
  j["width"] = level.width;
  j["height"] = level.height;
  j["tile_render_width"] = level.tile_render_width;
  j["tile_render_height"] = level.tile_render_height;
  j["spawn_point"] = {{"x", level.spawn_point.x}, {"y", level.spawn_point.y}};

  // Zones and layers are written even when empty; see the note on
  // sprite_id above for why an absent key is not an option.
  std::vector<nlohmann::json> zones_json;
  for (const auto& zone : level.zones) {
    nlohmann::json zone_j;
    ToJson(zone_j, zone);
    zones_json.push_back(zone_j);
  }
  j["zones"] = zones_json;

  std::vector<nlohmann::json> layers_json;
  layers_json.reserve(level.layers.size());
  for (const WorldLayer& layer : level.layers) {
    nlohmann::json layer_j;
    ToJson(layer_j, layer);
    layers_json.push_back(std::move(layer_j));
  }
  j["layers"] = std::move(layers_json);

  return j;
}

// Reads a level document, assuming every field the writer emits is present.
// Throws nlohmann::json::exception when one is not; GetLevelFromJson turns that
// into a Status so a stale definition names the missing field instead of
// terminating the editor.
absl::StatusOr<Level> ParseLevel(const nlohmann::json& j) {
  if (j.contains("themes")) {
    return absl::FailedPreconditionError(
        "Level still contains embedded parallax themes; run scripts/migrate_definitions.py.");
  }
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

  {
    for (const auto& item : j.at("zones")) {
      ParallaxZone zone;
      try {
        FromJson(item, zone);
      } catch (const nlohmann::json::exception& e) {
        return absl::InvalidArgumentError(absl::StrCat("Failed to parse zone: ", e.what()));
      }
      level.zones.push_back(zone);
    }
  }

  level.layers.clear();
  for (const nlohmann::json& item : j.at("layers")) {
    ASSIGN_OR_RETURN(WorldLayer layer, WorldLayerFromJson(item));
    level.layers.push_back(std::move(layer));
  }

  RETURN_IF_ERROR(ValidateLevel(level));
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

nlohmann::json LevelToJson(const Level& level) { return SerializeLevel(level); }

absl::StatusOr<std::unique_ptr<LevelManager>> LevelManager::Create(std::string root_path) {
  return std::unique_ptr<LevelManager>(new LevelManager(root_path));
}

LevelManager::LevelManager(std::string root_path)
    : root_path_(root_path), definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

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
  return LoadJsonDefinitions(definitions_path_, "level",
                             [this](const std::filesystem::path& path) -> absl::Status {
                               return LoadLevel(path.filename().string()).status();
                             });
}

absl::StatusOr<std::string> LevelManager::CreateLevel(Level level) {
  if (level.name.empty()) {
    return absl::InvalidArgumentError("Levels must have a non-empty name.");
  }

  // Names appear in filenames, so duplicates are rejected rather than
  // silently disambiguated.
  for (const auto& [id, existing_level] : levels_) {
    if (existing_level->name == level.name) {
      return absl::InvalidArgumentError(
          absl::StrCat("Level name '", level.name, "' already exists."));
    }
  }

  // IDs are the manager's to assign; a caller-supplied one is discarded.
  level.id = GenerateGuid();

  RETURN_IF_ERROR(SaveLevel(level));

  // Reloaded from disk, so the cached level is what a fresh start loads.
  std::string filename = absl::StrCat(level.name, "-", level.id, ".json");
  ASSIGN_OR_RETURN(Level * loaded_level, LoadLevel(filename));

  return loaded_level->id;
}

absl::Status LevelManager::SaveLevel(const Level& level) {
  RETURN_IF_ERROR(ValidateLevel(level));

  // Validate level name uniqueness across the catalog. Intrinsic definition
  // invariants are handled by ValidateLevel above for both load and save.
  for (const auto& [id, existing_level] : levels_) {
    if (id != level.id && existing_level->name == level.name) {
      return absl::InvalidArgumentError(
          absl::StrCat("Level name '", level.name, "' is already taken by another level."));
    }
  }

  nlohmann::json json = LevelToJson(level);

  // A rename changes the filename, so the old file has to go or the catalog
  // loads the level twice under two names.
  auto it = levels_.find(level.id);
  if (it != levels_.end()) {
    RemoveOldFileIfExists(level.id, it->second->name, level.name, definitions_path_);
  }

  std::string filename = absl::StrCat(level.name, "-", level.id, ".json");
  std::string full_path = GetDefinitionsPath(filename);

  std::filesystem::create_directories(definitions_path_);

  std::ofstream file(full_path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file for writing: ", full_path));
  }
  file << json.dump(4);

  // Assigned through the existing allocation rather than replacing it: the
  // level editor holds the Level* it is editing for the whole session, and
  // swapping the unique_ptr frees it mid-edit. The pointer indirection exists
  // so an address survives a save.
  if (auto it = levels_.find(level.id); it != levels_.end()) {
    *it->second = level;
    return absl::OkStatus();
  }
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
