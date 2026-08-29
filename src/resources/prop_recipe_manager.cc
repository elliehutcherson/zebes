#include "resources/prop_recipe_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/named_asset_order.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/prop_recipes";

}  // namespace

absl::StatusOr<std::unique_ptr<PropRecipeManager>> PropRecipeManager::Create(
    std::string root_path) {
  if (root_path.empty()) return absl::InvalidArgumentError("prop recipe asset root is empty");
  return std::unique_ptr<PropRecipeManager>(new PropRecipeManager(std::move(root_path)));
}

PropRecipeManager::PropRecipeManager(std::string root_path)
    : definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)) {}

std::string PropRecipeManager::RecipePath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

absl::StatusOr<PropRecipe> PropRecipeManager::LoadRecipeFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  try {
    nlohmann::json json;
    stream >> json;
    return PropRecipeFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid prop recipe JSON in ", path, ": ", error.what()));
  }
}

absl::Status PropRecipeManager::LoadAllRecipes() {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create prop recipe directory: ", error.message()));
  }

  absl::flat_hash_map<std::string, std::unique_ptr<PropRecipe>> loaded;
  RETURN_IF_ERROR(LoadJsonDefinitions(
      definitions_path_, "prop recipe",
      [&loaded](const std::filesystem::path& path) -> absl::Status {
        ASSIGN_OR_RETURN(PropRecipe recipe, LoadRecipeFile(path.string()));
        if (!IsPathSafeResourceId(recipe.id)) {
          return absl::InvalidArgumentError("prop recipe ID is not path-safe");
        }
        if (path.stem() != recipe.id) {
          return absl::InvalidArgumentError("prop recipe filename does not match its ID");
        }
        if (loaded.contains(recipe.id)) {
          return absl::AlreadyExistsError(absl::StrCat("duplicate prop recipe ID ", recipe.id));
        }
        const std::string id = recipe.id;
        loaded[id] = std::make_unique<PropRecipe>(std::move(recipe));
        return absl::OkStatus();
      }));
  recipes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> PropRecipeManager::CreateRecipe(PropRecipe recipe) {
  recipe.id = GenerateGuid();
  RETURN_IF_ERROR(SaveRecipe(recipe));
  return recipe.id;
}

absl::Status PropRecipeManager::CreateRecipeWithId(PropRecipe recipe) {
  RETURN_IF_ERROR(PreflightRecipeWithId(recipe));
  return SaveRecipe(recipe);
}

absl::Status PropRecipeManager::PreflightRecipeWithId(const PropRecipe& recipe) const {
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("prop recipe ID is not path-safe");
  }
  if (recipes_.contains(recipe.id) || std::filesystem::exists(RecipePath(recipe.id))) {
    return absl::AlreadyExistsError(absl::StrCat("prop recipe ", recipe.id, " exists"));
  }
  return absl::OkStatus();
}

absl::Status PropRecipeManager::SaveRecipe(const PropRecipe& recipe) {
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("prop recipe ID is not path-safe");
  }

  RETURN_IF_ERROR(WriteTextFileAtomically(RecipePath(recipe.id), PropRecipeToJson(recipe).dump(2)));

  if (auto found = recipes_.find(recipe.id); found != recipes_.end()) {
    *found->second = recipe;
    return absl::OkStatus();
  }
  recipes_[recipe.id] = std::make_unique<PropRecipe>(recipe);
  return absl::OkStatus();
}

absl::StatusOr<PropRecipe*> PropRecipeManager::GetRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError(absl::StrCat("prop recipe ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<PropRecipe> PropRecipeManager::GetAllRecipes() const {
  std::vector<PropRecipe> result;
  result.reserve(recipes_.size());
  for (const auto& [id, recipe] : recipes_) result.push_back(*recipe);
  std::ranges::sort(result, NamedAssetLess{});
  return result;
}

absl::Status PropRecipeManager::DeleteRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) return absl::NotFoundError("prop recipe is not loaded");
  std::error_code error;
  if (!std::filesystem::remove(RecipePath(id), error) || error) {
    return absl::InternalError(absl::StrCat("could not delete prop recipe: ", error.message()));
  }
  recipes_.erase(found);
  return absl::OkStatus();
}

}  // namespace zebes
