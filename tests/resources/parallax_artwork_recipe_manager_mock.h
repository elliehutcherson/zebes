#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/parallax_artwork_recipe_manager.h"

namespace zebes {

class ParallaxArtworkRecipeManagerMock : public ParallaxArtworkRecipeManager {
 public:
  MOCK_METHOD(absl::Status, LoadAllRecipes, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateRecipe, (ParallaxArtworkRecipe), (override));
  MOCK_METHOD(absl::Status, CreateRecipeWithId, (ParallaxArtworkRecipe), (override));
  MOCK_METHOD(absl::Status, PreflightRecipeWithId, (const ParallaxArtworkRecipe&),
              (const, override));
  MOCK_METHOD(absl::Status, SaveRecipe, (const ParallaxArtworkRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<ParallaxArtworkRecipe*>, GetRecipe, (const std::string&), (override));
  MOCK_METHOD(std::vector<ParallaxArtworkRecipe>, GetAllRecipes, (), (const, override));
  MOCK_METHOD(absl::Status, DeleteRecipe, (const std::string&), (override));
};

}  // namespace zebes
