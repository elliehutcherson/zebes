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
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/prop_recipes";

bool IsSafeId(std::string_view id) {
  if (id.empty()) return false;
  for (const char character : id) {
    const bool safe = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '-';
    if (!safe) return false;
  }
  return true;
}

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
  ResourceLoadFailures failures;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    absl::StatusOr<PropRecipe> parsed = LoadRecipeFile(entry.path().string());
    if (!parsed.ok()) {
      failures.Add(entry.path().string(), parsed.status());
      continue;
    }
    PropRecipe recipe = std::move(*parsed);
    if (!IsSafeId(recipe.id)) {
      failures.Add(entry.path().string(),
                   absl::InvalidArgumentError("prop recipe ID is not path-safe"));
      continue;
    }
    if (entry.path().stem() != recipe.id) {
      failures.Add(entry.path().string(),
                   absl::InvalidArgumentError("prop recipe filename does not match its ID"));
      continue;
    }
    if (loaded.contains(recipe.id)) {
      failures.Add(entry.path().string(),
                   absl::AlreadyExistsError(absl::StrCat("duplicate prop recipe ID ", recipe.id)));
      continue;
    }
    const std::string id = recipe.id;
    loaded[id] = std::make_unique<PropRecipe>(std::move(recipe));
  }
  RETURN_IF_ERROR(failures.ToStatus("prop recipe"));
  recipes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> PropRecipeManager::CreateRecipe(PropRecipe recipe) {
  recipe.id = GenerateGuid();
  RETURN_IF_ERROR(SaveRecipe(recipe));
  return recipe.id;
}

absl::Status PropRecipeManager::SaveRecipe(const PropRecipe& recipe) {
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  if (!IsSafeId(recipe.id)) return absl::InvalidArgumentError("prop recipe ID is not path-safe");

  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create prop recipe directory: ", error.message()));
  }

  const std::string target = RecipePath(recipe.id);
  const std::string temporary = absl::StrCat(target, ".tmp");
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
      return absl::InternalError(absl::StrCat("could not write prop recipe: ", temporary));
    }
    stream << PropRecipeToJson(recipe).dump(2);
    stream.flush();
    if (!stream.good()) {
      std::filesystem::remove(temporary, error);
      return absl::InternalError(absl::StrCat("failed while writing prop recipe: ", temporary));
    }
  }
  std::filesystem::rename(temporary, target, error);
  if (error) {
    std::filesystem::remove(temporary);
    return absl::InternalError(absl::StrCat("could not commit prop recipe: ", error.message()));
  }

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
  std::sort(result.begin(), result.end(), [](const PropRecipe& left, const PropRecipe& right) {
    if (left.name != right.name) return left.name < right.name;
    return left.id < right.id;
  });
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
