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
  MOCK_METHOD(absl::Status, ShowTexturePixels,
              (const std::string&, int, int, absl::Span<const uint8_t>), (override));
  MOCK_METHOD(absl::StatusOr<RgbaImage>, ReadTexturePixels, (const std::string&), (override));
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

  // Parallax themes
  MOCK_METHOD(absl::StatusOr<std::string>, CreateParallaxTheme, (ParallaxTheme), (override));
  MOCK_METHOD(absl::Status, UpdateParallaxTheme, (ParallaxTheme), (override));
  MOCK_METHOD(absl::Status, DeleteParallaxTheme, (const std::string&), (override));
  MOCK_METHOD(std::vector<ParallaxTheme>, GetAllParallaxThemes, (), (override));
  MOCK_METHOD(absl::StatusOr<ParallaxTheme*>, GetParallaxTheme, (const std::string&), (override));

  // Tilesets
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTileset, (Tileset), (override));
  MOCK_METHOD(absl::Status, UpdateTileset, (Tileset), (override));
  MOCK_METHOD(absl::Status, DeleteTileset, (const std::string&), (override));
  MOCK_METHOD(absl::Status, CheckTileDeletable, (const std::string&, int), (override));
  MOCK_METHOD(std::vector<Tileset>, GetAllTilesets, (), (override));
  MOCK_METHOD(absl::StatusOr<Tileset*>, GetTileset, (const std::string&), (override));

  // Terrain recipes
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTerrainRecipe, (TerrainRecipe), (override));
  MOCK_METHOD(absl::Status, SaveTerrainRecipe, (const TerrainRecipe&), (override));
  MOCK_METHOD(absl::Status, DeleteTerrainRecipe, (const std::string&), (override));
  MOCK_METHOD(absl::Status, CheckGeneratedTerrainDeletable, (const std::string&), (override));
  MOCK_METHOD(absl::Status, DeleteGeneratedTerrain, (const std::string&), (override));
  MOCK_METHOD(std::vector<TerrainRecipe>, GetAllTerrainRecipes, (), (const, override));
  MOCK_METHOD(absl::StatusOr<TerrainRecipe*>, GetTerrainRecipe, (const std::string&), (override));
  MOCK_METHOD(absl::StatusOr<std::optional<TerrainRecipe>>, FindTerrainRecipeForTileset,
              (const std::string&), (override));

  // Retained artwork sources
  MOCK_METHOD(absl::StatusOr<std::string>, CreateSourceArtwork,
              (std::string, SourceArtworkProvenance, const RgbaImage&), (override));
  MOCK_METHOD(absl::StatusOr<SourceArtwork*>, GetSourceArtwork, (const std::string&), (override));
  MOCK_METHOD(std::vector<SourceArtwork>, GetAllSourceArtwork, (), (const, override));
  MOCK_METHOD(absl::StatusOr<RgbaImage>, ReadSourceArtworkPixels, (const std::string&),
              (const, override));
  MOCK_METHOD(absl::StatusOr<RgbaImage>, ReadSourceArtworkPixels, (const std::string&, size_t),
              (const, override));
  MOCK_METHOD(absl::Status, DeleteSourceArtwork, (const std::string&), (override));

  // Generated prop recipes and bundles
  MOCK_METHOD(absl::StatusOr<std::string>, CreatePropRecipe, (PropRecipe), (override));
  MOCK_METHOD(absl::Status, SavePropRecipe, (const PropRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<PropRecipe*>, GetPropRecipe, (const std::string&), (override));
  MOCK_METHOD(std::vector<PropRecipe>, GetAllPropRecipes, (), (const, override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateGeneratedProp, (const PreparedPropAsset&),
              (override));
  MOCK_METHOD(absl::Status, CheckGeneratedPropDeletable, (const std::string&), (override));
  MOCK_METHOD(absl::Status, DeleteGeneratedProp, (const std::string&), (override));
  MOCK_METHOD(absl::Status, RegenerateGeneratedProp, (const PreparedPropRegeneration&), (override));

  // Imported and manually authored animation frame sets
  MOCK_METHOD(absl::StatusOr<AnimationFrameSetRecipe*>, GetAnimationFrameSetRecipe,
              (const std::string&), (override));
  MOCK_METHOD(std::vector<AnimationFrameSetRecipe>, GetAllAnimationFrameSetRecipes, (),
              (const, override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateAnimationFrameSet,
              (const PreparedAnimationFrameSetAsset&), (override));
  MOCK_METHOD(absl::Status, RegenerateAnimationFrameSet,
              (const PreparedAnimationFrameSetRegeneration&), (override));
  MOCK_METHOD(absl::Status, CheckAnimationFrameSetDeletable, (const std::string&), (override));
  MOCK_METHOD(absl::Status, DeleteAnimationFrameSet, (const PreparedAnimationFrameSetDeletion&),
              (override));

  // Generated parallax artwork recipes and bundles
  MOCK_METHOD(absl::StatusOr<std::string>, CreateParallaxArtworkRecipe, (ParallaxArtworkRecipe),
              (override));
  MOCK_METHOD(absl::Status, SaveParallaxArtworkRecipe, (const ParallaxArtworkRecipe&), (override));
  MOCK_METHOD(absl::StatusOr<ParallaxArtworkRecipe*>, GetParallaxArtworkRecipe,
              (const std::string&), (override));
  MOCK_METHOD(std::vector<ParallaxArtworkRecipe>, GetAllParallaxArtworkRecipes, (),
              (const, override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateGeneratedParallaxArtwork,
              (const PreparedParallaxArtworkAsset&), (override));
  MOCK_METHOD(absl::Status, RenameGeneratedParallaxArtwork,
              (const std::string&, const std::string&), (override));
  MOCK_METHOD(absl::Status, CheckGeneratedParallaxArtworkDeletable, (const std::string&),
              (override));
  MOCK_METHOD(absl::Status, DeleteGeneratedParallaxArtwork, (const std::string&), (override));
  MOCK_METHOD(absl::Status, RegenerateGeneratedParallaxArtwork,
              (const PreparedParallaxArtworkRegeneration&), (override));
  MOCK_METHOD(absl::Status, RedrawGeneratedParallaxArtwork, (const PreparedParallaxArtworkRedraw&),
              (override));

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
