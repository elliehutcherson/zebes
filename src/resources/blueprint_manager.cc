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
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/blueprints";

}  // namespace

// JSON Serialization helpers locally
void ToJson(nlohmann::json& j, const Blueprint& blueprint) {
  j = nlohmann::json{
      {"id", blueprint.id},
      {"name", blueprint.name},
  };

  std::vector<nlohmann::json> states_json;
  for (const auto& state : blueprint.states) {
    states_json.push_back({
        {"name", state.name},
        {"collider_id", state.collider_id},
        {"sprite_id", state.sprite_id},
    });
  }
  j["states"] = states_json;
}

absl::StatusOr<Blueprint> GetBlueprintFromJson(const nlohmann::json& j) {
  Blueprint blueprint;
  try {
    j.at("id").get_to(blueprint.id);
    j.at("name").get_to(blueprint.name);
    if (!j.contains("states")) return blueprint;

    for (const auto& state_json : j["states"]) {
      Blueprint::State state;
      state_json.at("name").get_to(state.name);
      if (state_json.contains("collider_id")) {
        state_json.at("collider_id").get_to(state.collider_id);
      }
      if (state_json.contains("sprite_id")) {
        state_json.at("sprite_id").get_to(state.sprite_id);
      }
      blueprint.states.push_back(state);
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
  // The id should always be generated. Never allow an id to be passed in.
  blueprint.id = GenerateGuid();

  RETURN_IF_ERROR(SaveBlueprint(blueprint));

  // Load it
  std::string filename = absl::StrCat(blueprint.name, "-", blueprint.id, ".json");
  ASSIGN_OR_RETURN(Blueprint * loaded_blueprint, LoadBlueprint(filename));

  return loaded_blueprint->id;
}

absl::Status BlueprintManager::SaveBlueprint(Blueprint blueprint) {
  if (blueprint.id.empty()) {
    return absl::InvalidArgumentError("Blueprint must have an ID to be saved.");
  }
  if (blueprint.name.empty()) {
    return absl::InvalidArgumentError("Blueprint must have a name to be saved.");
  }

  // VALIDATION: Ensure keys in maps are valid indices for states
  // VALIDATION: Ensure states have names?
  for (const auto& state : blueprint.states) {
    if (state.name.empty()) {
      return absl::InvalidArgumentError("All blueprint states must have a name.");
    }
  }

  // Handle Renaming: If the name has changed, delete the old file.
  auto it = blueprints_.find(blueprint.id);
  if (it != blueprints_.end()) {
    RemoveOldFileIfExists(blueprint.id, it->second->name, blueprint.name, definitions_path_);
  }

  nlohmann::json json;
  ToJson(json, blueprint);

  std::string filename = absl::StrCat(blueprint.name, "-", blueprint.id, ".json");
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
  // We need to know the name to reconstruct filename, or search for it.
  // Since we have the blueprint in memory, we can use it.
  const auto& blueprint = it->second;
  std::string filename = absl::StrCat(blueprint->name, "-", id, ".json");
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
    for (const auto& state : blueprint->states) {
      if (state.sprite_id == sprite_id) return true;
    }
  }
  return false;
}

bool BlueprintManager::IsColliderUsed(const std::string& collider_id) const {
  for (const auto& [id, blueprint] : blueprints_) {
    for (const auto& state : blueprint->states) {
      if (state.collider_id == collider_id) return true;
    }
  }
  return false;
}

}  // namespace zebes
