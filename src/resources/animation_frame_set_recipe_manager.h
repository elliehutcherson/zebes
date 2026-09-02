#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_recipe.h"

namespace zebes {

class AnimationFrameSetRecipeManager {
 public:
  static absl::StatusOr<std::unique_ptr<AnimationFrameSetRecipeManager>> Create(
      std::string root_path);

  virtual ~AnimationFrameSetRecipeManager() = default;

  virtual absl::Status LoadAllRecipes();
  virtual absl::StatusOr<std::string> CreateRecipe(AnimationFrameSetRecipe recipe);
  virtual absl::Status CreateRecipeWithId(AnimationFrameSetRecipe recipe);
  virtual absl::Status PreflightRecipeWithId(const AnimationFrameSetRecipe& recipe) const;
  virtual absl::Status SaveRecipe(const AnimationFrameSetRecipe& recipe);
  virtual absl::StatusOr<AnimationFrameSetRecipe*> GetRecipe(const std::string& id);
  virtual std::vector<AnimationFrameSetRecipe> GetAllRecipes() const;
  virtual absl::Status DeleteRecipe(const std::string& id);

 protected:
  AnimationFrameSetRecipeManager() = default;

 private:
  explicit AnimationFrameSetRecipeManager(std::string root_path);

  static absl::StatusOr<AnimationFrameSetRecipe> LoadRecipeFile(const std::string& path);
  std::string RecipePath(const std::string& id) const;

  std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<AnimationFrameSetRecipe>> recipes_;
};

}  // namespace zebes
