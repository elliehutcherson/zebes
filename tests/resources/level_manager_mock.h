#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/level_manager.h"

namespace zebes {

class LevelManagerMock : public LevelManager {
 public:
  LevelManagerMock() : LevelManager("") {}

  MOCK_METHOD(absl::StatusOr<Level*>, LoadLevel, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllLevels, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateLevel, (Level level), (override));
  MOCK_METHOD(absl::Status, SaveLevel, (const Level& level), (override));
  MOCK_METHOD(absl::StatusOr<Level*>, GetLevel, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteLevel, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Level>, GetAllLevels, (), (const, override));
};

}  // namespace zebes
