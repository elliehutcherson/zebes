#include "resources/animation_frame_set_recipe_manager.h"

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

constexpr char kDefinitionsPath[] = "definitions/animation_frame_set_recipes";

}  // namespace

absl::StatusOr<std::unique_ptr<AnimationFrameSetRecipeManager>>
AnimationFrameSetRecipeManager::Create(std::string root_path) {
  if (root_path.empty()) {
    return absl::InvalidArgumentError("animation frame-set recipe asset root is empty");
  }
  return std::unique_ptr<AnimationFrameSetRecipeManager>(
      new AnimationFrameSetRecipeManager(std::move(root_path)));
}

AnimationFrameSetRecipeManager::AnimationFrameSetRecipeManager(std::string root_path)
    : definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)) {}

std::string AnimationFrameSetRecipeManager::RecipePath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

absl::StatusOr<AnimationFrameSetRecipe> AnimationFrameSetRecipeManager::LoadRecipeFile(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  try {
    nlohmann::json json;
    stream >> json;
    return AnimationFrameSetRecipeFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid animation frame-set recipe JSON in ", path, ": ", error.what()));
  }
}

absl::Status AnimationFrameSetRecipeManager::LoadAllRecipes() {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create animation frame-set recipe directory: ", error.message()));
  }

  absl::flat_hash_map<std::string, std::unique_ptr<AnimationFrameSetRecipe>> loaded;
  RETURN_IF_ERROR(LoadJsonDefinitions(
      definitions_path_, "animation frame-set recipe",
      [&loaded](const std::filesystem::path& path) -> absl::Status {
        ASSIGN_OR_RETURN(AnimationFrameSetRecipe recipe, LoadRecipeFile(path.string()));
        if (!IsPathSafeResourceId(recipe.id)) {
          return absl::InvalidArgumentError("animation frame-set recipe ID is not path-safe");
        }
        if (path.stem() != recipe.id) {
          return absl::InvalidArgumentError(
              "animation frame-set recipe filename does not match its ID");
        }
        if (loaded.contains(recipe.id)) {
          return absl::AlreadyExistsError(
              absl::StrCat("duplicate animation frame-set recipe ID ", recipe.id));
        }
        const std::string id = recipe.id;
        loaded[id] = std::make_unique<AnimationFrameSetRecipe>(std::move(recipe));
        return absl::OkStatus();
      }));
  recipes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> AnimationFrameSetRecipeManager::CreateRecipe(
    AnimationFrameSetRecipe recipe) {
  recipe.id = GenerateGuid();
  RETURN_IF_ERROR(SaveRecipe(recipe));
  return recipe.id;
}

absl::Status AnimationFrameSetRecipeManager::CreateRecipeWithId(AnimationFrameSetRecipe recipe) {
  RETURN_IF_ERROR(PreflightRecipeWithId(recipe));
  return SaveRecipe(recipe);
}

absl::Status AnimationFrameSetRecipeManager::PreflightRecipeWithId(
    const AnimationFrameSetRecipe& recipe) const {
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("animation frame-set recipe ID is not path-safe");
  }
  if (recipes_.contains(recipe.id) || std::filesystem::exists(RecipePath(recipe.id))) {
    return absl::AlreadyExistsError(
        absl::StrCat("animation frame-set recipe ", recipe.id, " exists"));
  }
  return absl::OkStatus();
}

absl::Status AnimationFrameSetRecipeManager::SaveRecipe(const AnimationFrameSetRecipe& recipe) {
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(recipe));
  if (!IsPathSafeResourceId(recipe.id)) {
    return absl::InvalidArgumentError("animation frame-set recipe ID is not path-safe");
  }
  RETURN_IF_ERROR(WriteTextFileAtomically(RecipePath(recipe.id),
                                          AnimationFrameSetRecipeToJson(recipe).dump(2)));

  if (auto found = recipes_.find(recipe.id); found != recipes_.end()) {
    *found->second = recipe;
    return absl::OkStatus();
  }
  recipes_[recipe.id] = std::make_unique<AnimationFrameSetRecipe>(recipe);
  return absl::OkStatus();
}

absl::StatusOr<AnimationFrameSetRecipe*> AnimationFrameSetRecipeManager::GetRecipe(
    const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError(absl::StrCat("animation frame-set recipe ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<AnimationFrameSetRecipe> AnimationFrameSetRecipeManager::GetAllRecipes() const {
  std::vector<AnimationFrameSetRecipe> result;
  result.reserve(recipes_.size());
  for (const auto& [id, recipe] : recipes_) result.push_back(*recipe);
  std::ranges::sort(result, NamedAssetLess{});
  return result;
}

absl::Status AnimationFrameSetRecipeManager::DeleteRecipe(const std::string& id) {
  auto found = recipes_.find(id);
  if (found == recipes_.end()) {
    return absl::NotFoundError("animation frame-set recipe is not loaded");
  }
  std::error_code error;
  if (!std::filesystem::remove(RecipePath(id), error) || error) {
    return absl::InternalError(
        absl::StrCat("could not delete animation frame-set recipe: ", error.message()));
  }
  recipes_.erase(found);
  return absl::OkStatus();
}

}  // namespace zebes
