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

  absl::Status LoadAllRecipes();
  absl::StatusOr<std::string> CreateRecipe(TerrainRecipe recipe);
  absl::Status SaveRecipe(const TerrainRecipe& recipe);
  absl::StatusOr<TerrainRecipe*> GetRecipe(const std::string& id);
  std::vector<TerrainRecipe> GetAllRecipes() const;
  absl::Status DeleteRecipe(const std::string& id);

 private:
  explicit TerrainRecipeManager(std::string root_path);

  absl::StatusOr<TerrainRecipe> LoadRecipeFile(const std::string& path) const;
  std::string RecipePath(const std::string& id) const;

  const std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<TerrainRecipe>> recipes_;
};

}  // namespace zebes
