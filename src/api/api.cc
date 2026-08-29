#include "api/api.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

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

absl::Status ValidateThemeTextures(Api& api, const ParallaxTheme& theme) {
  RETURN_IF_ERROR(ValidateParallaxTheme(theme));
  for (const ParallaxLayer& layer : theme.layers) {
    for (const ParallaxElement& element : layer.elements) {
      absl::StatusOr<Texture*> texture = api.GetTexture(element.texture_id);
      if (!texture.ok() || *texture == nullptr) {
        return absl::FailedPreconditionError(absl::StrCat("Parallax element '", element.name,
                                                          "' references missing texture '",
                                                          element.texture_id, "'."));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateLevelThemes(Api& api, const Level& level) {
  RETURN_IF_ERROR(ValidateLevel(level));
  for (const ParallaxZone& zone : level.zones) {
    absl::StatusOr<ParallaxTheme*> theme = api.GetParallaxTheme(zone.theme_id);
    if (!theme.ok() || *theme == nullptr) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Parallax zone '", zone.name, "' references missing theme '", zone.theme_id, "'."));
    }
  }
  return absl::OkStatus();
}

absl::Status RequireAbsent(const absl::Status& lookup, std::string_view kind, std::string_view id) {
  if (absl::IsNotFound(lookup)) return absl::OkStatus();
  if (lookup.ok()) {
    return absl::AlreadyExistsError(absl::StrCat(kind, " '", id, "' already exists"));
  }
  return absl::Status(
      lookup.code(), absl::StrCat("could not preflight ", kind, " '", id, "': ", lookup.message()));
}

class CompensationFailures {
 public:
  void Add(std::string_view action, const absl::Status& status) {
    if (status.ok() || absl::IsNotFound(status)) return;
    failures_.push_back(absl::StrCat(action, ": ", status.message()));
  }

  absl::Status Report(const absl::Status& primary) const {
    if (failures_.empty()) return primary;
    return absl::Status(primary.code(),
                        absl::StrCat(primary.message(), "; compensation also failed: ",
                                     absl::StrJoin(failures_, "; ")));
  }

 private:
  std::vector<std::string> failures_;
};

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
  if (options.parallax_theme_manager == nullptr) {
    return absl::InvalidArgumentError("ParallaxThemeManager is null.");
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
  if (options.parallax_artwork_recipe_manager == nullptr) {
    return absl::InvalidArgumentError("ParallaxArtworkRecipeManager is null.");
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
      parallax_theme_manager_(options.parallax_theme_manager),
      tileset_manager_(options.tileset_manager),
      terrain_recipe_manager_(options.terrain_recipe_manager),
      source_artwork_manager_(options.source_artwork_manager),
      prop_recipe_manager_(options.prop_recipe_manager),
      parallax_artwork_recipe_manager_(options.parallax_artwork_recipe_manager) {}

absl::Status Api::SaveConfig(const EngineConfig& config) {
  LOG(INFO) << "SaveConfig in the api....";
  RETURN_IF_ERROR(EngineConfig::Save(config));

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
      .parallax_themes = parallax_theme_manager_->GetAllThemes(),
      .recipes = terrain_recipe_manager_->GetAllRecipes(),
      .prop_recipes = prop_recipe_manager_->GetAllRecipes(),
      .parallax_artwork_recipes = parallax_artwork_recipe_manager_->GetAllRecipes(),
  };
}

AssetCatalog Api::CatalogSnapshot::View() const {
  return AssetCatalog{tilesets,        sprites, blueprints,   levels,
                      parallax_themes, recipes, prop_recipes, parallax_artwork_recipes};
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
  Level validation_level = level;
  validation_level.id = "new-level";
  RETURN_IF_ERROR(ValidateLevelThemes(*this, validation_level));
  return level_manager_->CreateLevel(std::move(level));
}

absl::Status Api::UpdateLevel(Level level) {
  RETURN_IF_ERROR(ValidateLevelThemes(*this, level));
  return level_manager_->SaveLevel(std::move(level));
}

absl::Status Api::DeleteLevel(const std::string& level_id) {
  return level_manager_->DeleteLevel(level_id);
}

std::vector<Level> Api::GetAllLevels() { return level_manager_->GetAllLevels(); }

absl::StatusOr<Level*> Api::GetLevel(const std::string& level_id) {
  return level_manager_->GetLevel(level_id);
}

absl::StatusOr<std::string> Api::CreateParallaxTheme(ParallaxTheme theme) {
  // The manager assigns identity, but the rest of the definition can be
  // validated before any file is written.
  ParallaxTheme validation_theme = theme;
  validation_theme.id = "new-theme";
  RETURN_IF_ERROR(ValidateThemeTextures(*this, validation_theme));
  return parallax_theme_manager_->CreateTheme(std::move(theme));
}

absl::Status Api::UpdateParallaxTheme(ParallaxTheme theme) {
  RETURN_IF_ERROR(ValidateThemeTextures(*this, theme));
  RETURN_IF_ERROR(parallax_theme_manager_->GetTheme(theme.id).status());
  return parallax_theme_manager_->SaveTheme(theme);
}

absl::Status Api::DeleteParallaxTheme(const std::string& theme_id) {
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(RefuseIfReferenced(absl::StrCat("parallax theme '", theme_id, "'"),
                                     FindParallaxThemeReferrers(catalog.View(), theme_id)));
  return parallax_theme_manager_->DeleteTheme(theme_id);
}

std::vector<ParallaxTheme> Api::GetAllParallaxThemes() {
  return parallax_theme_manager_->GetAllThemes();
}

absl::StatusOr<ParallaxTheme*> Api::GetParallaxTheme(const std::string& theme_id) {
  return parallax_theme_manager_->GetTheme(theme_id);
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

absl::Status Api::CheckGeneratedTerrainDeletable(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const TerrainRecipe* recipe, terrain_recipe_manager_->GetRecipe(recipe_id));
  const CatalogSnapshot catalog = SnapshotCatalog();
  return CheckGeneratedTerrainDeletable(*recipe, catalog);
}

absl::Status Api::CheckGeneratedTerrainDeletable(const TerrainRecipe& recipe,
                                                 const CatalogSnapshot& catalog) {
  const std::string& name = recipe.name;
  const std::string tileset_id = recipe.tileset_id;
  const std::string texture_id = recipe.texture_id;

  // Pre-flight, because the sequence below cannot be unwound. Deleting the
  // recipe and then discovering a level is bound to the tileset would leave
  // exactly the un-regenerable pair this operation exists to prevent.
  //
  // A bundle member referencing another is not an outside reference: the recipe
  // naming the tileset, and the recipe and tileset naming the artwork, are what
  // make these three one thing.
  std::vector<AssetReference> outside;
  for (const AssetReference& referrer : FindTerrainRecipeReferrers(catalog.View(), recipe.id)) {
    outside.push_back(referrer);
  }
  for (const AssetReference& referrer : FindTilesetReferrers(catalog.View(), tileset_id)) {
    if (referrer.kind == AssetKind::kTerrainRecipe && referrer.id == recipe.id) continue;
    outside.push_back(referrer);
  }
  for (const AssetReference& referrer : FindTextureReferrers(catalog.View(), texture_id)) {
    if (referrer.kind == AssetKind::kTerrainRecipe && referrer.id == recipe.id) continue;
    if (referrer.kind == AssetKind::kTileset && referrer.id == tileset_id) continue;
    outside.push_back(referrer);
  }
  return RefuseIfReferenced(absl::StrCat("terrain '", name, "'"), outside);
}

absl::Status Api::DeleteGeneratedTerrain(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const TerrainRecipe* recipe, terrain_recipe_manager_->GetRecipe(recipe_id));
  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(CheckGeneratedTerrainDeletable(*recipe, catalog));
  // Copied, because deleting the recipe invalidates the pointer well before the
  // tileset and texture have been dealt with.
  const std::string tileset_id = recipe->tileset_id;
  const std::string texture_id = recipe->texture_id;

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

absl::StatusOr<std::string> Api::CreateGeneratedProp(const PreparedPropAsset& prepared) {
  RETURN_IF_ERROR(ValidatePreparedPropAsset(prepared));

  ASSIGN_OR_RETURN(SourceArtwork * current_source,
                   source_artwork_manager_->GetArtwork(prepared.source.id));
  if (current_source->content_digest != prepared.source.content_digest ||
      current_source->width != prepared.source.width ||
      current_source->height != prepared.source.height) {
    return absl::FailedPreconditionError(
        "accepted source artwork changed after this prop was prepared");
  }
  if (prepared.recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(
        terrain_recipe_manager_->GetRecipe(*prepared.recipe.terrain_recipe_id).status());
  }

  RETURN_IF_ERROR(RequireAbsent(texture_manager_->GetTexture(prepared.texture.id).status(),
                                "texture", prepared.texture.id));
  RETURN_IF_ERROR(RequireAbsent(sprite_manager_->GetSprite(prepared.sprite.id).status(), "sprite",
                                prepared.sprite.id));
  RETURN_IF_ERROR(RequireAbsent(blueprint_manager_->GetBlueprint(prepared.blueprint.id).status(),
                                "blueprint", prepared.blueprint.id));
  RETURN_IF_ERROR(RequireAbsent(prop_recipe_manager_->GetRecipe(prepared.recipe.id).status(),
                                "prop recipe", prepared.recipe.id));
  RETURN_IF_ERROR(texture_manager_->PreflightGeneratedTexture(prepared.texture));
  RETURN_IF_ERROR(sprite_manager_->PreflightSpriteWithId(prepared.sprite));
  RETURN_IF_ERROR(blueprint_manager_->PreflightBlueprintWithId(prepared.blueprint));
  RETURN_IF_ERROR(prop_recipe_manager_->PreflightRecipeWithId(prepared.recipe));

  const RgbaImage& image = prepared.artwork.finished.image;
  RETURN_IF_ERROR(texture_manager_->CreateGeneratedTexture(prepared.texture, image.width,
                                                           image.height, image.pixels));

  absl::Status status = sprite_manager_->CreateSpriteWithId(prepared.sprite);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("delete texture", texture_manager_->DeleteTexture(prepared.texture.id));
    return compensation.Report(status);
  }

  status = blueprint_manager_->CreateBlueprintWithId(prepared.blueprint);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("delete sprite", sprite_manager_->DeleteSprite(prepared.sprite.id));
    compensation.Add("delete texture", texture_manager_->DeleteTexture(prepared.texture.id));
    return compensation.Report(status);
  }

  status = prop_recipe_manager_->CreateRecipeWithId(prepared.recipe);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("delete blueprint",
                     blueprint_manager_->DeleteBlueprint(prepared.blueprint.id));
    compensation.Add("delete sprite", sprite_manager_->DeleteSprite(prepared.sprite.id));
    compensation.Add("delete texture", texture_manager_->DeleteTexture(prepared.texture.id));
    return compensation.Report(status);
  }
  return prepared.recipe.id;
}

absl::Status Api::CheckGeneratedPropDeletable(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const PropRecipe* loaded, prop_recipe_manager_->GetRecipe(recipe_id));
  const CatalogSnapshot catalog = SnapshotCatalog();
  return CheckGeneratedPropDeletable(*loaded, catalog);
}

