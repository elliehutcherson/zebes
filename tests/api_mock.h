#pragma once

#include "api/api.h"
#include "gmock/gmock.h"

namespace zebes {

class MockApi : public Api {
 public:
  MockApi() : Api() {}

  // Config
  MOCK_METHOD(absl::Status, SaveConfig, (const EngineConfig&), (override));

  // Textures
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTexture, (Texture), (override));
  MOCK_METHOD(absl::Status, DeleteTexture, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<std::vector<Texture>>, GetAllTextures, (), (override));
  MOCK_METHOD(absl::Status, UpdateTexture, (const Texture&), (override));
  MOCK_METHOD(absl::StatusOr<Texture*>, GetTexture, (const std::string&), (override));

  // Sprites
  MOCK_METHOD(absl::StatusOr<std::string>, CreateSprite, (Sprite), (override));
  MOCK_METHOD(absl::Status, UpdateSprite, (Sprite), (override));
  MOCK_METHOD(absl::Status, DeleteSprite, (const std::string&), (override));
  MOCK_METHOD(std::vector<Sprite>, GetAllSprites, (), (override));
  MOCK_METHOD(absl::StatusOr<Sprite*>, GetSprite, (const std::string&), (override));

  // Colliders
  MOCK_METHOD(absl::StatusOr<std::string>, CreateCollider, (Collider), (override));
  MOCK_METHOD(absl::Status, UpdateCollider, (Collider), (override));
  MOCK_METHOD(absl::Status, DeleteCollider, (const std::string&), (override));
  MOCK_METHOD(std::vector<Collider>, GetAllColliders, (), (override));
  MOCK_METHOD(absl::StatusOr<Collider*>, GetCollider, (const std::string&), (override));

  // Blueprints
  MOCK_METHOD(absl::StatusOr<std::string>, CreateBlueprint, (Blueprint), (override));
  MOCK_METHOD(absl::Status, UpdateBlueprint, (Blueprint), (override));
  MOCK_METHOD(absl::Status, DeleteBlueprint, (const std::string&), (override));
  MOCK_METHOD(std::vector<Blueprint>, GetAllBlueprints, (), (override));
  MOCK_METHOD(absl::StatusOr<Blueprint*>, GetBlueprint, (const std::string&), (override));

  // Levels
  MOCK_METHOD(absl::StatusOr<std::string>, CreateLevel, (Level), (override));
  MOCK_METHOD(absl::Status, UpdateLevel, (Level), (override));
  MOCK_METHOD(absl::Status, DeleteLevel, (const std::string&), (override));
  MOCK_METHOD(std::vector<Level>, GetAllLevels, (), (override));
  MOCK_METHOD(absl::StatusOr<Level*>, GetLevel, (const std::string&), (override));
};

}  // namespace zebes
