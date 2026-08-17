#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "artwork/prepare_prop_asset.h"
#include "artwork/prop_recipe.h"
#include "artwork/regenerate_prop_asset.h"
#include "artwork/source_artwork.h"
#include "common/config.h"
#include "engine/texture_handle.h"
#include "objects/texture.h"
#include "resources/asset_references.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/prop_recipe_manager.h"
#include "resources/source_artwork_manager.h"
#include "resources/sprite_manager.h"
#include "resources/terrain_recipe_manager.h"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"

namespace zebes {

class Api {
 public:
  struct Options {
    EngineConfig* config;
    TextureManager* texture_manager;
    SpriteManager* sprite_manager;
    ColliderManager* collider_manager;
    BlueprintManager* blueprint_manager;
    LevelManager* level_manager;
    TilesetManager* tileset_manager;
    TerrainRecipeManager* terrain_recipe_manager;
    SourceArtworkManager* source_artwork_manager;
    PropRecipeManager* prop_recipe_manager;
  };

  static absl::StatusOr<std::unique_ptr<Api>> Create(const Options& options);

  explicit Api(const Options& options);
  virtual ~Api() = default;

  // Get reading access to the config
  const EngineConfig* GetConfig() const { return &config_; }

  // Save the config to disk
  virtual absl::Status SaveConfig(const EngineConfig& config);

  virtual absl::StatusOr<std::string> CreateTexture(Texture texture);
  // Registers artwork the editor generated rather than loaded from disk. See
  // TextureManager::CreateTextureFromPixels.
  virtual absl::StatusOr<std::string> CreateTextureFromPixels(const std::string& name, int width,
                                                              int height,
                                                              absl::Span<const uint8_t> pixels);
  virtual absl::Status ReplaceTexturePixels(const std::string& texture_id, int width, int height,
                                            absl::Span<const uint8_t> pixels);
  // Makes artwork visible without making it durable. See
  // TextureManager::ShowTexturePixels: a derived terrain's atlas grows while a
  // level is painted, and the paint is not saved until the level is.
  virtual absl::Status ShowTexturePixels(const std::string& texture_id, int width, int height,
                                         absl::Span<const uint8_t> pixels);
  // Decodes a texture's artwork back off disk. See
  // TextureManager::ReadTexturePixels.
  virtual absl::StatusOr<RgbaImage> ReadTexturePixels(const std::string& texture_id);
  virtual absl::Status DeleteTexture(const std::string& texture_id);
  virtual absl::StatusOr<std::vector<Texture>> GetAllTextures();
  virtual absl::Status UpdateTexture(const Texture& texture);
  virtual absl::StatusOr<Texture*> GetTexture(const std::string& sprite_id);
  // Runtime GPU handle for a texture. Definitions no longer carry one, so
  // rendering paths resolve it by ID at the point of use.
  virtual absl::StatusOr<TextureHandle> GetTextureHandle(const std::string& texture_id);

  virtual absl::StatusOr<std::string> CreateSprite(Sprite sprite);
  virtual absl::Status UpdateSprite(Sprite sprite);
  virtual absl::Status DeleteSprite(const std::string& sprite_id);
  virtual std::vector<Sprite> GetAllSprites();
  virtual absl::StatusOr<Sprite*> GetSprite(const std::string& sprite_id);

  virtual absl::StatusOr<std::string> CreateCollider(Collider collider);
  virtual absl::Status UpdateCollider(Collider collider);
  virtual absl::Status DeleteCollider(const std::string& collider_id);
  virtual std::vector<Collider> GetAllColliders();
  virtual absl::StatusOr<Collider*> GetCollider(const std::string& collider_id);

  virtual absl::StatusOr<std::string> CreateBlueprint(Blueprint blueprint);
  virtual absl::Status UpdateBlueprint(Blueprint blueprint);
  virtual absl::Status DeleteBlueprint(const std::string& blueprint_id);
  virtual std::vector<Blueprint> GetAllBlueprints();
  virtual absl::StatusOr<Blueprint*> GetBlueprint(const std::string& blueprint_id);

  virtual absl::StatusOr<std::string> CreateLevel(Level level);
  virtual absl::Status UpdateLevel(Level level);
  virtual absl::Status DeleteLevel(const std::string& level_id);
  virtual std::vector<Level> GetAllLevels();
  virtual absl::StatusOr<Level*> GetLevel(const std::string& level_id);

  virtual absl::StatusOr<std::string> CreateTileset(Tileset tileset);
  virtual absl::Status UpdateTileset(Tileset tileset);
  virtual absl::Status DeleteTileset(const std::string& tileset_id);
  // Refuses when a level has painted this tile, naming the levels that did.
  //
  // A tile is not deleted through the Api -- it lives inside the tileset copy
  // the editor is editing -- so unlike the Delete methods this is the check on
  // its own, and the caller applies the removal once it passes.
  virtual absl::Status CheckTileDeletable(const std::string& tileset_id, int tile_id);
  virtual std::vector<Tileset> GetAllTilesets();
  virtual absl::StatusOr<Tileset*> GetTileset(const std::string& tileset_id);

