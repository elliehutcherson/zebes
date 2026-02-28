#include "resources/tileset_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/tilesets";

void ToJson(nlohmann::json& j, const Tile& tile_def) {
  j = {
      {"id", tile_def.id},
      {"name", tile_def.name},
      {"source_x", tile_def.source_x},
      {"source_y", tile_def.source_y},
      {"shape", static_cast<int>(tile_def.shape)},
      {"is_one_way", tile_def.is_one_way},
      {"tags", tile_def.tags},
  };
}

absl::StatusOr<Tile> GetTileFromJson(const nlohmann::json& j) {
  Tile tile_def;
  try {
    j.at("id").get_to(tile_def.id);
    j.at("name").get_to(tile_def.name);
    tile_def.source_x = j.value("source_x", 0);
    tile_def.source_y = j.value("source_y", 0);
    tile_def.shape = static_cast<TileShape>(j.value("shape", 0));
    tile_def.is_one_way = j.value("is_one_way", false);
    if (j.contains("tags")) {
      j.at("tags").get_to(tile_def.tags);
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(
        absl::StrCat("JSON parsing error for Tile: ", e.what()));
  }
  return tile_def;
}

nlohmann::json ToJson(const Tileset& tileset) {
  nlohmann::json j;
  j["id"] = tileset.id;
  j["name"] = tileset.name;
  j["texture_id"] = tileset.texture_id;
  j["tile_width"] = tileset.tile_width;
  j["tile_height"] = tileset.tile_height;

  std::vector<nlohmann::json> tiles_json;
  for (const Tile& tile_def : tileset.tiles) {
    nlohmann::json tile_j;
    ToJson(tile_j, tile_def);
    tiles_json.push_back(tile_j);
  }
  j["tiles"] = tiles_json;

  return j;
}

absl::StatusOr<Tileset> GetTilesetFromJson(const nlohmann::json& j) {
  Tileset tileset;
  try {
    j.at("id").get_to(tileset.id);
    j.at("name").get_to(tileset.name);
    j.at("texture_id").get_to(tileset.texture_id);
    tileset.tile_width = j.value("tile_width", 16);
    tileset.tile_height = j.value("tile_height", 16);
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(
        absl::StrCat("JSON parsing error for Tileset: ", e.what()));
  }

  if (!j.contains("tiles")) return tileset;

  for (const nlohmann::json& tile_j : j["tiles"]) {
    ASSIGN_OR_RETURN(Tile tile_def, GetTileFromJson(tile_j));
    tileset.tiles.push_back(std::move(tile_def));
  }
  return tileset;
}

absl::Status ValidateTileset(const Tileset& tileset) {
  if (tileset.name.empty()) {
    return absl::InvalidArgumentError("Tileset name cannot be empty.");
  }
  if (tileset.texture_id.empty()) {
    return absl::InvalidArgumentError("Tileset texture_id cannot be empty.");
  }
  if (tileset.tile_width <= 0) {
    return absl::InvalidArgumentError("Tileset tile_width must be positive.");
  }
  if (tileset.tile_height <= 0) {
    return absl::InvalidArgumentError("Tileset tile_height must be positive.");
  }

  absl::flat_hash_set<int> seen_ids;
  for (const Tile& tile_def : tileset.tiles) {
    if (tile_def.id <= 0) {
      return absl::InvalidArgumentError(
          "Tile ID must be a positive integer; 0 is reserved for empty cells.");
    }
    if (tile_def.name.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Tile with id ", tile_def.id, " must have a name."));
    }
    if (seen_ids.contains(tile_def.id)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate tile ID within tileset: ", tile_def.id));
    }
    seen_ids.insert(tile_def.id);
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<TilesetManager>> TilesetManager::Create(
    std::string root_path) {
  return std::unique_ptr<TilesetManager>(
      new TilesetManager(std::move(root_path)));
}

TilesetManager::TilesetManager(std::string root_path)
    : root_path_(std::move(root_path)),
      definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

std::string TilesetManager::GetDefinitionsPath(const std::string& relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

absl::StatusOr<Tileset*> TilesetManager::LoadTileset(
    const std::string& path_json) {
  const std::string full_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(full_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", full_path));
  }

  std::ifstream stream(full_path);
  nlohmann::json json;
  stream >> json;

  ASSIGN_OR_RETURN(Tileset tileset, GetTilesetFromJson(json));

  // Deduplicate: return the already-cached pointer if this ID is known.
  if (tilesets_.find(tileset.id) != tilesets_.end()) {
    return tilesets_[tileset.id].get();
  }

  std::string id = tileset.id;
  tilesets_[id] = std::make_unique<Tileset>(std::move(tileset));
  return tilesets_[id].get();
}

absl::Status TilesetManager::LoadAllTilesets() {
  if (!std::filesystem::exists(definitions_path_)) {
    return absl::NotFoundError(
        absl::StrCat("Tileset root directory not found: ", definitions_path_));
  }

  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadTileset(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load tileset from " << entry.path() << ": "
                   << status.status();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> TilesetManager::CreateTileset(Tileset tileset) {
  if (tileset.name.empty()) {
    return absl::InvalidArgumentError("Tilesets must have a non-empty name.");
  }

  // The id is always generated; never accept a caller-provided id.
  tileset.id = GenerateGuid();

  RETURN_IF_ERROR(SaveTileset(tileset));

  std::string filename = absl::StrCat(tileset.name, "-", tileset.id, ".json");
  ASSIGN_OR_RETURN(Tileset * loaded, LoadTileset(filename));
  return loaded->id;
}

absl::Status TilesetManager::SaveTileset(const Tileset& tileset) {
  if (tileset.id.empty()) {
    return absl::InvalidArgumentError("Tileset must have an ID to be saved.");
  }

  RETURN_IF_ERROR(ValidateTileset(tileset));

  // Handle renaming: remove the old file if the name has changed.
  auto it = tilesets_.find(tileset.id);
  if (it != tilesets_.end()) {
    RemoveOldFileIfExists(tileset.id, it->second->name, tileset.name,
                          definitions_path_);
  }

  nlohmann::json json = ToJson(tileset);
  std::string filename = absl::StrCat(tileset.name, "-", tileset.id, ".json");
  std::string full_path = GetDefinitionsPath(filename);

  std::filesystem::create_directories(definitions_path_);

  std::ofstream file(full_path);
  if (!file.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open file for writing: ", full_path));
  }
  file << json.dump(4);

  tilesets_[tileset.id] = std::make_unique<Tileset>(tileset);
  return absl::OkStatus();
}

absl::StatusOr<Tileset*> TilesetManager::GetTileset(const std::string& id) {
  auto it = tilesets_.find(id);
  if (it == tilesets_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Tileset with id ", id, " not found in manager."));
  }
  return it->second.get();
}

absl::Status TilesetManager::DeleteTileset(const std::string& id) {
  auto it = tilesets_.find(id);
  if (it == tilesets_.end()) {
    return absl::NotFoundError("Tileset not found.");
  }

  std::string filename = absl::StrCat(it->second->name, "-", id, ".json");
  std::filesystem::remove(GetDefinitionsPath(filename));

  tilesets_.erase(it);
  return absl::OkStatus();
}

std::vector<Tileset> TilesetManager::GetAllTilesets() const {
  std::vector<Tileset> tilesets;
  tilesets.reserve(tilesets_.size());
  for (const auto& [id, tileset] : tilesets_) {
    tilesets.push_back(*tileset);
  }
  return tilesets;
}

bool TilesetManager::IsTextureUsed(const std::string& texture_id) const {
  for (const auto& [id, tileset] : tilesets_) {
    if (tileset->texture_id == texture_id) return true;
  }
  return false;
}

}  // namespace zebes
