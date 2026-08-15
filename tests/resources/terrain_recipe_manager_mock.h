#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/terrain_recipe_manager.h"

namespace zebes {

class TerrainRecipeManagerMock : public TerrainRecipeManager {
 public:
  MOCK_METHOD(absl::Status, LoadAllRecipes, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateRecipe, (TerrainRecipe), (override));
  MOCK_METHOD(absl::Status, SaveRecipe, (const TerrainRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<TerrainRecipe*>, GetRecipe, (const std::string&), (override));
  MOCK_METHOD(std::vector<TerrainRecipe>, GetAllRecipes, (), (const, override));
  MOCK_METHOD(absl::Status, DeleteRecipe, (const std::string&), (override));
};

}  // namespace zebes