absl::Status Api::CheckGeneratedPropDeletable(const PropRecipe& recipe,
                                              const CatalogSnapshot& catalog) {
  std::vector<AssetReference> outside;
  for (const AssetReference& referrer :
       FindBlueprintReferrers(catalog.View(), recipe.blueprint_id)) {
    if (referrer.kind == AssetKind::kPropRecipe && referrer.id == recipe.id) continue;
    outside.push_back(referrer);
  }
  for (const AssetReference& referrer : FindSpriteReferrers(catalog.View(), recipe.sprite_id)) {
    if (referrer.kind == AssetKind::kPropRecipe && referrer.id == recipe.id) continue;
    if (referrer.kind == AssetKind::kBlueprint && referrer.id == recipe.blueprint_id) continue;
    outside.push_back(referrer);
  }
  for (const AssetReference& referrer : FindTextureReferrers(catalog.View(), recipe.texture_id)) {
    if (referrer.kind == AssetKind::kPropRecipe && referrer.id == recipe.id) continue;
    if (referrer.kind == AssetKind::kSprite && referrer.id == recipe.sprite_id) continue;
    outside.push_back(referrer);
  }
  return RefuseIfReferenced(absl::StrCat("generated prop '", recipe.name, "'"), outside);
}

absl::Status Api::DeleteGeneratedProp(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const PropRecipe* loaded, prop_recipe_manager_->GetRecipe(recipe_id));
  const PropRecipe recipe = *loaded;

  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(CheckGeneratedPropDeletable(recipe, catalog));

  bool source_is_shared = false;
  for (const AssetReference& referrer :
       FindSourceArtworkReferrers(catalog.View(), recipe.source_artwork_id)) {
    if (referrer.kind != AssetKind::kPropRecipe || referrer.id != recipe.id) {
      source_is_shared = true;
      break;
    }
  }

  // Keep the recipe until every output is gone. It is the recovery record for
  // a partially completed delete: if an I/O failure interrupts this sequence,
  // retrying with the same recipe can finish removing members already absent.
  // The external-reference scan above is what makes direct manager deletion
  // safe here; ordinary one-off deletes still go through their Api guards.
  const absl::Status blueprint_deleted = blueprint_manager_->DeleteBlueprint(recipe.blueprint_id);
  if (!blueprint_deleted.ok() && !absl::IsNotFound(blueprint_deleted)) return blueprint_deleted;

  const absl::Status sprite_deleted = sprite_manager_->DeleteSprite(recipe.sprite_id);
  if (!sprite_deleted.ok() && !absl::IsNotFound(sprite_deleted)) return sprite_deleted;

  const absl::Status texture_deleted = texture_manager_->DeleteTexture(recipe.texture_id);
  if (!texture_deleted.ok() && !absl::IsNotFound(texture_deleted)) return texture_deleted;

  RETURN_IF_ERROR(prop_recipe_manager_->DeleteRecipe(recipe.id));

  if (!source_is_shared) {
    const absl::Status source_deleted =
        source_artwork_manager_->DeleteArtwork(recipe.source_artwork_id);
    if (!source_deleted.ok() && !absl::IsNotFound(source_deleted)) return source_deleted;
  }
  return absl::OkStatus();
}

