#pragma once

#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "editor/level_editor/terrain_brush.h"
#include "objects/tileset.h"
#include "terrain/terrain_content_index.h"
#include "terrain/terrain_generator.h"

namespace zebes {

// Resolves a derived terrain's artwork by rendering it, growing the atlas only
// by pictures that are genuinely new.
//
// This is the cache the whole phase is about. TerrainRenderer is a pure
// function of a cell's shape, its neighbours' shapes and the phase; the atlas
// used to be a cache of it keyed on a 47-mask, which cannot represent that
// domain, so AutoContext existed to guess what the key left out. Here the key
// is the real input and entries are filled in on demand, so the count is
// bounded by what levels actually contain rather than by combinatorics.
//
// Three layers answer a request, cheapest first:
//
//  1. A session memo on the key, so a key is rendered at most once per session.
//     It is deliberately not persisted: it is derived, and a cache on disk
//     would be another format to keep honest.
//  2. Content lookup, so two keys that render identically share one tile. The
//     comparison is exact, so a near miss -- a peak differs from a wall by two
//     pixels in a thousand -- keeps its own tile rather than collapsing.
//  3. Append, which is the only path that grows the atlas.
//
// The tileset is referenced, not copied, and must outlive the provider. A level
// stores tile IDs, so during an edit exactly one tileset may decide what those
// IDs mean -- a copy would let the viewport resolve an ID the brush had just
// invented, or fail to resolve one it had. The atlas is owned, because it is
// pixels the provider builds up rather than shared state.
//
// Growth is in memory. The caller shows new artwork as it appears and writes
// both the atlas and the tileset when the level is saved.
class DerivedTileProvider : public TerrainTileProvider {
 public:
  // `tileset` and `atlas` describe what already exists; `renderer` is built
  // from the terrain's recipe. The atlas must be a whole number of cells in
  // each direction, since a partial cell has no tile that could occupy it.
  static absl::StatusOr<DerivedTileProvider> Create(TerrainRenderer renderer, Tileset& tileset,
                                                    RgbaImage atlas);

  absl::StatusOr<int> TileForKey(const Terrain& terrain, const TerrainCellKey& key, int tile_x,
                                 int tile_y) override;

  // Renders the artwork and looks it up, but never appends.
  //
  // A miss hands the pixels back loose rather than placing them, so hovering
  // costs at most one render of a 32px tile and leaves the atlas exactly as it
  // was. Rendered images are cached by key, so holding the pointer still over a
  // novel cell renders once rather than once a frame.
  absl::StatusOr<TerrainPreview> PreviewForKey(const Terrain& terrain, const TerrainCellKey& key,
                                               int tile_x, int tile_y) override;

  // Whether anything was appended since Create. False means the caller has
  // nothing to write back, which is the common case once a level settles.
  bool has_uncommitted_tiles() const { return appended_ > 0; }
  int appended_tile_count() const { return appended_; }

  const Tileset& tileset() const { return *tileset_; }
  const RgbaImage& atlas() const { return atlas_; }

 private:
  DerivedTileProvider(TerrainRenderer renderer, Tileset& tileset, RgbaImage atlas,
                      TerrainContentIndex content, int columns);

  // Places artwork in the first free cell, growing the atlas by a row when the
  // last one fills, and records the tile against the terrain.
  absl::StatusOr<int> AppendTile(const Terrain& terrain, const TerrainCellKey& key,
                                 const RgbaImage& artwork);

  // The first cell no tile sources from, in row-major order.
  int FirstFreeCell() const;

  TerrainRenderer renderer_;
  Tileset* tileset_;
  RgbaImage atlas_;
  TerrainContentIndex content_;
  absl::flat_hash_map<TerrainCellKey, int> tile_by_key_;
  // Artwork rendered for a preview that had nowhere to go. Kept so that resting
  // the pointer on a novel cell does not re-render every frame, and dropped
  // wholesale once a key earns a tile.
  absl::flat_hash_map<TerrainCellKey, RgbaImage> preview_by_key_;
  int columns_ = 0;
  int appended_ = 0;
};

}  // namespace zebes
