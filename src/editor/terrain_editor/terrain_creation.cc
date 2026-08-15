#include "editor/terrain_editor/terrain_creation.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "terrain/terrain_detect.h"

namespace zebes {
namespace {

// Both authoring routes end here: a candidate plus the artwork it refers to
// becomes a saved tileset. Tile and terrain IDs start at 1 because the tileset
// is new and has nothing to collide with.
absl::StatusOr<CreatedTerrain> SaveTilesetForCandidate(Api& api, const std::string& name,
                                                       const std::string& texture_id,
                                                       TerrainCandidate candidate) {
  if (candidate.tile_size <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain artwork has no usable cell size: ", candidate.tile_size));
  }

  Tileset tileset;
  tileset.name = name;
  tileset.texture_id = texture_id;
  tileset.tile_width = candidate.tile_size;
  tileset.tile_height = candidate.tile_size;
  tileset.tiles = std::move(candidate.tiles);

  // The terrain takes the tileset's name: importing cannot infer one, and
  // "Terrain" tells a person nothing once a project has several.
  candidate.terrain.name = name;
  tileset.terrains.push_back(std::move(candidate.terrain));

  const int tile_count = static_cast<int>(tileset.tiles.size());
  ASSIGN_OR_RETURN(const std::string tileset_id, api.CreateTileset(std::move(tileset)));

  return CreatedTerrain{
      .texture_id = texture_id,
      .tileset_id = tileset_id,
      .tile_count = tile_count,
  };
}

absl::Status ValidateName(const std::string& name) {
  if (name.empty()) return absl::InvalidArgumentError("Name the terrain before creating it");
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(Api& api, const std::string& name,
                                                             const TerrainGenConfig& config) {
  RETURN_IF_ERROR(ValidateName(name));

  ASSIGN_OR_RETURN(const Blob47Atlas atlas, GenerateBlob47Atlas(config));
  // Artwork first: if the name collides with existing artwork this fails before
  // anything has been written, leaving no half-made tileset behind.
  ASSIGN_OR_RETURN(
      const std::string texture_id,
      api.CreateTextureFromPixels(name, atlas.image.width, atlas.image.height, atlas.image.pixels));
  ASSIGN_OR_RETURN(TerrainCandidate candidate,
                   BuildTerrainCandidate(atlas, /*first_tile_id=*/1, /*terrain_id=*/1,
                                         TerrainScheme::kDerived));

  return SaveTilesetForCandidate(api, name, texture_id, std::move(candidate));
}

absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(
    Api& api, const std::string& name,
    const TerrainGenConfig& config, const std::optional<std::string>& source_preset) {
  ASSIGN_OR_RETURN(CreatedTerrain created, CreateGeneratedTerrainTileset(api, name, config));

  TerrainRecipe recipe{
      .name = name,
      .tileset_id = created.tileset_id,
      .texture_id = created.texture_id,
      .terrain_id = 1,
      .source_preset = source_preset,
      .config = config,
  };
  absl::StatusOr<std::string> recipe_id = api.CreateTerrainRecipe(std::move(recipe));
  if (!recipe_id.ok()) {
    // Definition rollback is best-effort but ordered: the tileset must stop
    // referencing the texture before the texture definition can disappear.
    api.DeleteTileset(created.tileset_id).IgnoreError();
    api.DeleteTexture(created.texture_id).IgnoreError();
    return recipe_id.status();
  }
  created.recipe_id = *recipe_id;
  return created;
}

absl::Status RegenerateTerrainTileset(Api& api, const TerrainRecipe& recipe,
                                      const TerrainGenConfig& config) {
  if (config.tile_size != recipe.config.tile_size ||
      config.variant_period != recipe.config.variant_period) {
    return absl::FailedPreconditionError(
        "tile size and repeat period change atlas topology; use Save As to create new IDs");
  }

  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(recipe.tileset_id));
  if (tileset->texture_id != recipe.texture_id) {
    return absl::FailedPreconditionError(
        "terrain recipe texture no longer matches its tileset; refusing to overwrite artwork");
  }
  if (tileset->tile_width != recipe.config.tile_size ||
      tileset->tile_height != recipe.config.tile_size) {
    return absl::FailedPreconditionError(
        "terrain tileset cell size changed after generation; use Save As");
  }

  const Terrain* terrain = nullptr;
  for (const Terrain& candidate : tileset->terrains) {
    if (candidate.id == recipe.terrain_id) terrain = &candidate;
  }
  if (terrain == nullptr || terrain->variant_period != recipe.config.variant_period) {
    return absl::FailedPreconditionError(
        "terrain recipe target is missing or its repeat period has changed");
  }

  // Regeneration re-renders positionally: tile N of the new atlas overwrites
  // tile N of the old one, which only holds while the tileset still has exactly
  // the tiles generation produced. A derived terrain grows past that as levels
  // ask for neighbourhoods, and those extra tiles cannot be re-rendered from
  // here because nothing records which neighbourhood each one depicts.
  //
  // Refusing is the honest answer until a derived tile carries its key. Silently
  // re-rendering the first N would leave every grown tile showing the old
  // material, and Save As would renumber IDs the level already references.
  const int generated_tiles =
      kBlob47TileCount * recipe.config.variant_period * recipe.config.variant_period;
  if (tileset->tiles.size() != static_cast<size_t>(generated_tiles)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "tileset '", tileset->name, "' has grown to ", tileset->tiles.size(), " tiles from the ",
        generated_tiles,
        " generation produced, so regenerating would leave the added artwork stale"));
  }

  ASSIGN_OR_RETURN(const Blob47Atlas atlas, GenerateBlob47Atlas(config));

  TerrainRecipe updated = recipe;
  updated.config = config;
  RETURN_IF_ERROR(api.SaveTerrainRecipe(updated));

  const absl::Status replaced = api.ReplaceTexturePixels(recipe.texture_id, atlas.image.width,
                                                         atlas.image.height, atlas.image.pixels);
  if (replaced.ok()) return absl::OkStatus();

  const absl::Status rolled_back = api.SaveTerrainRecipe(recipe);
  if (!rolled_back.ok()) {
    return absl::InternalError(absl::StrCat("artwork replacement failed (", replaced.message(),
                                            ") and the recipe rollback also failed (",
                                            rolled_back.message(), ")"));
  }
  return replaced;
}

absl::StatusOr<CreatedTerrain> CreateImportedTerrainTileset(Api& api, const std::string& name,
                                                            const std::string& texture_id,
                                                            absl::string_view manifest_json) {
  RETURN_IF_ERROR(ValidateName(name));
  if (texture_id.empty()) {
    return absl::InvalidArgumentError(
        "Choose the texture this manifest describes; importing does not create artwork");
  }

  ASSIGN_OR_RETURN(TerrainCandidate candidate,
                   ImportBlob47Manifest(manifest_json, /*first_tile_id=*/1, /*terrain_id=*/1));

  return SaveTilesetForCandidate(api, name, texture_id, std::move(candidate));
}

}  // namespace zebes
