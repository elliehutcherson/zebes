#include "resources/blueprint_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/blueprints";

}  // namespace

// JSON Serialization helpers locally
void ToJson(nlohmann::json& j, const Blueprint& blueprint) {
  j = nlohmann::json{
      {"id", blueprint.id},
      {"states", blueprint.states},
      {"collider_ids", blueprint.collider_ids},
      {"sprite_ids", blueprint.sprite_ids},
  };
}

absl::StatusOr<Blueprint> GetBlueprintFromJson(const nlohmann::json& j) {
  Blueprint blueprint;
  try {
    j.at("id").get_to(blueprint.id);
    j.at("states").get_to(blueprint.states);

    // Nlohmann json can map json objects to std::map
    // However, keys in json are strings, while our map keys are int.
    // Nlohmann handles this conversion automatically for std::map<int, ...>
    if (j.contains("collider_ids")) {
      j.at("collider_ids").get_to(blueprint.collider_ids);
    }
    if (j.contains("sprite_ids")) {
      j.at("sprite_ids").get_to(blueprint.sprite_ids);
    }

  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(absl::StrCat("JSON parsing error for Blueprint: ", e.what()));
  }
  return blueprint;
}

absl::StatusOr<std::unique_ptr<BlueprintManager>> BlueprintManager::Create(std::string root_path) {
  return std::unique_ptr<BlueprintManager>(new BlueprintManager(root_path));
}

BlueprintManager::BlueprintManager(std::string root_path)
    : root_path_(root_path), definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

std::string BlueprintManager::GetDefinitionsPath(const std::string relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

absl::StatusOr<Blueprint*> BlueprintManager::LoadBlueprint(const std::string& path_json) {
  const std::string definitions_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(definitions_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", definitions_path));
  }

  std::ifstream stream(definitions_path);
  nlohmann::json json;
  stream >> json;

  ASSIGN_OR_RETURN(Blueprint blueprint, GetBlueprintFromJson(json));

  // Check for duplicate ID
  if (blueprints_.find(blueprint.id) != blueprints_.end()) {
    return blueprints_[blueprint.id].get();
  }

  // Create Blueprint object.
  std::string id = blueprint.id;
  blueprints_[id] = std::make_unique<Blueprint>(std::move(blueprint));
  return blueprints_[id].get();
}

absl::Status BlueprintManager::LoadAllBlueprints() {
  if (!std::filesystem::exists(definitions_path_)) {
    // If the directory doesn't exist, we can treat it as no blueprints or return error.
    // Other managers return Not Found.
    return absl::NotFoundError(
        absl::StrCat("Blueprint root directory not found: ", definitions_path_));
  }

  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadBlueprint(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load blueprint from " << entry.path() << ": " << status.status();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> BlueprintManager::CreateBlueprint(Blueprint blueprint) {
  // Generate ID if empty
  if (blueprint.id.empty()) {
    blueprint.id = GenerateGuid();
  }

  RETURN_IF_ERROR(SaveBlueprint(blueprint));

  // Load it
  std::string filename = absl::StrCat(blueprint.id, ".json");
  ASSIGN_OR_RETURN(Blueprint * loaded_blueprint, LoadBlueprint(filename));

  return loaded_blueprint->id;
}

absl::Status BlueprintManager::SaveBlueprint(Blueprint blueprint) {
  if (blueprint.id.empty()) {
    return absl::InvalidArgumentError("Blueprint must have an ID to be saved.");
  }

  // VALIDATION: Ensure keys in maps are valid indices for states
  size_t states_count = blueprint.states.size();
  for (const auto& [key, val] : blueprint.collider_ids) {
    if (key < 0) return absl::InvalidArgumentError("Negative index in collider_ids");
    if (static_cast<size_t>(key) >= states_count) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Collider ID key ", key, " is out of bounds for states size ", states_count));
    }
  }
  for (const auto& [key, val] : blueprint.sprite_ids) {
    if (key < 0) return absl::InvalidArgumentError("Negative index in sprite_ids");
    if (static_cast<size_t>(key) >= states_count) {
      return absl::InvalidArgumentError(
          absl::StrCat("Sprite ID key ", key, " is out of bounds for states size ", states_count));
    }
  }

  nlohmann::json json;
  ToJson(json, blueprint);

  std::string filename = absl::StrCat(blueprint.id, ".json");
  std::string definitions_path = GetDefinitionsPath(filename);

  // Ensure directory exists
  std::filesystem::create_directories(definitions_path_);

  std::ofstream file(definitions_path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file for writing: ", definitions_path));
  }
  file << json.dump(4);

  // Update in-memory cache
  std::string id = blueprint.id;
  blueprints_[id] = std::make_unique<Blueprint>(std::move(blueprint));

  return absl::OkStatus();
}

absl::StatusOr<Blueprint*> BlueprintManager::GetBlueprint(const std::string& id) {
  auto it = blueprints_.find(id);
  if (it == blueprints_.end()) {
    return absl::NotFoundError(absl::StrCat("Blueprint with id ", id, " not found in manager."));
  }
  return it->second.get();
}

absl::Status BlueprintManager::DeleteBlueprint(const std::string& id) {
  auto it = blueprints_.find(id);
  if (it == blueprints_.end()) return absl::NotFoundError("Blueprint not found");

  // Remove JSON file
  std::string filename = absl::StrCat(id, ".json");
  std::filesystem::remove(GetDefinitionsPath(filename));

  blueprints_.erase(it);
  return absl::OkStatus();
}

std::vector<Blueprint> BlueprintManager::GetAllBlueprints() const {
  std::vector<Blueprint> blueprints;
  blueprints.reserve(blueprints_.size());
  for (const auto& [id, blueprint] : blueprints_) {
    blueprints.push_back(*blueprint);
  }
  return blueprints;
}

bool BlueprintManager::IsSpriteUsed(const std::string& sprite_id) const {
  for (const auto& [id, blueprint] : blueprints_) {
    for (const auto& [index, s_id] : blueprint->sprite_ids) {
      if (s_id == sprite_id) return true;
    }
  }
  return false;
}

bool BlueprintManager::IsColliderUsed(const std::string& collider_id) const {
  for (const auto& [id, blueprint] : blueprints_) {
    for (const auto& [index, c_id] : blueprint->collider_ids) {
      if (c_id == collider_id) return true;
    }
  }
  return false;
}

}  // namespace zebes