absl::Status Api::RegenerateGeneratedProp(const PreparedPropRegeneration& prepared) {
  RETURN_IF_ERROR(ValidatePreparedPropRegeneration(prepared));

  ASSIGN_OR_RETURN(SourceArtwork * current_source,
                   source_artwork_manager_->GetArtwork(prepared.source_snapshot.id));
  if (SourceArtworkToJson(*current_source) != SourceArtworkToJson(prepared.source_snapshot)) {
    return absl::FailedPreconditionError(
        "source artwork changed while this prop was regenerating; retry with current source");
  }

  ASSIGN_OR_RETURN(PropRecipe * current_recipe,
                   prop_recipe_manager_->GetRecipe(prepared.recipe_snapshot.id));
  if (PropRecipeToJson(*current_recipe) != PropRecipeToJson(prepared.recipe_snapshot)) {
    return absl::FailedPreconditionError(
        "prop recipe changed while artwork was regenerating; retry with current settings");
  }

  ASSIGN_OR_RETURN(Texture * current_texture,
                   texture_manager_->GetTexture(prepared.texture_snapshot.id));
  if (current_texture->id != prepared.texture_snapshot.id ||
      current_texture->name != prepared.texture_snapshot.name ||
      current_texture->path != prepared.texture_snapshot.path) {
    return absl::FailedPreconditionError(
        "generated texture definition changed while this prop was regenerating");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_pixels,
                   texture_manager_->ReadTexturePixels(current_texture->id));
  ASSIGN_OR_RETURN(const std::string current_digest, RgbaImageDigest(current_pixels));
  if (current_digest != prepared.texture_pixel_digest) {
    return absl::FailedPreconditionError(
        "generated texture pixels changed while this prop was regenerating");
  }

  ASSIGN_OR_RETURN(Sprite * current_sprite,
                   sprite_manager_->GetSprite(prepared.sprite_snapshot.id));
  if (*current_sprite != prepared.sprite_snapshot) {
    return absl::FailedPreconditionError(
        "generated sprite changed while this prop was regenerating; retry or use Save As");
  }
  RETURN_IF_ERROR(blueprint_manager_->GetBlueprint(prepared.updated_recipe.blueprint_id).status());
  if (prepared.updated_recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(
        terrain_recipe_manager_->GetRecipe(*prepared.updated_recipe.terrain_recipe_id).status());
  }

  RETURN_IF_ERROR(sprite_manager_->SaveSprite(prepared.updated_sprite));

  absl::Status status = prop_recipe_manager_->SaveRecipe(prepared.updated_recipe);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("restore sprite", sprite_manager_->SaveSprite(prepared.sprite_snapshot));
    return compensation.Report(status);
  }

  const RgbaImage& image = prepared.artwork.finished.image;
  status = texture_manager_->ReplaceTexturePixels(prepared.texture_snapshot.id, image.width,
                                                  image.height, image.pixels);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("restore recipe", prop_recipe_manager_->SaveRecipe(prepared.recipe_snapshot));
    compensation.Add("restore sprite", sprite_manager_->SaveSprite(prepared.sprite_snapshot));
    return compensation.Report(status);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> Api::CreateParallaxArtworkRecipe(ParallaxArtworkRecipe recipe) {
  RETURN_IF_ERROR(source_artwork_manager_->GetArtwork(recipe.source_artwork_id).status());
  if (recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(terrain_recipe_manager_->GetRecipe(*recipe.terrain_recipe_id).status());
  }
  RETURN_IF_ERROR(texture_manager_->GetTexture(recipe.texture_id).status());
  return parallax_artwork_recipe_manager_->CreateRecipe(std::move(recipe));
}

