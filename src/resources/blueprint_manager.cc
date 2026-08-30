#include "resources/blueprint_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/blueprints";

absl::Status ValidateBlueprint(const Blueprint& blueprint) {
  if (blueprint.id.empty()) {
    return absl::InvalidArgumentError("Blueprint must have an ID to be saved.");
  }
  if (blueprint.name.empty()) {
    return absl::InvalidArgumentError("Blueprint must have a name to be saved.");
  }

  absl::flat_hash_set<std::string> state_keys;
  for (const Blueprint::State& state : blueprint.states) {
    if (!IsValidBlueprintStateKey(state.key)) {
      return absl::InvalidArgumentError(
          "All blueprint states must have a lowercase alphanumeric key with optional hyphens.");
    }
    if (!state_keys.insert(state.key).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("Blueprint state key must be unique: ", state.key));
    }
    if (state.name.empty()) {
      return absl::InvalidArgumentError("All blueprint states must have a name.");
    }
    if (!IsValidBlueprintPlacementMode(state.placement_mode)) {
      return absl::InvalidArgumentError("All blueprint states must have a valid placement mode.");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<BlueprintPlacementMode> BlueprintPlacementModeFromId(
    const std::string& placement_mode) {
  if (placement_mode == "grounded") return BlueprintPlacementMode::kGrounded;
  if (placement_mode == "ceiling") return BlueprintPlacementMode::kCeiling;
  if (placement_mode == "free") return BlueprintPlacementMode::kFree;
  return absl::InvalidArgumentError(
      absl::StrCat("unknown blueprint placement mode: ", placement_mode));
}

}  // namespace

void ToJson(nlohmann::json& j, const Blueprint& blueprint) {
  j = nlohmann::json{
      {"id", blueprint.id},
      {"name", blueprint.name},
  };

  std::vector<nlohmann::json> states_json;
  for (const auto& state : blueprint.states) {
    states_json.push_back({
        {"key", state.key},
        {"name", state.name},
        {"collider_id", state.collider_id},
        {"sprite_id", state.sprite_id},
        {"placement_mode", BlueprintPlacementModeId(state.placement_mode)},
    });
  }
  j["states"] = states_json;
}

absl::StatusOr<Blueprint> GetBlueprintFromJson(const nlohmann::json& j) {
  Blueprint blueprint;
  try {
    j.at("id").get_to(blueprint.id);
    j.at("name").get_to(blueprint.name);
    for (const auto& state_json : j.at("states")) {
      Blueprint::State state;
      state_json.at("key").get_to(state.key);
      state_json.at("name").get_to(state.name);
      state_json.at("collider_id").get_to(state.collider_id);
      state_json.at("sprite_id").get_to(state.sprite_id);
      std::string placement_mode;
      state_json.at("placement_mode").get_to(placement_mode);
      ASSIGN_OR_RETURN(state.placement_mode, BlueprintPlacementModeFromId(placement_mode));
      blueprint.states.push_back(state);
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(absl::StrCat("JSON parsing error for Blueprint: ", e.what()));
  }

  RETURN_IF_ERROR(ValidateBlueprint(blueprint));
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

  // Returns the live object rather than reloading; callers hold Blueprint*.
  if (blueprints_.find(blueprint.id) != blueprints_.end()) {
    return blueprints_[blueprint.id].get();
  }

  std::string id = blueprint.id;
  blueprints_[id] = std::make_unique<Blueprint>(std::move(blueprint));
  return blueprints_[id].get();
}

absl::Status BlueprintManager::LoadAllBlueprints() {
  return LoadJsonDefinitions(definitions_path_, "blueprint",
                             [this](const std::filesystem::path& path) -> absl::Status {
                               return LoadBlueprint(path.filename().string()).status();
                             });
}

absl::StatusOr<std::string> BlueprintManager::CreateBlueprint(Blueprint blueprint) {
  // The id should always be generated. Never allow an id to be passed in.
  blueprint.id = GenerateGuid();

  RETURN_IF_ERROR(SaveBlueprint(blueprint));

  // Reloaded from disk, so the cached blueprint is what a fresh start loads.
  std::string filename = absl::StrCat(blueprint.name, "-", blueprint.id, ".json");
  ASSIGN_OR_RETURN(Blueprint * loaded_blueprint, LoadBlueprint(filename));

  return loaded_blueprint->id;
}

absl::Status BlueprintManager::CreateBlueprintWithId(Blueprint blueprint) {
  RETURN_IF_ERROR(PreflightBlueprintWithId(blueprint));
  return SaveBlueprint(std::move(blueprint));
}

absl::Status BlueprintManager::PreflightBlueprintWithId(const Blueprint& blueprint) {
  RETURN_IF_ERROR(ValidateBlueprint(blueprint));
  if (!IsPathSafeResourceId(blueprint.id) || !IsSafeResourceName(blueprint.name)) {
    return absl::InvalidArgumentError("prepared blueprint needs a path-safe ID and name");
  }
  if (blueprints_.contains(blueprint.id)) {
    return absl::AlreadyExistsError(absl::StrCat("Blueprint with id ", blueprint.id, " exists"));
  }
  const std::string filename = absl::StrCat(blueprint.name, "-", blueprint.id, ".json");
  if (std::filesystem::exists(GetDefinitionsPath(filename))) {
    return absl::AlreadyExistsError("prepared blueprint definition already exists");
  }
  return absl::OkStatus();
}

absl::Status BlueprintManager::SaveBlueprint(Blueprint blueprint) {
  RETURN_IF_ERROR(ValidateBlueprint(blueprint));

  nlohmann::json json;
  ToJson(json, blueprint);

  std::string filename = absl::StrCat(blueprint.name, "-", blueprint.id, ".json");
  std::string definitions_path = GetDefinitionsPath(filename);

  RETURN_IF_ERROR(WriteTextFileAtomically(definitions_path, json.dump(4)));

  // Publish the new definition before removing the old name. A failed write
  // must leave the previously loaded blueprint durable.
  auto it = blueprints_.find(blueprint.id);
  if (it != blueprints_.end()) {
    RemoveOldFileIfExists(blueprint.id, it->second->name, blueprint.name, definitions_path_);
  }

  // Assigned through the existing allocation rather than replacing it: callers
  // hold Blueprint* from GetBlueprint, and swapping the unique_ptr frees what
  // they point at. The pointer indirection exists so an address survives a save.
  std::string id = blueprint.id;
  if (auto it = blueprints_.find(id); it != blueprints_.end()) {
    *it->second = std::move(blueprint);
    return absl::OkStatus();
  }
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

  const auto& blueprint = it->second;
  std::string filename = absl::StrCat(blueprint->name, "-", id, ".json");
  std::error_code error;
  std::filesystem::remove(GetDefinitionsPath(filename), error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not delete blueprint definition: ", error.message()));
  }

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

}  // namespace zebes
