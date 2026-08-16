#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/prop_recipe_manager.h"

namespace zebes {

class PropRecipeManagerMock : public PropRecipeManager {
 public:
  MOCK_METHOD(absl::Status, LoadAllRecipes, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateRecipe, (PropRecipe), (override));
  MOCK_METHOD(absl::Status, SaveRecipe, (const PropRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<PropRecipe*>, GetRecipe, (const std::string&), (override));
  MOCK_METHOD(std::vector<PropRecipe>, GetAllRecipes, (), (const, override));
  MOCK_METHOD(absl::Status, DeleteRecipe, (const std::string&), (override));
};

}  // namespace zebes
