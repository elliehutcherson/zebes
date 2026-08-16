#include "resources/terrain_recipe_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/terrain_recipes";

absl::Status ValidateRecipeForSave(const TerrainRecipe& recipe) {
  if (recipe.id.empty()) return absl::InvalidArgumentError("terrain recipe ID is empty");
  if (recipe.name.empty()) return absl::InvalidArgumentError("terrain recipe name is empty");
  if (recipe.tileset_id.empty() || recipe.texture_id.empty()) {
    return absl::InvalidArgumentError("terrain recipe is missing its generated asset IDs");
  }
  if (recipe.terrain_id <= 0) {
    return absl::InvalidArgumentError("terrain recipe terrain ID must be positive");
  }
  return ResolveTerrainStyle(recipe.config).status();
}

}  // namespace

absl::StatusOr<std::unique_ptr<TerrainRecipeManager>> TerrainRecipeManager::Create(
    std::string root_path) {
  if (root_path.empty()) return absl::InvalidArgumentError("terrain recipe asset root is empty");
  return std::unique_ptr<TerrainRecipeManager>(new TerrainRecipeManager(std::move(root_path)));
}

TerrainRecipeManager::TerrainRecipeManager(std::string root_path)
    : definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)) {}

std::string TerrainRecipeManager::RecipePath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

absl::StatusOr<TerrainRecipe> TerrainRecipeManager::LoadRecipeFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));

  try {
    nlohmann::json json;
    stream >> json;
    return TerrainRecipeFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid terrain recipe JSON in ", path, ": ", error.what()));
  }
}

absl::Status TerrainRecipeManager::LoadAllRecipes() {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create terrain recipe directory: ", error.message()));
  }

  absl::flat_hash_map<std::string, std::unique_ptr<TerrainRecipe>> loaded;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    ASSIGN_OR_RETURN(TerrainRecipe recipe, LoadRecipeFile(entry.path().string()));
    if (entry.path().stem() != recipe.id) {
      return absl::InvalidArgumentError(
          absl::StrCat("terrain recipe filename does not match its ID: ", entry.path().string()));
    }
    if (loaded.contains(recipe.id)) {
      return absl::AlreadyExistsError(absl::StrCat("duplicate terrain recipe ID ", recipe.id));
    }
    const std::string id = recipe.id;
    loaded[id] = std::make_unique<TerrainRecipe>(std::move(recipe));
  }
  recipes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> TerrainRecipeManager::CreateRecipe(TerrainRecipe recipe) {
  recipe.id = GenerateGuid();
  RETURN_IF_ERROR(SaveRecipe(recipe));
  return recipe.id;
}

absl::Status TerrainRecipeManager::SaveRecipe(const TerrainRecipe& recipe) {
  RETURN_IF_ERROR(ValidateRecipeForSave(recipe));

  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create terrain recipe directory: ", error.message()));
  }

  const std::string target = RecipePath(recipe.id);
  const std::string temporary = absl::StrCat(target, ".tmp");
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
      return absl::InternalError(absl::StrCat("could not write terrain recipe: ", temporary));
    }
    stream << TerrainRecipeToJson(recipe).dump(2);
    stream.flush();
    if (!stream.good()) {
      std::filesystem::remove(temporary, error);
      return absl::InternalError(absl::StrCat("failed while writing terrain recipe: ", temporary));
    }
  }

  std::filesystem::rename(temporary, target, error);
  if (error) {
    // Windows does not replace an existing destination. Keep the common path
    // atomic, but report platforms that cannot provide that guarantee instead
    // of deleting the old file first and risking data loss.
    std::filesystem::remove(temporary);
    return absl::InternalError(absl::StrCat("could not commit terrain recipe: ", error.message()));
  }

  // Assigned through the existing allocation rather than replacing it: callers
  // hold TerrainRecipe* from GetRecipe, and swapping the unique_ptr frees what
  // they point at. The pointer indirection exists so an address survives a save.
  if (auto it = recipes_.find(recipe.id); it != recipes_.end()) {
    *it->second = recipe;
    return absl::OkStatus();
  }
  recipes_[recipe.id] = std::make_unique<TerrainRecipe>(recipe);
  return absl::OkStatus();
}

absl::StatusOr<TerrainRecipe*> TerrainRecipeManager::GetRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError(absl::StrCat("terrain recipe ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<TerrainRecipe> TerrainRecipeManager::GetAllRecipes() const {
  std::vector<TerrainRecipe> recipes;
  recipes.reserve(recipes_.size());
  for (const auto& [id, recipe] : recipes_) recipes.push_back(*recipe);
  std::sort(recipes.begin(), recipes.end(),
            [](const TerrainRecipe& left, const TerrainRecipe& right) {
              if (left.name != right.name) return left.name < right.name;
              return left.id < right.id;
            });
  return recipes;
}

absl::Status TerrainRecipeManager::DeleteRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) return absl::NotFoundError("terrain recipe is not loaded");

  std::error_code error;
  if (!std::filesystem::remove(RecipePath(id), error) || error) {
    return absl::InternalError(absl::StrCat("could not delete terrain recipe: ", error.message()));
  }
  recipes_.erase(found);
  return absl::OkStatus();
}

}  // namespace zebes
