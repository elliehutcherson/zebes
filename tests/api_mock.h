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
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTextureFromPixels,
              (const std::string&, int, int, absl::Span<const uint8_t>), (override));
  MOCK_METHOD(absl::Status, ReplaceTexturePixels,
              (const std::string&, int, int, absl::Span<const uint8_t>), (override));
  MOCK_METHOD(absl::Status, DeleteTexture, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<std::vector<Texture>>, GetAllTextures, (), (override));
  MOCK_METHOD(absl::Status, UpdateTexture, (const Texture&), (override));
  MOCK_METHOD(absl::StatusOr<Texture*>, GetTexture, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<TextureHandle>, GetTextureHandle, (const std::string&), (override));

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

  // Tilesets
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTileset, (Tileset), (override));
  MOCK_METHOD(absl::Status, UpdateTileset, (Tileset), (override));
  MOCK_METHOD(absl::Status, DeleteTileset, (const std::string&), (override));
  MOCK_METHOD(std::vector<Tileset>, GetAllTilesets, (), (override));
  MOCK_METHOD(absl::StatusOr<Tileset*>, GetTileset, (const std::string&), (override));

  // Terrain recipes
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTerrainRecipe, (TerrainRecipe), (override));
  MOCK_METHOD(absl::Status, SaveTerrainRecipe, (const TerrainRecipe&), (override));
  MOCK_METHOD(absl::Status, DeleteTerrainRecipe, (const std::string&), (override));
  MOCK_METHOD(std::vector<TerrainRecipe>, GetAllTerrainRecipes, (), (const, override));
  MOCK_METHOD(absl::StatusOr<TerrainRecipe*>, GetTerrainRecipe, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<std::optional<TerrainRecipe>>, FindTerrainRecipeForTileset,
              (const std::string&), (override));

  // Routes every recipe call to a real manager, for tests that care whether a
  // recipe actually round-trips to disk rather than that a call was made.
  void DelegateTerrainRecipesTo(TerrainRecipeManager& recipes) {
    ON_CALL(*this, CreateTerrainRecipe).WillByDefault([&recipes](TerrainRecipe recipe) {
      return recipes.CreateRecipe(std::move(recipe));
    });
    ON_CALL(*this, SaveTerrainRecipe).WillByDefault([&recipes](const TerrainRecipe& recipe) {
      return recipes.SaveRecipe(recipe);
    });
    ON_CALL(*this, DeleteTerrainRecipe).WillByDefault([&recipes](const std::string& id) {
      return recipes.DeleteRecipe(id);
    });
    ON_CALL(*this, GetAllTerrainRecipes).WillByDefault([&recipes] {
      return recipes.GetAllRecipes();
    });
    ON_CALL(*this, GetTerrainRecipe).WillByDefault([&recipes](const std::string& id) {
      return recipes.GetRecipe(id);
    });
  }
};

}  // namespace zebes