absl::Status Api::SaveParallaxArtworkRecipe(const ParallaxArtworkRecipe& recipe) {
  RETURN_IF_ERROR(source_artwork_manager_->GetArtwork(recipe.source_artwork_id).status());
  if (recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(terrain_recipe_manager_->GetRecipe(*recipe.terrain_recipe_id).status());
  }
  RETURN_IF_ERROR(texture_manager_->GetTexture(recipe.texture_id).status());
  return parallax_artwork_recipe_manager_->SaveRecipe(recipe);
}

absl::StatusOr<ParallaxArtworkRecipe*> Api::GetParallaxArtworkRecipe(const std::string& recipe_id) {
  return parallax_artwork_recipe_manager_->GetRecipe(recipe_id);
}

std::vector<ParallaxArtworkRecipe> Api::GetAllParallaxArtworkRecipes() const {
  return parallax_artwork_recipe_manager_->GetAllRecipes();
}

absl::StatusOr<std::string> Api::CreateGeneratedParallaxArtwork(
    const PreparedParallaxArtworkAsset& prepared) {
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkAsset(prepared));

  ASSIGN_OR_RETURN(SourceArtwork * current_source,
                   source_artwork_manager_->GetArtwork(prepared.source.id));
  if (SourceArtworkToJson(*current_source) != SourceArtworkToJson(prepared.source)) {
    return absl::FailedPreconditionError(
        "accepted source artwork changed after this parallax artwork was prepared");
  }
  if (prepared.recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(
        terrain_recipe_manager_->GetRecipe(*prepared.recipe.terrain_recipe_id).status());
  }

  RETURN_IF_ERROR(RequireAbsent(texture_manager_->GetTexture(prepared.texture.id).status(),
                                "texture", prepared.texture.id));
  RETURN_IF_ERROR(
      RequireAbsent(parallax_artwork_recipe_manager_->GetRecipe(prepared.recipe.id).status(),
                    "parallax artwork recipe", prepared.recipe.id));
  RETURN_IF_ERROR(texture_manager_->PreflightGeneratedTexture(prepared.texture));
  RETURN_IF_ERROR(parallax_artwork_recipe_manager_->PreflightRecipeWithId(prepared.recipe));

  const RgbaImage& image = prepared.artwork.finished;
  RETURN_IF_ERROR(texture_manager_->CreateGeneratedTexture(prepared.texture, image.width,
                                                           image.height, image.pixels));
  const absl::Status status = parallax_artwork_recipe_manager_->CreateRecipeWithId(prepared.recipe);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("delete texture", texture_manager_->DeleteTexture(prepared.texture.id));
    return compensation.Report(status);
  }
  return prepared.recipe.id;
}

