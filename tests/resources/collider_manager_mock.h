#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/collider_manager.h"

namespace zebes {

class ColliderManagerMock : public ColliderManager {
 public:
  ColliderManagerMock() : ColliderManager("") {}

  MOCK_METHOD(absl::StatusOr<Collider*>, LoadCollider, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllColliders, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateCollider, (Collider collider), (override));
  MOCK_METHOD(absl::Status, SaveCollider, (Collider collider), (override));
  MOCK_METHOD(absl::StatusOr<Collider*>, GetCollider, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteCollider, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Collider>, GetAllColliders, (), (const, override));
};

}  // namespace zebes
