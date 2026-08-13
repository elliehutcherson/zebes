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
  ASSIGN_OR_RETURN(const std::string texture_id,
                   api.CreateTextureFromPixels(name, atlas.image.width, atlas.image.height,
                                               atlas.image.pixels));
  ASSIGN_OR_RETURN(TerrainCandidate candidate,
                   BuildTerrainCandidate(atlas, /*first_tile_id=*/1, /*terrain_id=*/1));

  return SaveTilesetForCandidate(api, name, texture_id, std::move(candidate));
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
