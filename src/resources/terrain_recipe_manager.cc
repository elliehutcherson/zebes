#include "resources/terrain_recipe_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/named_asset_order.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

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
  RETURN_IF_ERROR(LoadJsonDefinitions(
      definitions_path_, "terrain recipe",
      [&loaded](const std::filesystem::path& path) -> absl::Status {
        ASSIGN_OR_RETURN(TerrainRecipe recipe, LoadRecipeFile(path.string()));
        if (path.stem() != recipe.id) {
          return absl::InvalidArgumentError("terrain recipe filename does not match its ID");
        }
        if (loaded.contains(recipe.id)) {
          return absl::AlreadyExistsError(absl::StrCat("duplicate terrain recipe ID ", recipe.id));
        }
        const std::string id = recipe.id;
        loaded[id] = std::make_unique<TerrainRecipe>(std::move(recipe));
        return absl::OkStatus();
      }));
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

  RETURN_IF_ERROR(
      WriteTextFileAtomically(RecipePath(recipe.id), TerrainRecipeToJson(recipe).dump(2)));

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
  std::ranges::sort(recipes, NamedAssetLess{});
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
