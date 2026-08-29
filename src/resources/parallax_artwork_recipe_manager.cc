#include "resources/parallax_artwork_recipe_manager.h"

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

constexpr char kDefinitionsPath[] = "definitions/parallax_artwork_recipes";

}  // namespace

absl::StatusOr<std::unique_ptr<ParallaxArtworkRecipeManager>> ParallaxArtworkRecipeManager::Create(
    std::string root_path) {
  if (root_path.empty()) {
    return absl::InvalidArgumentError("parallax artwork recipe asset root is empty");
  }
  return std::unique_ptr<ParallaxArtworkRecipeManager>(
      new ParallaxArtworkRecipeManager(std::move(root_path)));
}

ParallaxArtworkRecipeManager::ParallaxArtworkRecipeManager(std::string root_path)
    : definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)) {}

std::string ParallaxArtworkRecipeManager::RecipePath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

absl::StatusOr<ParallaxArtworkRecipe> ParallaxArtworkRecipeManager::LoadRecipeFile(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  try {
    nlohmann::json json;
    stream >> json;
    return ParallaxArtworkRecipeFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid parallax artwork recipe JSON in ", path, ": ", error.what()));
  }
}

absl::Status ParallaxArtworkRecipeManager::LoadAllRecipes() {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create parallax artwork recipe directory: ", error.message()));
  }

  absl::flat_hash_map<std::string, std::unique_ptr<ParallaxArtworkRecipe>> loaded;
  RETURN_IF_ERROR(LoadJsonDefinitions(
      definitions_path_, "parallax artwork recipe",
      [&loaded](const std::filesystem::path& path) -> absl::Status {
        ASSIGN_OR_RETURN(ParallaxArtworkRecipe recipe, LoadRecipeFile(path.string()));
        if (!IsPathSafeResourceId(recipe.id)) {
          return absl::InvalidArgumentError("parallax artwork recipe ID is not path-safe");
        }
        if (path.stem() != recipe.id) {
          return absl::InvalidArgumentError(
              "parallax artwork recipe filename does not match its ID");
        }
        if (loaded.contains(recipe.id)) {
          return absl::AlreadyExistsError(
              absl::StrCat("duplicate parallax artwork recipe ID ", recipe.id));
        }
        const std::string id = recipe.id;
        loaded[id] = std::make_unique<ParallaxArtworkRecipe>(std::move(recipe));
        return absl::OkStatus();
      }));
  recipes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> ParallaxArtworkRecipeManager::CreateRecipe(
    ParallaxArtworkRecipe recipe) {
  recipe.id = GenerateGuid();
  RETURN_IF_ERROR(SaveRecipe(recipe));
  return recipe.id;
}

absl::Status ParallaxArtworkRecipeManager::CreateRecipeWithId(ParallaxArtworkRecipe recipe) {
  RETURN_IF_ERROR(PreflightRecipeWithId(recipe));
  return SaveRecipe(recipe);
}

absl::Status ParallaxArtworkRecipeManager::PreflightRecipeWithId(
    const ParallaxArtworkRecipe& recipe) const {
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("parallax artwork recipe ID is not path-safe");
  }
  if (recipes_.contains(recipe.id) || std::filesystem::exists(RecipePath(recipe.id))) {
    return absl::AlreadyExistsError(absl::StrCat("parallax artwork recipe ", recipe.id, " exists"));
  }
  return absl::OkStatus();
}

absl::Status ParallaxArtworkRecipeManager::SaveRecipe(const ParallaxArtworkRecipe& recipe) {
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("parallax artwork recipe ID is not path-safe");
  }

  RETURN_IF_ERROR(
      WriteTextFileAtomically(RecipePath(recipe.id), ParallaxArtworkRecipeToJson(recipe).dump(2)));

  if (auto found = recipes_.find(recipe.id); found != recipes_.end()) {
    *found->second = recipe;
    return absl::OkStatus();
  }
  recipes_[recipe.id] = std::make_unique<ParallaxArtworkRecipe>(recipe);
  return absl::OkStatus();
}

absl::StatusOr<ParallaxArtworkRecipe*> ParallaxArtworkRecipeManager::GetRecipe(
    const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError(absl::StrCat("parallax artwork recipe ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<ParallaxArtworkRecipe> ParallaxArtworkRecipeManager::GetAllRecipes() const {
  std::vector<ParallaxArtworkRecipe> result;
  result.reserve(recipes_.size());
  for (const auto& [id, recipe] : recipes_) {
    static_cast<void>(id);
    result.push_back(*recipe);
  }
  std::ranges::sort(result, NamedAssetLess{});
  return result;
}

absl::Status ParallaxArtworkRecipeManager::DeleteRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError("parallax artwork recipe is not loaded");
  }
  std::error_code error;
  if (!std::filesystem::remove(RecipePath(id), error) || error) {
    return absl::InternalError(
        absl::StrCat("could not delete parallax artwork recipe: ", error.message()));
  }
  recipes_.erase(found);
  return absl::OkStatus();
}

}  // namespace zebes
