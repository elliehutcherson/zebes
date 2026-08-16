#include "api/api.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

// Turns a non-empty referrer list into the refusal the user sees.
//
// Deleting is blocked rather than cascaded: a dangling reference does not
// degrade a level, it stops the viewport rendering it at all, and which of the
// two the user wants is not a decision this layer can make for them. The message
// names every referrer because the whole point is telling them what to change.
absl::Status RefuseIfReferenced(std::string_view subject,
                                const std::vector<AssetReference>& referrers,
                                std::string_view hint = "") {
  if (referrers.empty()) return absl::OkStatus();
  std::string message = DescribeBlockedDeletion(subject, referrers);
  if (!hint.empty()) absl::StrAppend(&message, "\n", hint);
  return absl::FailedPreconditionError(message);
}

// True when a recipe is among the referrers, which means the blocked asset is
// half of a generated terrain rather than something the user can simply unbind.
bool AnyRecipeReferrer(const std::vector<AssetReference>& referrers) {
  for (const AssetReference& referrer : referrers) {
    if (referrer.kind == AssetKind::kTerrainRecipe) return true;
  }
  return false;
}

}  // namespace

absl::StatusOr<std::unique_ptr<Api>> Api::Create(const Options& options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("EngineConfig is null.");
  }
  if (options.texture_manager == nullptr) {
    return absl::InvalidArgumentError("TextureManager is null.");
  }
  if (options.sprite_manager == nullptr) {
    return absl::InvalidArgumentError("SpriteManager is null.");
  }
  if (options.collider_manager == nullptr) {
    return absl::InvalidArgumentError("ColliderManager is null.");
  }
  if (options.level_manager == nullptr) {
    return absl::InvalidArgumentError("LevelManager is null.");
  }
  if (options.tileset_manager == nullptr) {
    return absl::InvalidArgumentError("TilesetManager is null.");
  }
  // Checked like the rest. BlueprintManager was the one manager nobody
  // validated, so a null one reached the first call that used it instead of
  // failing here.
  if (options.blueprint_manager == nullptr) {
    return absl::InvalidArgumentError("BlueprintManager is null.");
  }
  if (options.terrain_recipe_manager == nullptr) {
    return absl::InvalidArgumentError("TerrainRecipeManager is null.");
  }
  if (options.source_artwork_manager == nullptr) {
    return absl::InvalidArgumentError("SourceArtworkManager is null.");
  }
  if (options.prop_recipe_manager == nullptr) {
    return absl::InvalidArgumentError("PropRecipeManager is null.");
  }
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options)
    : config_(*options.config),
      texture_manager_(options.texture_manager),
      sprite_manager_(options.sprite_manager),
      collider_manager_(options.collider_manager),
      blueprint_manager_(options.blueprint_manager),
      level_manager_(options.level_manager),
      tileset_manager_(options.tileset_manager),
      terrain_recipe_manager_(options.terrain_recipe_manager),
      source_artwork_manager_(options.source_artwork_manager),
      prop_recipe_manager_(options.prop_recipe_manager) {}

absl::Status Api::SaveConfig(const EngineConfig& config) {
  LOG(INFO) << "SaveConfig in the api....";
  absl::Status status = EngineConfig::Save(config);
  if (!status.ok()) return status;

  // Publish the saved settings to long-lived editor consumers. The Api holds
  // the EditorEngine-owned config by reference, so this does not introduce a
  // second source of truth.
  config_ = config;
  return absl::OkStatus();
}

absl::StatusOr<std::string> Api::CreateTexture(Texture texture) {
  // Delegate to TextureManager
  return texture_manager_->CreateTexture(texture);
}

absl::StatusOr<std::string> Api::CreateTextureFromPixels(const std::string& name, int width,
                                                         int height,
                                                         absl::Span<const uint8_t> pixels) {
  return texture_manager_->CreateTextureFromPixels(name, width, height, pixels);
}

absl::Status Api::ReplaceTexturePixels(const std::string& texture_id, int width, int height,
                                       absl::Span<const uint8_t> pixels) {
  return texture_manager_->ReplaceTexturePixels(texture_id, width, height, pixels);
}

absl::Status Api::ShowTexturePixels(const std::string& texture_id, int width, int height,
                                    absl::Span<const uint8_t> pixels) {
  return texture_manager_->ShowTexturePixels(texture_id, width, height, pixels);
}