absl::Status Api::RenameGeneratedParallaxArtwork(const std::string& recipe_id,
                                                 const std::string& name) {
  RETURN_IF_ERROR(ValidateParallaxArtworkAssetName(name));
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * current_recipe,
                   parallax_artwork_recipe_manager_->GetRecipe(recipe_id));
  ASSIGN_OR_RETURN(Texture * current_texture,
                   texture_manager_->GetTexture(current_recipe->texture_id));
  if (current_recipe->name != current_texture->name) {
    return absl::FailedPreconditionError(
        "generated parallax artwork recipe and texture names already differ");
  }
  if (current_recipe->name == name) return absl::OkStatus();

  const ParallaxArtworkRecipe recipe_snapshot = *current_recipe;
  ParallaxArtworkRecipe renamed_recipe = recipe_snapshot;
  renamed_recipe.name = name;
  Texture renamed_texture = *current_texture;
  renamed_texture.name = name;

  RETURN_IF_ERROR(parallax_artwork_recipe_manager_->SaveRecipe(renamed_recipe));
  const absl::Status texture_status = texture_manager_->UpdateTexture(renamed_texture);
  if (!texture_status.ok()) {
    CompensationFailures compensation;
    compensation.Add("restore recipe",
                     parallax_artwork_recipe_manager_->SaveRecipe(recipe_snapshot));
    return compensation.Report(texture_status);
  }
  return absl::OkStatus();
}

