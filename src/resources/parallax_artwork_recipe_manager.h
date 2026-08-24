#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_recipe.h"

namespace zebes {

class ParallaxArtworkRecipeManager {
 public:
  static absl::StatusOr<std::unique_ptr<ParallaxArtworkRecipeManager>> Create(
      std::string root_path);

  virtual ~ParallaxArtworkRecipeManager() = default;

  virtual absl::Status LoadAllRecipes();
  virtual absl::StatusOr<std::string> CreateRecipe(ParallaxArtworkRecipe recipe);
  virtual absl::Status CreateRecipeWithId(ParallaxArtworkRecipe recipe);
  virtual absl::Status PreflightRecipeWithId(const ParallaxArtworkRecipe& recipe) const;
  virtual absl::Status SaveRecipe(const ParallaxArtworkRecipe& recipe);
  virtual absl::StatusOr<ParallaxArtworkRecipe*> GetRecipe(const std::string& id);
  virtual std::vector<ParallaxArtworkRecipe> GetAllRecipes() const;
  virtual absl::Status DeleteRecipe(const std::string& id);

 protected:
  ParallaxArtworkRecipeManager() = default;

 private:
  explicit ParallaxArtworkRecipeManager(std::string root_path);

  static absl::StatusOr<ParallaxArtworkRecipe> LoadRecipeFile(const std::string& path);
  std::string RecipePath(const std::string& id) const;

  std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<ParallaxArtworkRecipe>> recipes_;
};

}  // namespace zebes
