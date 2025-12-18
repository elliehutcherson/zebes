#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/sprite_manager.h"

namespace zebes {

class SpriteManagerMock : public SpriteManager {
 public:
  SpriteManagerMock() : SpriteManager(nullptr, "") {}

  MOCK_METHOD(absl::StatusOr<Sprite*>, LoadSprite, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllSprites, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateSprite, (Sprite sprite), (override));
  MOCK_METHOD(absl::Status, SaveSprite, (Sprite sprite), (override));
  MOCK_METHOD(absl::StatusOr<Sprite*>, GetSprite, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteSprite, (const std::string& id), (override));
  MOCK_METHOD(bool, IsTextureUsed, (const std::string& texture_id), (const, override));
  MOCK_METHOD(std::vector<Sprite>, GetAllSprites, (), (const, override));
};

}  // namespace zebes