absl::Status Api::CheckGeneratedParallaxArtworkDeletable(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const ParallaxArtworkRecipe* loaded,
                   parallax_artwork_recipe_manager_->GetRecipe(recipe_id));
  const CatalogSnapshot catalog = SnapshotCatalog();
  return CheckGeneratedParallaxArtworkDeletable(*loaded, catalog);
}

absl::Status Api::CheckGeneratedParallaxArtworkDeletable(const ParallaxArtworkRecipe& recipe,
                                                         const CatalogSnapshot& catalog) {
  std::vector<AssetReference> outside;
  for (const AssetReference& referrer : FindTextureReferrers(catalog.View(), recipe.texture_id)) {
    if (referrer.kind == AssetKind::kParallaxArtworkRecipe && referrer.id == recipe.id) continue;
    outside.push_back(referrer);
  }
  return RefuseIfReferenced(absl::StrCat("generated parallax artwork '", recipe.name, "'"),
                            outside);
}

absl::Status Api::DeleteGeneratedParallaxArtwork(const std::string& recipe_id) {
  ASSIGN_OR_RETURN(const ParallaxArtworkRecipe* loaded,
                   parallax_artwork_recipe_manager_->GetRecipe(recipe_id));
  const ParallaxArtworkRecipe recipe = *loaded;

  const CatalogSnapshot catalog = SnapshotCatalog();
  RETURN_IF_ERROR(CheckGeneratedParallaxArtworkDeletable(recipe, catalog));

  bool source_is_shared = false;
  for (const AssetReference& referrer :
       FindSourceArtworkReferrers(catalog.View(), recipe.source_artwork_id)) {
    if (referrer.kind != AssetKind::kParallaxArtworkRecipe || referrer.id != recipe.id) {
      source_is_shared = true;
      break;
    }
  }

  const absl::Status texture_deleted = texture_manager_->DeleteTexture(recipe.texture_id);
  if (!texture_deleted.ok() && !absl::IsNotFound(texture_deleted)) return texture_deleted;
  RETURN_IF_ERROR(parallax_artwork_recipe_manager_->DeleteRecipe(recipe.id));

  if (!source_is_shared) {
    const absl::Status source_deleted =
        source_artwork_manager_->DeleteArtwork(recipe.source_artwork_id);
    if (!source_deleted.ok() && !absl::IsNotFound(source_deleted)) return source_deleted;
  }
  return absl::OkStatus();
}

