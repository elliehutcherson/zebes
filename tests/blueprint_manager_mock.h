#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/blueprint_manager.h"

namespace zebes {

class BlueprintManagerMock : public BlueprintManager {
 public:
  BlueprintManagerMock() : BlueprintManager("") {}

  MOCK_METHOD(absl::StatusOr<Blueprint*>, LoadBlueprint, (const std::string& path_json),
              (override));
  MOCK_METHOD(absl::Status, LoadAllBlueprints, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateBlueprint, (Blueprint blueprint), (override));
  MOCK_METHOD(absl::Status, SaveBlueprint, (Blueprint blueprint), (override));
  MOCK_METHOD(absl::StatusOr<Blueprint*>, GetBlueprint, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteBlueprint, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Blueprint>, GetAllBlueprints, (), (const, override));
  MOCK_METHOD(bool, IsSpriteUsed, (const std::string& sprite_id), (const, override));
  MOCK_METHOD(bool, IsColliderUsed, (const std::string& collider_id), (const, override));
};

}  // namespace zebes
