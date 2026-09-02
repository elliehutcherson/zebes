#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/animation_frame_set_recipe_manager.h"

namespace zebes {

class AnimationFrameSetRecipeManagerMock : public AnimationFrameSetRecipeManager {
 public:
  MOCK_METHOD(absl::Status, LoadAllRecipes, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateRecipe, (AnimationFrameSetRecipe), (override));
  MOCK_METHOD(absl::Status, CreateRecipeWithId, (AnimationFrameSetRecipe), (override));
  MOCK_METHOD(absl::Status, PreflightRecipeWithId, (const AnimationFrameSetRecipe&),
              (const, override));
  MOCK_METHOD(absl::Status, SaveRecipe, (const AnimationFrameSetRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<AnimationFrameSetRecipe*>, GetRecipe, (const std::string&),
              (override));
  MOCK_METHOD(std::vector<AnimationFrameSetRecipe>, GetAllRecipes, (), (const, override));
  MOCK_METHOD(absl::Status, DeleteRecipe, (const std::string&), (override));
};

}  // namespace zebes