absl::Status Api::RegenerateGeneratedParallaxArtwork(
    const PreparedParallaxArtworkRegeneration& prepared) {
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkRegeneration(prepared));

  ASSIGN_OR_RETURN(SourceArtwork * current_source,
                   source_artwork_manager_->GetArtwork(prepared.source_snapshot.id));
  if (SourceArtworkToJson(*current_source) != SourceArtworkToJson(prepared.source_snapshot)) {
    return absl::FailedPreconditionError(
        "source artwork changed while parallax artwork was regenerating; retry");
  }

  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * current_recipe,
                   parallax_artwork_recipe_manager_->GetRecipe(prepared.recipe_snapshot.id));
  if (ParallaxArtworkRecipeToJson(*current_recipe) !=
      ParallaxArtworkRecipeToJson(prepared.recipe_snapshot)) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe changed while artwork was regenerating; retry");
  }

  ASSIGN_OR_RETURN(Texture * current_texture,
                   texture_manager_->GetTexture(prepared.texture_snapshot.id));
  if (current_texture->id != prepared.texture_snapshot.id ||
      current_texture->name != prepared.texture_snapshot.name ||
      current_texture->path != prepared.texture_snapshot.path) {
    return absl::FailedPreconditionError(
        "generated texture definition changed while parallax artwork was regenerating");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_pixels,
                   texture_manager_->ReadTexturePixels(current_texture->id));
  ASSIGN_OR_RETURN(const std::string current_digest, RgbaImageDigest(current_pixels));
  if (current_digest != prepared.texture_pixel_digest) {
    return absl::FailedPreconditionError(
        "generated texture pixels changed while parallax artwork was regenerating");
  }
  if (prepared.updated_recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(
        terrain_recipe_manager_->GetRecipe(*prepared.updated_recipe.terrain_recipe_id).status());
  }

  RETURN_IF_ERROR(parallax_artwork_recipe_manager_->SaveRecipe(prepared.updated_recipe));
  const RgbaImage& image = prepared.artwork.finished;
  const absl::Status status = texture_manager_->ReplaceTexturePixels(
      prepared.texture_snapshot.id, image.width, image.height, image.pixels);
  if (!status.ok()) {
    CompensationFailures compensation;
    compensation.Add("restore recipe",
                     parallax_artwork_recipe_manager_->SaveRecipe(prepared.recipe_snapshot));
    return compensation.Report(status);
  }
  return absl::OkStatus();
}

