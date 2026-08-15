#include "editor/level_editor/derived_tile_provider.h"

#include <algorithm>
#include <set>

#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

// Copies artwork into an atlas cell. Unlike CopyTile in blob47_compose this
// takes independent width and height, because a tileset's cells need not be
// square.
absl::Status BlitRegion(const RgbaImage& source, RgbaImage& target, int x, int y) {
  if (x < 0 || y < 0 || x + source.width > target.width || y + source.height > target.height) {
    return absl::OutOfRangeError(absl::StrCat("artwork at (", x, ", ", y, ") does not fit a ",
                                              target.width, "x", target.height, " atlas"));
  }
  for (int row = 0; row < source.height; ++row) {
    const size_t from = static_cast<size_t>(row) * source.width * 4;
    const size_t to = (static_cast<size_t>(y + row) * target.width + x) * 4;
    std::copy_n(source.pixels.begin() + from, static_cast<size_t>(source.width) * 4,
                target.pixels.begin() + to);
  }
  return absl::OkStatus();
}

int NextTileId(const Tileset& tileset) {
  int highest = 0;
  for (const Tile& tile : tileset.tiles) highest = std::max(highest, tile.id);
  return highest + 1;
}

}  // namespace

DerivedTileProvider::DerivedTileProvider(TerrainRenderer renderer, Tileset& tileset,
                                         RgbaImage atlas, TerrainContentIndex content, int columns)
    : renderer_(std::move(renderer)),
      tileset_(&tileset),
      atlas_(std::move(atlas)),
      content_(std::move(content)),
      columns_(columns) {}

absl::StatusOr<DerivedTileProvider> DerivedTileProvider::Create(TerrainRenderer renderer,
                                                                Tileset& tileset,
                                                                RgbaImage atlas) {
  if (tileset.tile_width <= 0 || tileset.tile_height <= 0) {
    return absl::InvalidArgumentError(absl::StrCat("tileset '", tileset.name, "' has a ",
                                                   tileset.tile_width, "x", tileset.tile_height,
                                                   " cell size"));
  }
  if (!atlas.IsValid()) {
    return absl::InvalidArgumentError(absl::StrCat("tileset '", tileset.name, "' has no atlas"));
  }
  if (atlas.width % tileset.tile_width != 0 || atlas.height % tileset.tile_height != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("a ", atlas.width, "x", atlas.height, " atlas is not a whole number of ",
                     tileset.tile_width, "x", tileset.tile_height, " cells"));
  }
  if (tileset.tile_width != renderer.config().tile_size ||
      tileset.tile_height != renderer.config().tile_size) {
    return absl::InvalidArgumentError(absl::StrCat(
        "the recipe renders ", renderer.config().tile_size, "px tiles but tileset '", tileset.name,
        "' cuts ", tileset.tile_width, "x", tileset.tile_height,
        "; regenerate the tileset rather than mixing cell sizes"));
  }

  const int columns = atlas.width / tileset.tile_width;
  ASSIGN_OR_RETURN(TerrainContentIndex content, TerrainContentIndex::Build(tileset, atlas));
  return DerivedTileProvider(std::move(renderer), tileset, std::move(atlas), std::move(content),
                             columns);
}

int DerivedTileProvider::FirstFreeCell() const {
  std::set<int> occupied;
  for (const Tile& tile : tileset_->tiles) {
    const int column = tile.source_x / tileset_->tile_width;
    const int row = tile.source_y / tileset_->tile_height;
    occupied.insert(row * columns_ + column);
  }

  int cell = 0;
  while (occupied.contains(cell)) ++cell;
  return cell;
}

