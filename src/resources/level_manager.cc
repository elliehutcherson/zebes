#include "resources/level_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
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
      {"texture_id", layer.texture_id},
      {"scroll_factor_x", layer.scroll_factor.x},
      {"scroll_factor_y", layer.scroll_factor.y},
      {"repeat_x", layer.repeat_x},
  };
}

void FromJson(const nlohmann::json& j, ParallaxLayer& layer) {
  j.at("texture_id").get_to(layer.texture_id);
  j.at("scroll_factor_x").get_to(layer.scroll_factor.x);
  j.at("scroll_factor_y").get_to(layer.scroll_factor.y);
  layer.repeat_x = j.value("repeat_x", false);
}

void ToJson(nlohmann::json& j, const Entity& entity) {
  j = nlohmann::json{
      {"id", entity.id},
      {"active", entity.active},
      {"transform",
       {
           {"x", entity.transform.position.x},
           {"y", entity.transform.position.y},
           {"rotation", entity.transform.rotation},
       }},
      {"current_frame_index", entity.current_frame_index},
      {"body",
       {
           {"vx", entity.body.velocity.x},
           {"vy", entity.body.velocity.y},
           {"ax", entity.body.acceleration.x},
           {"ay", entity.body.acceleration.y},
           {"is_static", entity.body.is_static},
           {"mass", entity.body.mass},
       }},
  };
  if (entity.sprite) {
    j["sprite_id"] = entity.sprite->id;
  }
  if (entity.collider) {
    j["collider_id"] = entity.collider->id;
  }
}

absl::Status FromJson(const nlohmann::json& j, Entity& entity, SpriteManager& sm,
                      ColliderManager& cm) {
  j.at("id").get_to(entity.id);
  entity.active = j.value("active", true);

  if (j.contains("transform")) {
    auto& t = j["transform"];
    entity.transform.position.x = t.value("x", 0.0);
    entity.transform.position.y = t.value("y", 0.0);
    entity.transform.rotation = t.value("rotation", 0.0);
  }

  entity.current_frame_index = j.value("current_frame_index", 0);

  if (j.contains("body")) {
    auto& b = j["body"];
    entity.body.velocity.x = b.value("vx", 0.0);
    entity.body.velocity.y = b.value("vy", 0.0);
    entity.body.acceleration.x = b.value("ax", 0.0);
    entity.body.acceleration.y = b.value("ay", 0.0);
    entity.body.is_static = b.value("is_static", false);
    entity.body.mass = b.value("mass", 0.0);
  }

  if (j.contains("sprite_id")) {
    std::string sprite_id = j["sprite_id"];
    ASSIGN_OR_RETURN(entity.sprite, sm.GetSprite(sprite_id));
  }

  if (j.contains("collider_id")) {
    std::string collider_id = j["collider_id"];
    ASSIGN_OR_RETURN(entity.collider, cm.GetCollider(collider_id));
  }
  return absl::OkStatus();
}

nlohmann::json ToJson(const Level& level) {
  nlohmann::json j;
  j["id"] = level.id;
  j["name"] = level.name;
  j["width"] = level.width;
  j["height"] = level.height;
  j["spawn_point"] = {{"x", level.spawn_point.x}, {"y", level.spawn_point.y}};

  // Parallax
  std::vector<nlohmann::json> parallax_json;
  for (const ParallaxLayer& layer : level.parallax_layers) {
    nlohmann::json layer_j;
    ToJson(layer_j, layer);
    parallax_json.push_back(layer_j);
  }
  j["parallax_layers"] = parallax_json;

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
  for (const std::unique_ptr<Entity>& entity : level.entities) {
    nlohmann::json entity_j;
    ToJson(entity_j, *entity);
    entities_json.push_back(entity_j);
  }
  j["entities"] = entities_json;

  return j;
}

absl::StatusOr<Level> GetLevelFromJson(const nlohmann::json& j, SpriteManager& sm,
                                       ColliderManager& cm) {
  Level level;
  j.at("id").get_to(level.id);
  j.at("name").get_to(level.name);
  level.width = j.value("width", 0.0);
  level.height = j.value("height", 0.0);
  if (j.contains("spawn_point")) {
    level.spawn_point.x = j["spawn_point"].value("x", 0.0);
    level.spawn_point.y = j["spawn_point"].value("y", 0.0);
  }

  // Validation
  constexpr int kTileSize = 16;
  if (static_cast<int>(level.width) % kTileSize != 0 ||
      static_cast<int>(level.height) % kTileSize != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Level boundaries must be multiples of tile size (", kTileSize, ")"));
  }

  if (j.contains("parallax_layers")) {
    for (const nlohmann::json& item : j["parallax_layers"]) {
      ParallaxLayer layer;
      FromJson(item, layer);
      level.parallax_layers.push_back(layer);
    }
  }

  if (j.contains("tile_chunks")) {
    for (const nlohmann::json& item : j["tile_chunks"]) {
      int64_t id = item["chunk_id"];
      TileChunk chunk;
      FromJson(item, chunk);
      level.tile_chunks[id] = chunk;
    }
  }

  if (j.contains("entities")) {
    for (const nlohmann::json& item : j["entities"]) {
      auto entity = std::make_unique<Entity>();
      RETURN_IF_ERROR(FromJson(item, *entity, sm, cm));
      level.AddEntity(std::move(entity));
    }
  }

  return level;
}

}  // namespace

absl::StatusOr<std::unique_ptr<LevelManager>> LevelManager::Create(SpriteManager* sm,
                                                                   ColliderManager* cm,
                                                                   std::string root_path) {
  if (sm == nullptr || cm == nullptr) {
    return absl::InvalidArgumentError("Dependencies must not be null");
  }
  return std::unique_ptr<LevelManager>(new LevelManager(*sm, *cm, root_path));
}

LevelManager::LevelManager(SpriteManager& sm, ColliderManager& cm, std::string root_path)
    : root_path_(root_path),
      definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)),
      sm_(sm),
      cm_(cm) {}

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

  ASSIGN_OR_RETURN(Level level, GetLevelFromJson(json, sm_, cm_));

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

  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadLevel(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load level from " << entry.path() << ": " << status;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> LevelManager::CreateLevel(Level level) {
  // FORCE ID GENERATION
  level.id = GenerateGuid();

  // Save the level (requires copy because SaveLevel takes const ref but here we have an object we
  // want to save) Actually, SaveLevel handles serialization. We should save the level we just
  // modified. BUT we want to reload it?

  // Wait, if we use SaveLevel which takes `const Level&`, we can just pass `level`.
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

  // Update cache by re-parsing the json effectively (or just using LoadLevel logic manually)
  // We can't move 'level' because it is const ref.
  // We CANNOT copy 'level' because of unique_ptrs.
  // We MUST reconstruct it.
  // Since we already have the json, we can `GetLevelFromJson`.
  ASSIGN_OR_RETURN(Level new_level_obj, GetLevelFromJson(json, sm_, cm_));
  levels_[level.id] = std::make_unique<Level>(std::move(new_level_obj));

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
    levels.push_back(level_ptr->GetCopy());
  }
  return levels;
}

}  // namespace zebes