absl::Status Api::RedrawGeneratedParallaxArtwork(const PreparedParallaxArtworkRedraw& prepared) {
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkRedraw(prepared));

  const CatalogSnapshot catalog = SnapshotCatalog();
  std::vector<AssetReference> other_source_users;
  for (const AssetReference& referrer :
       FindSourceArtworkReferrers(catalog.View(), prepared.source_snapshot.id)) {
    if (referrer.kind == AssetKind::kParallaxArtworkRecipe &&
        referrer.id == prepared.recipe_snapshot.id) {
      continue;
    }
    other_source_users.push_back(referrer);
  }
  RETURN_IF_ERROR(RefuseIfReferenced(
      absl::StrCat("retained source redraw '", prepared.source_snapshot.name, "'"),
      other_source_users,
      "A redraw cannot silently make another generated bundle stale; give this recipe an "
      "unshared source first."));

  ASSIGN_OR_RETURN(SourceArtwork * current_source,
                   source_artwork_manager_->GetArtwork(prepared.source_snapshot.id));
  if (SourceArtworkToJson(*current_source) != SourceArtworkToJson(prepared.source_snapshot)) {
    return absl::FailedPreconditionError(
        "source artwork changed while its redraw was being reviewed; retry");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_source_pixels,
                   source_artwork_manager_->ReadArtworkPixels(current_source->id));
  ASSIGN_OR_RETURN(const std::string current_source_digest, RgbaImageDigest(current_source_pixels));
  if (current_source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError(
        "source artwork pixels changed while its redraw was being reviewed; retry");
  }

  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * current_recipe,
                   parallax_artwork_recipe_manager_->GetRecipe(prepared.recipe_snapshot.id));
  if (ParallaxArtworkRecipeToJson(*current_recipe) !=
      ParallaxArtworkRecipeToJson(prepared.recipe_snapshot)) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe changed while its redraw was being reviewed; retry");
  }

  ASSIGN_OR_RETURN(Texture * current_texture,
                   texture_manager_->GetTexture(prepared.texture_snapshot.id));
  if (current_texture->id != prepared.texture_snapshot.id ||
      current_texture->name != prepared.texture_snapshot.name ||
      current_texture->path != prepared.texture_snapshot.path) {
    return absl::FailedPreconditionError(
        "generated texture definition changed while its redraw was being reviewed");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_texture_pixels,
                   texture_manager_->ReadTexturePixels(current_texture->id));
  ASSIGN_OR_RETURN(const std::string current_texture_digest,
                   RgbaImageDigest(current_texture_pixels));
  if (current_texture_digest != prepared.texture_pixel_digest) {
    return absl::FailedPreconditionError(
        "generated texture pixels changed while its redraw was being reviewed");
  }
  if (prepared.updated_recipe.terrain_recipe_id.has_value()) {
    RETURN_IF_ERROR(
        terrain_recipe_manager_->GetRecipe(*prepared.updated_recipe.terrain_recipe_id).status());
  }

  RETURN_IF_ERROR(source_artwork_manager_->ReplaceArtwork(
      prepared.source_snapshot, prepared.updated_source, prepared.updated_source_pixels));
  const absl::Status recipe_status =
      parallax_artwork_recipe_manager_->SaveRecipe(prepared.updated_recipe);
  if (!recipe_status.ok()) {
    CompensationFailures compensation;
    compensation.Add(
        "restore retained source",
        source_artwork_manager_->ReplaceArtwork(prepared.updated_source, prepared.source_snapshot,
                                                prepared.source_pixels_snapshot));
    return compensation.Report(recipe_status);
  }

  const RgbaImage& image = prepared.artwork.finished;
  const absl::Status texture_status = texture_manager_->ReplaceTexturePixels(
      prepared.texture_snapshot.id, image.width, image.height, image.pixels);
  if (!texture_status.ok()) {
    CompensationFailures compensation;
    compensation.Add("restore recipe",
                     parallax_artwork_recipe_manager_->SaveRecipe(prepared.recipe_snapshot));
    compensation.Add(
        "restore retained source",
        source_artwork_manager_->ReplaceArtwork(prepared.updated_source, prepared.source_snapshot,
                                                prepared.source_pixels_snapshot));
    return compensation.Report(texture_status);
  }
  return absl::OkStatus();
}

}  // namespace zebes