absl::StatusOr<int> DerivedTileProvider::AppendTile(const Terrain& terrain,
                                                    const TerrainCellKey& key,
                                                    const RgbaImage& artwork) {
  const TileShape shape = key.shape;
  const int cell = FirstFreeCell();
  const int column = cell % columns_;
  const int row = cell / columns_;

  // Grow by whole rows. The atlas stays a rectangle of cells, so nothing
  // downstream has to know it was ever a different size.
  const int rows_needed = row + 1;
  const int rows_present = atlas_.height / tileset_->tile_height;
  if (rows_needed > rows_present) {
    atlas_.height = rows_needed * tileset_->tile_height;
    atlas_.pixels.resize(static_cast<size_t>(atlas_.width) * atlas_.height * 4, 0);
  }

  RETURN_IF_ERROR(
      BlitRegion(artwork, atlas_, column * tileset_->tile_width, row * tileset_->tile_height));

  const int tile_id = NextTileId(*tileset_);
  tileset_->tiles.push_back(Tile{
      .id = tile_id,
      // Named for the geometry, because that is the authored half. Which
      // neighbourhood produced it lives in the key, and a name long enough to
      // hold one would be unreadable in a palette.
      .name = absl::StrCat(terrain.name, " ", kTileShapeIdentifiers[static_cast<size_t>(shape)]),
      .source_x = column * tileset_->tile_width,
      .source_y = row * tileset_->tile_height,
      // The tile carries the geometry that was asked for. This is the link that
      // makes artwork derived from collision rather than the other way round:
      // the level stores a tile ID, and the shape it collides with is whatever
      // this says.
      .shape = shape,
  });
  // The terrain has to claim the tile, or the brush reads it back as foreign
  // material: the next cell painted beside it would see air where its own
  // ground is, and draw an edge against itself.
  Terrain* owner = nullptr;
  for (Terrain& candidate : tileset_->terrains) {
    if (candidate.id == terrain.id) owner = &candidate;
  }
  if (owner == nullptr) {
    return absl::NotFoundError(absl::StrCat("terrain '", terrain.name, "' is not in tileset '",
                                            tileset_->name, "', so it cannot own new artwork"));
  }
  // The key travels with the tile. It is what the renderer was given, so it is
  // what has to be given again to redraw this tile when the recipe changes --
  // and nothing else records what the picture is of.
  owner->derived_tiles.push_back(DerivedTile{.tile_id = tile_id, .key = key});

  RETURN_IF_ERROR(content_.Insert(artwork, tile_id));
  ++appended_;
  return tile_id;
}

absl::StatusOr<TerrainPreview> DerivedTileProvider::PreviewForKey(const Terrain& terrain,
                                                                  const TerrainCellKey& key,
                                                                  int tile_x, int tile_y) {
  if (terrain.scheme != TerrainScheme::kDerived) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain '", terrain.name, "' is not derived and has no recipe to render"));
  }

  if (auto memo = tile_by_key_.find(key); memo != tile_by_key_.end()) {
    return TerrainPreview{.tile_id = memo->second};
  }
  if (auto shown = preview_by_key_.find(key); shown != preview_by_key_.end()) {
    return TerrainPreview{.artwork = shown->second};
  }

  ASSIGN_OR_RETURN(RgbaImage artwork,
                   renderer_.RenderShapeTileInContext(key.shape, key.neighbors, key.phase));
  if (const std::optional<int> existing = content_.Find(artwork); existing.has_value()) {
    // Worth memoizing: the pixels settled the question, and painting this cell
    // would reach the same answer without rendering again.
    tile_by_key_.emplace(key, *existing);
    return TerrainPreview{.tile_id = *existing};
  }

  preview_by_key_.emplace(key, artwork);
  return TerrainPreview{.artwork = std::move(artwork)};
}

absl::StatusOr<int> DerivedTileProvider::TileForKey(const Terrain& terrain,
                                                    const TerrainCellKey& key, int tile_x,
                                                    int tile_y) {
  if (terrain.scheme != TerrainScheme::kDerived) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain '", terrain.name, "' is not derived and has no recipe to render"));
  }

  if (auto memo = tile_by_key_.find(key); memo != tile_by_key_.end()) return memo->second;

  // A preview of this cell has usually just rendered exactly this picture, so
  // clicking after hovering costs no second render.
  RgbaImage artwork;
  if (auto shown = preview_by_key_.find(key); shown != preview_by_key_.end()) {
    artwork = std::move(shown->second);
    preview_by_key_.erase(shown);
  } else {
    ASSIGN_OR_RETURN(artwork,
                     renderer_.RenderShapeTileInContext(key.shape, key.neighbors, key.phase));
  }

  // A key that renders to a picture already in the atlas is that tile. Nothing
  // asserts which keys collide; the pixels do.
  if (const std::optional<int> existing = content_.Find(artwork); existing.has_value()) {
    tile_by_key_.emplace(key, *existing);
    return *existing;
  }

  ASSIGN_OR_RETURN(const int tile_id, AppendTile(terrain, key, artwork));
  tile_by_key_.emplace(key, tile_id);
  return tile_id;
}

}  // namespace zebes