  // Authoring state for generated terrain: the complete TerrainGenConfig that
  // produced a tileset, plus the IDs it produced.
  //
  // Both editors need it and for different reasons -- the Terrain tab to reopen
  // a recipe, the Level tab to render artwork a level asks for that the atlas
  // does not hold yet -- so it belongs here beside the other managers rather
  // than being constructed wherever it is first wanted. Two managers over one
  // directory would keep two caches and disagree about what is on disk.
  virtual absl::StatusOr<std::string> CreateTerrainRecipe(TerrainRecipe recipe);
  virtual absl::Status SaveTerrainRecipe(const TerrainRecipe& recipe);
  virtual absl::Status DeleteTerrainRecipe(const std::string& recipe_id);
  // Removes a generated terrain whole: its recipe, the tileset it produced, and
  // the artwork behind that tileset.
  //
  // The three are one build product with three records, so they go together or
  // not at all. Deleting only the recipe would leave a tileset nothing can
  // regenerate, which is the state this exists to stop anyone reaching.
  //
  // Refuses, changing nothing, when anything outside the bundle still references
  // a member -- a level bound to the tileset, or a sprite or parallax layer
  // naming the artwork.
  virtual absl::Status DeleteGeneratedTerrain(const std::string& recipe_id);
  virtual std::vector<TerrainRecipe> GetAllTerrainRecipes() const;
  virtual absl::StatusOr<TerrainRecipe*> GetTerrainRecipe(const std::string& recipe_id);

  // The recipe that produced a tileset, or nullopt when it was not generated.
  // A tileset carries no back-reference, so this is a scan; there are tens of
  // recipes, and caching a reverse index would be a second thing to keep true.
  virtual absl::StatusOr<std::optional<TerrainRecipe>> FindTerrainRecipeForTileset(
      const std::string& tileset_id);

  virtual absl::StatusOr<std::string> CreateSourceArtwork(std::string name,
                                                          SourceArtworkProvenance provenance,
                                                          const RgbaImage& image);
  virtual absl::StatusOr<SourceArtwork*> GetSourceArtwork(const std::string& source_artwork_id);
  virtual std::vector<SourceArtwork> GetAllSourceArtwork() const;
  virtual absl::StatusOr<RgbaImage> ReadSourceArtworkPixels(
      const std::string& source_artwork_id) const;
  virtual absl::Status DeleteSourceArtwork(const std::string& source_artwork_id);

  // Prop recipes are exposed for creation, regeneration edits, and lookup.
  // Their deletion is intentionally reserved for the generated-prop bundle
  // operation so artwork outputs cannot be orphaned by deleting only a recipe.
  virtual absl::StatusOr<std::string> CreatePropRecipe(PropRecipe recipe);
  virtual absl::Status SavePropRecipe(const PropRecipe& recipe);
  virtual absl::StatusOr<PropRecipe*> GetPropRecipe(const std::string& recipe_id);
  virtual std::vector<PropRecipe> GetAllPropRecipes() const;

  // Publishes a fully prepared prop in dependency order. The recipe is written
  // last, so catalogue visibility means every runtime dependency exists.
  // Failures compensate already-created members in reverse order.
  virtual absl::StatusOr<std::string> CreateGeneratedProp(const PreparedPropAsset& prepared);

  // Removes the recipe-owned runtime bundle after proving that no level or
  // unrelated definition still references an output. Unshared retained source
  // artwork is removed last; shared source artwork remains available.
  virtual absl::Status DeleteGeneratedProp(const std::string& recipe_id);

  // Commits a worker-prepared redraw after proving every recipe-owned input is
  // still the exact snapshot that was processed. Blueprint content is only
  // checked for existence and is never overwritten.
  virtual absl::Status RegenerateGeneratedProp(const PreparedPropRegeneration& prepared);

 protected:
  // Allow default construction for mocks
  Api()
      : config_(*(new EngineConfig())),
        texture_manager_(nullptr),
        sprite_manager_(nullptr),
        collider_manager_(nullptr),
        blueprint_manager_(nullptr),
        level_manager_(nullptr),
        tileset_manager_(nullptr),
        terrain_recipe_manager_(nullptr),
        source_artwork_manager_(nullptr),
        prop_recipe_manager_(nullptr) {}

 private:
  // Every catalogue a reference can live in, read once for one deletion check.
  //
  // Owned rather than viewed because the managers hand back copies: AssetCatalog
  // holds references, so something has to keep the vectors alive for the length
  // of the scan.
  struct CatalogSnapshot {
    std::vector<Tileset> tilesets;
    std::vector<Sprite> sprites;
    std::vector<Blueprint> blueprints;
    std::vector<Level> levels;
    std::vector<TerrainRecipe> recipes;
    std::vector<PropRecipe> prop_recipes;

    AssetCatalog View() const;
  };

  // Reads every catalogue. Only sound because LoadAll* reports the files it
  // could not read: a catalogue with silent holes would let a scan approve
  // deleting something the unreadable definition still references.
  CatalogSnapshot SnapshotCatalog();

  EngineConfig& config_;
  TextureManager* texture_manager_;
  SpriteManager* sprite_manager_;
  ColliderManager* collider_manager_;
  BlueprintManager* blueprint_manager_;
  LevelManager* level_manager_;
  TilesetManager* tileset_manager_;
  TerrainRecipeManager* terrain_recipe_manager_;
  SourceArtworkManager* source_artwork_manager_;
  PropRecipeManager* prop_recipe_manager_;
};

}  // namespace zebes