absl::StatusOr<RgbaImage> Api::ReadTexturePixels(const std::string& texture_id) {
  return texture_manager_->ReadTexturePixels(texture_id);
}

absl::Status Api::UpdateTexture(const Texture& texture) {
  return texture_manager_->UpdateTexture(texture);
}

absl::Status Api::DeleteTexture(const std::string& texture_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("texture '", texture_id, "'"),
                                     FindTextureReferrers(catalog.View(), texture_id)));
  return texture_manager_->DeleteTexture(texture_id);
}

absl::StatusOr<std::vector<Texture>> Api::GetAllTextures() {
  return texture_manager_->GetAllTextures();
}

absl::StatusOr<TextureHandle> Api::GetTextureHandle(const std::string& texture_id) {
  return texture_manager_->GetTextureHandle(texture_id);
}

absl::StatusOr<Texture*> Api::GetTexture(const std::string& id) {
  return texture_manager_->GetTexture(id);
}

absl::StatusOr<std::string> Api::CreateSprite(Sprite sprite) {
  return sprite_manager_->CreateSprite(std::move(sprite));
}

absl::Status Api::UpdateSprite(Sprite sprite) { return sprite_manager_->SaveSprite(sprite); }

Api::CatalogSnapshot Api::SnapshotCatalog() {
  return CatalogSnapshot{
      .tilesets = tileset_manager_->GetAllTilesets(),
      .sprites = sprite_manager_->GetAllSprites(),
      .blueprints = blueprint_manager_->GetAllBlueprints(),
      .levels = level_manager_->GetAllLevels(),
      .recipes = terrain_recipe_manager_->GetAllRecipes(),
      .prop_recipes = prop_recipe_manager_->GetAllRecipes(),
  };
}

AssetCatalog Api::CatalogSnapshot::View() const {
  return AssetCatalog{tilesets, sprites, blueprints, levels, recipes, prop_recipes};
}

absl::Status Api::DeleteSprite(const std::string& sprite_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("sprite '", sprite_id, "'"),
                                     FindSpriteReferrers(catalog.View(), sprite_id)));
  return sprite_manager_->DeleteSprite(sprite_id);
}

std::vector<Sprite> Api::GetAllSprites() { return sprite_manager_->GetAllSprites(); }

absl::StatusOr<Sprite*> Api::GetSprite(const std::string& sprite_id) {
  return sprite_manager_->GetSprite(sprite_id);
}

absl::StatusOr<std::string> Api::CreateCollider(Collider collider) {
  return collider_manager_->CreateCollider(std::move(collider));
}

absl::Status Api::UpdateCollider(Collider collider) {
  return collider_manager_->SaveCollider(std::move(collider));
}

absl::Status Api::DeleteCollider(const std::string& collider_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("collider '", collider_id, "'"),
                                     FindColliderReferrers(catalog.View(), collider_id)));
  return collider_manager_->DeleteCollider(collider_id);
}

std::vector<Collider> Api::GetAllColliders() { return collider_manager_->GetAllColliders(); }

absl::StatusOr<Collider*> Api::GetCollider(const std::string& collider_id) {
  return collider_manager_->GetCollider(collider_id);
}

absl::StatusOr<std::string> Api::CreateBlueprint(Blueprint blueprint) {
  return blueprint_manager_->CreateBlueprint(std::move(blueprint));
}

absl::Status Api::UpdateBlueprint(Blueprint blueprint) {
  return blueprint_manager_->SaveBlueprint(std::move(blueprint));
}

absl::Status Api::DeleteBlueprint(const std::string& blueprint_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("blueprint '", blueprint_id, "'"),
                                     FindBlueprintReferrers(catalog.View(), blueprint_id)));
  return blueprint_manager_->DeleteBlueprint(blueprint_id);
}

std::vector<Blueprint> Api::GetAllBlueprints() { return blueprint_manager_->GetAllBlueprints(); }

absl::StatusOr<Blueprint*> Api::GetBlueprint(const std::string& blueprint_id) {
  return blueprint_manager_->GetBlueprint(blueprint_id);
}

absl::StatusOr<std::string> Api::CreateLevel(Level level) {
  return level_manager_->CreateLevel(std::move(level));
}

absl::Status Api::UpdateLevel(Level level) { return level_manager_->SaveLevel(std::move(level)); }

absl::Status Api::DeleteLevel(const std::string& level_id) {
  return level_manager_->DeleteLevel(level_id);
}

