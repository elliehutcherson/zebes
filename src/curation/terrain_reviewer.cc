#include "curation/terrain_reviewer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "objects/tileset.h"
#include "resources/tileset_manager.h"
#include "terrain/terrain_recipe.h"
#include "terrain/terrain_review_scene.h"

namespace zebes {
namespace {

constexpr size_t kMaximumTerrainFrames = 4096;
constexpr RgbaColor8 kTransparent{.red = 0, .green = 0, .blue = 0, .alpha = 0};

const Terrain* FindTerrain(const Tileset& tileset, int terrain_id) {
  for (const Terrain& terrain : tileset.terrains) {
    if (terrain.id == terrain_id) return &terrain;
  }
  return nullptr;
}

std::set<int> OwnedTileIds(const Terrain& terrain) {
  std::set<int> ids(terrain.shape_tile_ids.begin(), terrain.shape_tile_ids.end());
  for (const TerrainRule& rule : terrain.rules) {
    for (const TerrainVariant& variant : rule.variants) ids.insert(variant.tile_id);
  }
  for (const DerivedTile& derived : terrain.derived_tiles) ids.insert(derived.tile_id);
  return ids;
}

absl::StatusOr<RgbaImage> CropTile(const Tileset& tileset, const Tile& tile,
                                   const RgbaImage& atlas) {
  const int64_t right = static_cast<int64_t>(tile.source_x) + tileset.tile_width;
  const int64_t bottom = static_cast<int64_t>(tile.source_y) + tileset.tile_height;
  if (tile.source_x < 0 || tile.source_y < 0 || right > atlas.width || bottom > atlas.height) {
    return absl::FailedPreconditionError(
        absl::StrCat("terrain tile ", tile.id, " lies outside its atlas"));
  }
  ASSIGN_OR_RETURN(RgbaImage frame,
                   CreateSolidRgbaImage(tileset.tile_width, tileset.tile_height, kTransparent));
  RETURN_IF_ERROR(CompositeRgbaNearest(frame, atlas,
                                       {.x = tile.source_x,
                                        .y = tile.source_y,
                                        .width = tileset.tile_width,
                                        .height = tileset.tile_height},
                                       {.x = 0.0,
                                        .y = 0.0,
                                        .width = static_cast<double>(tileset.tile_width),
                                        .height = static_cast<double>(tileset.tile_height)}));
  return frame;
}

}  // namespace

absl::StatusOr<CurationReview> TerrainReviewer::Review(Api& api,
                                                       const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(TerrainRecipe * recipe, api.GetTerrainRecipe(request.asset_id));
  if (recipe == nullptr)
    return absl::FailedPreconditionError("terrain recipe lookup returned null");
  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(recipe->tileset_id));
  if (tileset == nullptr || tileset->id != recipe->tileset_id) {
    return absl::FailedPreconditionError("terrain recipe tileset lookup returned invalid data");
  }
  RETURN_IF_ERROR(ValidateTileset(*tileset));
  if (tileset->texture_id != recipe->texture_id) {
    return absl::FailedPreconditionError("terrain recipe and tileset name different textures");
  }
  if (recipe->config.tile_size != tileset->tile_width ||
      recipe->config.tile_size != tileset->tile_height) {
    return absl::FailedPreconditionError(
        "terrain recipe tile size does not match its square tileset cells");
  }
  const Terrain* terrain = FindTerrain(*tileset, recipe->terrain_id);
  if (terrain == nullptr) {
    return absl::FailedPreconditionError("terrain recipe names an unknown terrain definition");
  }
  const std::set<int> owned_ids = OwnedTileIds(*terrain);
  if (owned_ids.empty()) {
    return absl::FailedPreconditionError("generated terrain owns no atlas frames");
  }
  if (owned_ids.size() > kMaximumTerrainFrames) {
    return absl::ResourceExhaustedError("terrain exceeds the headless frame artifact limit");
  }
  std::map<int, const Tile*> tile_by_id;
  for (const Tile& tile : tileset->tiles) tile_by_id.emplace(tile.id, &tile);
  for (const int tile_id : owned_ids) {
    if (!tile_by_id.contains(tile_id)) {
      return absl::FailedPreconditionError(absl::StrCat("terrain owns unknown tile ID ", tile_id));
    }
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  if (texture == nullptr || texture->id != recipe->texture_id) {
    return absl::FailedPreconditionError("terrain atlas texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage atlas, api.ReadTexturePixels(texture->id));
  ASSIGN_OR_RETURN(RgbaImage slopes, RenderTerrainSlopeReviewMatrix(recipe->config));

  nlohmann::json bands = nlohmann::json::array();
  for (const std::string_view band : TerrainSlopeReviewBandNames()) bands.push_back(band);
  CurationReview review{
      .kind = std::string(kind()),
      .asset_id = recipe->id,
      .asset_name = recipe->name,
      .metadata =
          {
              {"recipe", TerrainRecipeToJson(*recipe)},
              {"tileset_id", recipe->tileset_id},
              {"texture_id", recipe->texture_id},
              {"terrain_id", recipe->terrain_id},
              {"terrain_name", terrain->name},
              {"owned_tile_count", owned_ids.size()},
              {"slope_bands", std::move(bands)},
          },
      .artifacts =
          {
              {
                  .id = "slope-matrix",
                  .relative_path = "slope-matrix.png",
                  .description =
                      "Contextual joins for flat, gentle, steep, peak, and valley slopes",
                  .image = std::move(slopes),
                  .metadata = {{"view", "slope-matrix"}},
              },
              {
                  .id = "atlas",
                  .relative_path = "atlas.png",
                  .description = "Complete persisted generated terrain atlas",
                  .image = atlas,
                  .metadata = {{"view", "atlas"}},
              },
          },
  };
  for (const int tile_id : owned_ids) {
    const Tile& tile = *tile_by_id.at(tile_id);
    if (static_cast<size_t>(tile.shape) >= std::size(kTileShapeIdentifiers)) {
      return absl::FailedPreconditionError(
          absl::StrCat("terrain tile ", tile.id, " has an invalid shape"));
    }
    ASSIGN_OR_RETURN(RgbaImage frame, CropTile(*tileset, tile, atlas));
    review.artifacts.push_back({
        .id = absl::StrCat("tile-", tile.id),
        .relative_path = absl::StrCat("tiles/", tile.id, ".png"),
        .description = absl::StrCat("Generated terrain atlas frame ", tile.id),
        .image = std::move(frame),
        .metadata = {{"view", "generated-atlas-frame"},
                     {"tile_id", tile.id},
                     {"shape", kTileShapeIdentifiers[static_cast<size_t>(tile.shape)]}},
    });
  }
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
