#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/prop_recipe.h"

namespace zebes {

class PropRecipeManager {
 public:
  static absl::StatusOr<std::unique_ptr<PropRecipeManager>> Create(std::string root_path);

  virtual ~PropRecipeManager() = default;

  virtual absl::Status LoadAllRecipes();
  virtual absl::StatusOr<std::string> CreateRecipe(PropRecipe recipe);
  virtual absl::Status CreateRecipeWithId(PropRecipe recipe);
  virtual absl::Status PreflightRecipeWithId(const PropRecipe& recipe) const;
  virtual absl::Status SaveRecipe(const PropRecipe& recipe);
  virtual absl::StatusOr<PropRecipe*> GetRecipe(const std::string& id);
  virtual std::vector<PropRecipe> GetAllRecipes() const;
  virtual absl::Status DeleteRecipe(const std::string& id);

 protected:
  PropRecipeManager() = default;

 private:
  explicit PropRecipeManager(std::string root_path);

  static absl::StatusOr<PropRecipe> LoadRecipeFile(const std::string& path);
  std::string RecipePath(const std::string& id) const;

  std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<PropRecipe>> recipes_;
};

}  // namespace zebes