std::vector<Level> Api::GetAllLevels() { return level_manager_->GetAllLevels(); }

absl::StatusOr<Level*> Api::GetLevel(const std::string& level_id) {
  return level_manager_->GetLevel(level_id);
}

absl::StatusOr<std::string> Api::CreateTileset(Tileset tileset) {
  return tileset_manager_->CreateTileset(std::move(tileset));
}

absl::Status Api::UpdateTileset(Tileset tileset) { return tileset_manager_->SaveTileset(tileset); }

absl::Status Api::DeleteTileset(const std::string& tileset_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  const std::vector<AssetReference> referrers = FindTilesetReferrers(catalog.View(), tileset_id);
  // A recipe blocking the delete means this tileset is half of a generated
  // terrain, and there is nothing to unbind: the recipe exists to regenerate
  // exactly this tileset. Without the hint the refusal is a dead end, since
  // removing all three is a different operation in a different tab.
  RETURN_IF_ERROR(RefuseIfReferenced(
      absl::StrCat("tileset '", tileset_id, "'"), referrers,
      AnyRecipeReferrer(referrers)
          ? "This tileset was generated. Use the Terrain Editor to delete the terrain, "
            "its tileset and its artwork together."
          : ""));
  return tileset_manager_->DeleteTileset(tileset_id);
}

absl::Status Api::CheckTileDeletable(const std::string& tileset_id, int tile_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  return RefuseIfReferenced(absl::StrCat("tile ", tile_id),
                            FindTileReferrers(catalog.View(), tileset_id, tile_id));
}

std::vector<Tileset> Api::GetAllTilesets() { return tileset_manager_->GetAllTilesets(); }

absl::StatusOr<Tileset*> Api::GetTileset(const std::string& tileset_id) {
  return tileset_manager_->GetTileset(tileset_id);
}

absl::StatusOr<std::string> Api::CreateTerrainRecipe(TerrainRecipe recipe) {
  return terrain_recipe_manager_->CreateRecipe(std::move(recipe));
}

absl::Status Api::SaveTerrainRecipe(const TerrainRecipe& recipe) {
  return terrain_recipe_manager_->SaveRecipe(recipe);
}

absl::Status Api::DeleteTerrainRecipe(const std::string& recipe_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("terrain recipe '", recipe_id, "'"),
                                     FindTerrainRecipeReferrers(catalog.View(), recipe_id)));
  return terrain_recipe_manager_->DeleteRecipe(recipe_id);
}

absl::Status Api::DeleteGeneratedTerrain(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const TerrainRecipe* recipe, terrain_recipe_manager_->GetRecipe(recipe_id));
  // Copied, because deleting the recipe invalidates the pointer well before the
  // tileset and texture have been dealt with.
  const std::string name = recipe->name;
  const std::string tileset_id = recipe->tileset_id;
  const std::string texture_id = recipe->texture_id;

  // Pre-flight, because the sequence below cannot be unwound. Deleting the
  // recipe and then discovering a level is bound to the tileset would leave
  // exactly the un-regenerable pair this operation exists to prevent.
  //
  // A bundle member referencing another is not an outside reference: the recipe
  // naming the tileset, and the recipe and tileset naming the artwork, are what
  // make these three one thing.
  const CatalogSnapshot catalog = SnapshotCatalog();
  std::vector<AssetReference> outside;
  for (const AssetReference& referrer : FindTilesetReferrers(catalog.View(), tileset_id)) {
    if (referrer.kind == AssetKind::kTerrainRecipe && referrer.id == recipe_id) continue;
    outside.push_back(referrer);
  }
  for (const AssetReference& referrer : FindTextureReferrers(catalog.View(), texture_id)) {
    if (referrer.kind == AssetKind::kTerrainRecipe && referrer.id == recipe_id) continue;
    if (referrer.kind == AssetKind::kTileset && referrer.id == tileset_id) continue;
    outside.push_back(referrer);
  }
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("terrain '", name, "'"), outside));

  // Recipe, then tileset, then artwork. That order is what lets each member go
  // through its own checked delete rather than around it: the recipe is gone
  // before it can block the tileset, and both are gone before they can block the
  // texture. The bundle is an ordering over the existing guards, not a bypass.
  RETURN_IF_ERROR(DeleteTerrainRecipe(recipe_id));

  // A member already missing is not a failure. The postcondition is that none of
  // the three remain, and a half-finished bundle has to stay finishable.
  const absl::Status tileset_deleted = DeleteTileset(tileset_id);
  if (!tileset_deleted.ok() && !absl::IsNotFound(tileset_deleted)) return tileset_deleted;

  const absl::Status texture_deleted = DeleteTexture(texture_id);
  if (!texture_deleted.ok() && !absl::IsNotFound(texture_deleted)) return texture_deleted;
  return absl::OkStatus();
}

