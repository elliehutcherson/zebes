#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "collider_manager_mock.h"
#include "resources/level_manager.h"
#include "sprite_manager_mock.h"

namespace zebes {

class LevelManagerMock : public LevelManager {
 public:
  // Using dummies to satisfy reference requirements of base class.
  // Base class only stores them, so this is safe as long as we don't access them invalidly.
  LevelManagerMock() : LevelManager(sm_dummy_, cm_dummy_, ""), sm_dummy_(), cm_dummy_() {}

  MOCK_METHOD(absl::StatusOr<Level*>, LoadLevel, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllLevels, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateLevel, (Level level), (override));
  MOCK_METHOD(absl::Status, SaveLevel, (const Level& level), (override));
  MOCK_METHOD(absl::StatusOr<Level*>, GetLevel, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteLevel, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Level>, GetAllLevels, (), (const, override));

 private:
  SpriteManagerMock sm_dummy_;
  ColliderManagerMock cm_dummy_;
};

}  // namespace zebes
