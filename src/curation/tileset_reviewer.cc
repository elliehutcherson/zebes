#include "curation/tileset_reviewer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "objects/tileset.h"
#include "resources/tileset_manager.h"

namespace zebes {
namespace {

constexpr size_t kMaximumTileArtifacts = 4096;
constexpr size_t kMaximumContextTiles = 1024;
constexpr int64_t kMaximumTileArtifactPixels = 256LL * 1024 * 1024;
constexpr RgbaColor8 kTransparent{.red = 0, .green = 0, .blue = 0, .alpha = 0};
constexpr RgbaColor8 kCheckerLight{.red = 55, .green = 55, .blue = 65, .alpha = 255};
constexpr RgbaColor8 kCheckerDark{.red = 35, .green = 35, .blue = 45, .alpha = 255};

absl::Status ValidateTileFrame(const Tileset& tileset, const Tile& tile, const RgbaImage& atlas) {
  if (static_cast<size_t>(tile.shape) >= std::size(kTileShapeIdentifiers)) {
    return absl::FailedPreconditionError(absl::StrCat("tile ", tile.id, " has an invalid shape"));
  }
  const int64_t right = static_cast<int64_t>(tile.source_x) + tileset.tile_width;
  const int64_t bottom = static_cast<int64_t>(tile.source_y) + tileset.tile_height;
  if (tile.source_x < 0 || tile.source_y < 0 || right > atlas.width || bottom > atlas.height) {
    return absl::FailedPreconditionError(absl::StrCat("tile ", tile.id, " lies outside its atlas"));
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> CropTile(const Tileset& tileset, const Tile& tile,
                                   const RgbaImage& atlas) {
  RETURN_IF_ERROR(ValidateTileFrame(tileset, tile, atlas));
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

absl::StatusOr<RgbaImage> RenderPlacementContext(const Tileset& tileset, const RgbaImage& atlas) {
  const size_t count = std::min(tileset.tiles.size(), kMaximumContextTiles);
  const int columns = std::max(1, std::min(16, static_cast<int>(count)));
  const int rows = std::max(1, static_cast<int>((count + columns - 1) / columns));
  const double scale = std::min(4.0, 64.0 / std::max(tileset.tile_width, tileset.tile_height));
  const int rendered_width = std::max(1, static_cast<int>(std::round(tileset.tile_width * scale)));
  const int rendered_height =
      std::max(1, static_cast<int>(std::round(tileset.tile_height * scale)));
  const int cell_width = rendered_width + 8;
  const int cell_height = rendered_height + 8;
  ASSIGN_OR_RETURN(RgbaImage context,
                   CreateCheckerboardRgbaImage(columns * cell_width + 8, rows * cell_height + 8, 8,
                                               kCheckerLight, kCheckerDark));
  for (size_t index = 0; index < count; ++index) {
    const Tile& tile = tileset.tiles[index];
    RETURN_IF_ERROR(ValidateTileFrame(tileset, tile, atlas));
    const int column = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    RETURN_IF_ERROR(CompositeRgbaNearest(context, atlas,
                                         {.x = tile.source_x,
                                          .y = tile.source_y,
                                          .width = tileset.tile_width,
                                          .height = tileset.tile_height},
                                         {.x = static_cast<double>(8 + column * cell_width),
                                          .y = static_cast<double>(8 + row * cell_height),
                                          .width = static_cast<double>(rendered_width),
                                          .height = static_cast<double>(rendered_height)}));
  }
  return context;
}

}  // namespace

absl::StatusOr<CurationReview> TilesetReviewer::Review(Api& api,
                                                       const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(request.asset_id));
  if (tileset == nullptr) return absl::FailedPreconditionError("tileset lookup returned null");
  RETURN_IF_ERROR(ValidateTileset(*tileset));
  if (tileset->tiles.size() > kMaximumTileArtifacts) {
    return absl::ResourceExhaustedError("tileset exceeds the headless per-tile artifact limit");
  }
  const int64_t tile_pixels = static_cast<int64_t>(tileset->tile_width) * tileset->tile_height;
  if (tile_pixels * static_cast<int64_t>(tileset->tiles.size()) > kMaximumTileArtifactPixels) {
    return absl::ResourceExhaustedError("tileset per-tile artifacts exceed the pixel limit");
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(tileset->texture_id));
  if (texture == nullptr || texture->id != tileset->texture_id) {
    return absl::FailedPreconditionError("tileset atlas texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage atlas, api.ReadTexturePixels(texture->id));
  ASSIGN_OR_RETURN(RgbaImage context, RenderPlacementContext(*tileset, atlas));

  nlohmann::json tile_definitions = nlohmann::json::array();
  CurationReview review{
      .kind = std::string(kind()),
      .asset_id = tileset->id,
      .asset_name = tileset->name,
      .metadata =
          {
              {"texture_id", tileset->texture_id},
              {"tile_width", tileset->tile_width},
              {"tile_height", tileset->tile_height},
              {"tile_count", tileset->tiles.size()},
              {"terrain_count", tileset->terrains.size()},
          },
      .artifacts =
          {
              {
                  .id = "atlas",
                  .relative_path = "atlas.png",
                  .description = "Complete managed tileset atlas at native resolution",
                  .image = atlas,
                  .metadata = {{"view", "atlas"}},
              },
              {
                  .id = "placement-context",
                  .relative_path = "placement-context.png",
                  .description = "Tile frames arranged over a game-style transparency grid",
                  .image = std::move(context),
                  .metadata = {{"view", "placement-context"},
                               {"tiles_rendered",
                                std::min(tileset->tiles.size(), kMaximumContextTiles)}},
              },
          },
  };
  for (const Tile& tile : tileset->tiles) {
    ASSIGN_OR_RETURN(RgbaImage frame, CropTile(*tileset, tile, atlas));
    review.artifacts.push_back({
        .id = absl::StrCat("tile-", tile.id),
        .relative_path = absl::StrCat("tiles/", tile.id, ".png"),
        .description = absl::StrCat("Native frame for tile ", tile.id, " (", tile.name, ")"),
        .image = std::move(frame),
        .metadata = {{"view", "tile-frame"},
                     {"tile_id", tile.id},
                     {"name", tile.name},
                     {"shape", kTileShapeIdentifiers[static_cast<size_t>(tile.shape)]}},
    });
    tile_definitions.push_back({
        {"id", tile.id},
        {"name", tile.name},
        {"source_x", tile.source_x},
        {"source_y", tile.source_y},
        {"shape", kTileShapeIdentifiers[static_cast<size_t>(tile.shape)]},
        {"one_way", tile.is_one_way},
        {"tags", tile.tags},
    });
  }
  review.metadata["tiles"] = std::move(tile_definitions);
  if (tileset->tiles.size() > kMaximumContextTiles) {
    review.findings.push_back({
        .severity = CurationFindingSeverity::kInfo,
        .code = "placement-context-truncated",
        .subject = tileset->name,
        .message = absl::StrCat("placement context shows the first ", kMaximumContextTiles,
                                " tiles; native frames include every tile"),
    });
  }
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