std::vector<TerrainRecipe> Api::GetAllTerrainRecipes() const {
  return terrain_recipe_manager_->GetAllRecipes();
}

absl::StatusOr<TerrainRecipe*> Api::GetTerrainRecipe(const std::string& recipe_id) {
  return terrain_recipe_manager_->GetRecipe(recipe_id);
}

absl::StatusOr<std::optional<TerrainRecipe>> Api::FindTerrainRecipeForTileset(
    const std::string& tileset_id) {
  if (tileset_id.empty()) return std::nullopt;

  std::optional<TerrainRecipe> found;
  for (TerrainRecipe& recipe : terrain_recipe_manager_->GetAllRecipes()) {
    if (recipe.tileset_id != tileset_id) continue;
    if (found.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("recipes '", found->id, "' and '", recipe.id, "' both claim tileset ",
                       tileset_id, "; which one regenerates it would be arbitrary"));
    }
    found = std::move(recipe);
  }
  return found;
}

absl::StatusOr<std::string> Api::CreateSourceArtwork(std::string name,
                                                     SourceArtworkProvenance provenance,
                                                     const RgbaImage& image) {
  return source_artwork_manager_->CreateArtwork(std::move(name), std::move(provenance), image);
}

absl::StatusOr<SourceArtwork*> Api::GetSourceArtwork(const std::string& source_artwork_id) {
  return source_artwork_manager_->GetArtwork(source_artwork_id);
}

std::vector<SourceArtwork> Api::GetAllSourceArtwork() const {
  return source_artwork_manager_->GetAllArtwork();
}

absl::StatusOr<RgbaImage> Api::ReadSourceArtworkPixels(const std::string& source_artwork_id) const {
  return source_artwork_manager_->ReadArtworkPixels(source_artwork_id);
}

absl::Status Api::DeleteSourceArtwork(const std::string& source_artwork_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(
      RefuseIfReferenced(absl::StrCat("source artwork '", source_artwork_id, "'"),
                         FindSourceArtworkReferrers(catalog.View(), source_artwork_id)));
  return source_artwork_manager_->DeleteArtwork(source_artwork_id);
}

absl::StatusOr<std::string> Api::CreatePropRecipe(PropRecipe recipe) {
  RETURN_IF_ERROR(source_artwork_manager_->GetArtwork(recipe.source_artwork_id).status());
  if (recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(terrain_recipe_manager_->GetRecipe(*recipe.terrain_recipe_id).status());
  }
  RETURN_IF_ERROR(texture_manager_->GetTexture(recipe.texture_id).status());
  RETURN_IF_ERROR(sprite_manager_->GetSprite(recipe.sprite_id).status());
  RETURN_IF_ERROR(blueprint_manager_->GetBlueprint(recipe.blueprint_id).status());
  return prop_recipe_manager_->CreateRecipe(std::move(recipe));
}

absl::Status Api::SavePropRecipe(const PropRecipe& recipe) {
  RETURN_IF_ERROR(source_artwork_manager_->GetArtwork(recipe.source_artwork_id).status());
  if (recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(terrain_recipe_manager_->GetRecipe(*recipe.terrain_recipe_id).status());
  }
  RETURN_IF_ERROR(texture_manager_->GetTexture(recipe.texture_id).status());
  RETURN_IF_ERROR(sprite_manager_->GetSprite(recipe.sprite_id).status());
  RETURN_IF_ERROR(blueprint_manager_->GetBlueprint(recipe.blueprint_id).status());
  return prop_recipe_manager_->SaveRecipe(recipe);
}

absl::StatusOr<PropRecipe*> Api::GetPropRecipe(const std::string& recipe_id) {
  return prop_recipe_manager_->GetRecipe(recipe_id);
}

std::vector<PropRecipe> Api::GetAllPropRecipes() const {
  return prop_recipe_manager_->GetAllRecipes();
}

}  // namespace zebes
