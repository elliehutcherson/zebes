#include "terrain/terrain_content_index.h"

#include <algorithm>

#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<RgbaImage> CropRegion(const RgbaImage& source, int x, int y, int width, int height) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("cannot crop a malformed image");
  }
  if (width <= 0 || height <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("cannot crop a ", width, "x", height, " region"));
  }
  if (x < 0 || y < 0 || x + width > source.width || y + height > source.height) {
    return absl::OutOfRangeError(absl::StrCat("region (", x, ", ", y, ") ", width, "x", height,
                                              " falls outside a ", source.width, "x", source.height,
                                              " image"));
  }

  RgbaImage region;
  region.width = width;
  region.height = height;
  region.pixels.resize(static_cast<size_t>(width) * height * 4);
  for (int row = 0; row < height; ++row) {
    const size_t from = (static_cast<size_t>(y + row) * source.width + x) * 4;
    const size_t to = static_cast<size_t>(row) * width * 4;
    std::copy_n(source.pixels.begin() + from, static_cast<size_t>(width) * 4,
                region.pixels.begin() + to);
  }
  return region;
}

absl::StatusOr<TerrainContentIndex> TerrainContentIndex::Build(const Tileset& tileset,
                                                               const RgbaImage& atlas) {
  if (tileset.tile_width <= 0 || tileset.tile_height <= 0) {
    return absl::InvalidArgumentError(absl::StrCat("tileset '", tileset.name, "' has a ",
                                                   tileset.tile_width, "x", tileset.tile_height,
                                                   " cell size"));
  }

  // Lowest ID wins, so the index does not depend on the order of the tile
  // table. A hand-drawn tileset may legitimately hold two tiles drawn the same.
  std::vector<const Tile*> ordered;
  ordered.reserve(tileset.tiles.size());
  for (const Tile& tile : tileset.tiles) ordered.push_back(&tile);
  std::ranges::sort(ordered, {}, [](const Tile* tile) { return tile->id; });

  TerrainContentIndex index;
  for (const Tile* tile : ordered) {
    absl::StatusOr<RgbaImage> region =
        CropRegion(atlas, tile->source_x, tile->source_y, tileset.tile_width, tileset.tile_height);
    if (!region.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("tile ", tile->id, " of tileset '", tileset.name,
                       "' is not inside its atlas: ", region.status().message()));
    }
    index.tile_by_content_.emplace(std::move(region->pixels), tile->id);
  }
  return index;
}

std::optional<int> TerrainContentIndex::Find(const RgbaImage& tile) const {
  auto found = tile_by_content_.find(tile.pixels);
  if (found == tile_by_content_.end()) return std::nullopt;
  return found->second;
}

absl::Status TerrainContentIndex::Insert(const RgbaImage& tile, int tile_id) {
  if (!tile.IsValid()) {
    return absl::InvalidArgumentError(absl::StrCat("tile ", tile_id, " is a malformed image"));
  }

  auto [entry, inserted] = tile_by_content_.emplace(tile.pixels, tile_id);
  if (inserted) return absl::OkStatus();

  return absl::AlreadyExistsError(
      absl::StrCat("tile ", entry->second, " already holds the artwork offered as tile ", tile_id));
}

}  // namespace zebes
