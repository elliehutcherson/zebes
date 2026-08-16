#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

// Persists generated-terrain authoring recipes under
// definitions/terrain_recipes. Files are keyed by immutable IDs rather than
// display names, so renaming cannot create unsafe or ambiguous paths.
class TerrainRecipeManager {
 public:
  static absl::StatusOr<std::unique_ptr<TerrainRecipeManager>> Create(std::string root_path);

  virtual ~TerrainRecipeManager() = default;

  virtual absl::Status LoadAllRecipes();
  virtual absl::StatusOr<std::string> CreateRecipe(TerrainRecipe recipe);
  virtual absl::Status SaveRecipe(const TerrainRecipe& recipe);
  virtual absl::StatusOr<TerrainRecipe*> GetRecipe(const std::string& id);
  virtual std::vector<TerrainRecipe> GetAllRecipes() const;
  virtual absl::Status DeleteRecipe(const std::string& id);

 protected:
  // Mocks need a default; every other manager in this directory is virtual and
  // has one, and this was the only one nothing depended on hard enough to say.
  TerrainRecipeManager() = default;

 private:
  explicit TerrainRecipeManager(std::string root_path);

  static absl::StatusOr<TerrainRecipe> LoadRecipeFile(const std::string& path);
  std::string RecipePath(const std::string& id) const;

  const std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<TerrainRecipe>> recipes_;
};

}  // namespace zebes
