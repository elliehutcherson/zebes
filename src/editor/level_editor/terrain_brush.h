#pragma once

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/level.h"
#include "objects/tileset.h"

namespace zebes {

// Reverse lookup from tile ID to the terrain that owns it.
//
// The Level Editor rebuilds this each frame from Api-owned tileset storage, so
// the referenced tileset must outlive the index. Tiles absent from the index
// belong to no terrain and are treated as foreign material by the brush.
class TerrainIndex {
 public:
  // Fails when a tile belongs to more than one terrain, which would make a
  // painted cell's neighbourhood ambiguous.
  static absl::StatusOr<TerrainIndex> Build(const Tileset& tileset);

  // Returns the terrain owning tile_id, counting both painted tiles and
  // hand-placed members such as slopes. Use this when deciding whether a
  // neighbour is the same material. Tile ID 0 is always null.
  const Terrain* FindByTileId(int tile_id) const;

  const Terrain* FindById(int terrain_id) const;

  // The collision shape of a tile, or kNone when it belongs to no terrain.
  //
  // This is what lets a neighbourhood be described by shape rather than by a
  // single "same terrain" bit, and what lets a refresh hand a cell back the
  // geometry it already had instead of choosing new geometry for it.
  TileShape ShapeOfTile(int tile_id) const;

  // The tile a terrain uses for a shape its mask-keyed rules do not produce,
  // such as an authored slope unit.
  std::optional<int> FindShapeTile(const Terrain& terrain, TileShape shape) const;

  // Records a tile a provider invented while resolving a cell.
  //
  // A derived terrain renders artwork on demand, so a tile can come into
  // existence in the middle of a paint. Until the index knows about it, the
  // cell holding it reads back as foreign material -- so refreshing that cell's
  // neighbours would draw them against air where their own ground is. Rebuilding
  // the index between resolving a cell and refreshing around it would work too,
  // and this is the same fact learned one tile at a time.
  //
  // Idempotent: a tile already claimed by this terrain is left alone.
  absl::Status NoteResolvedTile(int tile_id, const Terrain& terrain, TileShape shape);

 private:
  // What a tile ID means to the terrain that claims it.
  //
  // There used to be a paintable flag here, marking tiles the brush counted but
  // must never rewrite -- hand-placed slopes, which a refresh would otherwise
  // replace with a blob tile. Refreshes now hand a cell back the shape it
  // already had, so re-resolving a slope returns that same slope and the flag
  // guarded nothing. It also stopped derived terrain refreshing at all, since
  // every one of its tiles is owned rather than rule-produced.
  struct TileOwnership {
    const Terrain* terrain = nullptr;
    TileShape shape = TileShape::kNone;
  };

  // Records every tile a terrain owns, rejecting tiles claimed twice.
  absl::Status IndexTerrainTiles(const Terrain& terrain, const Tileset& tileset);
  absl::Status ClaimTile(int tile_id, const Terrain& terrain, const Tileset& tileset);

  absl::flat_hash_map<int, TileOwnership> tile_ownership_;
  absl::flat_hash_map<int, const Terrain*> terrain_by_id_;
  // (terrain id, shape) -> tile, for shapes the rule table does not cover.
  absl::flat_hash_map<std::pair<int, TileShape>, int> shape_tiles_;
};

// Resolves the artwork for a cell whose collision geometry is already decided.
//
// Two schemes answer this differently -- a blob-47 terrain looks the key's mask
// up in its rule table, a derived terrain renders the artwork on demand -- and
// the brush must not know which. Keeping it an interface is also what keeps
// terrain_brush free of the generator: rendering is an algorithm with its own
// build unit and headless tests, and painting a cell should not link it.
class TerrainTileProvider {
 public:
  virtual ~TerrainTileProvider() = default;

  // The tile whose artwork depicts `key`.
  //
  // tile_x and tile_y say which cell is asking. They do not change what the
  // artwork must depict -- that is entirely `key` -- but a terrain carrying
  // several interchangeable drawings of one neighbourhood picks between them by
  // coordinate, so the same cell always gets the same one and repainting a
  // region never reshuffles it.
  virtual absl::StatusOr<int> TileForKey(const Terrain& terrain, const TerrainCellKey& key,
                                         int tile_x, int tile_y) = 0;
};

// The provider for terrain whose artwork was authored against a neighbour mask.
//
// A mask is the complete truth about a drawing made from quadrants, so nothing
// is lost by projecting the key down to one here. Shapes the rule table does
// not cover resolve through the terrain's authored shape tiles.
class Blob47TileProvider : public TerrainTileProvider {
 public:
  explicit Blob47TileProvider(const TerrainIndex& index) : index_(index) {}

  absl::StatusOr<int> TileForKey(const Terrain& terrain, const TerrainCellKey& key, int tile_x,
                                 int tile_y) override;

 private:
  const TerrainIndex& index_;
};

// Describes what artwork the cell at (tile_x, tile_y) must depict.
//
// A neighbour contributes its own shape when it holds a tile of the same
// terrain, and air otherwise, so the key says "a wedge is there" where a mask
// could only say "something of mine is there". Coordinates outside the level
// follow Terrain::solid_outside_level, which is what keeps ground continuous at
// the world border.
//
// `shape` is the cell's own collision geometry and belongs to the caller: on a
// fresh paint it comes from the placement unit, and on a refresh it is what the
// cell already has. The brush reads geometry and never chooses it.
absl::StatusOr<TerrainCellKey> ComputeTerrainCellKey(const Level& level, const TerrainIndex& index,
                                                     const Terrain& terrain, TileShape shape,
                                                     int tile_x, int tile_y);

// The normalized neighbour mask for a cell painted with terrain.
//
// This is ComputeTerrainCellKey projected down to one bit per neighbour, which
// is all a blob-47 terrain's artwork was authored against.
absl::StatusOr<uint8_t> ComputeTerrainMask(const Level& level, const TerrainIndex& index,
                                           const Terrain& terrain, int tile_x, int tile_y);

// Picks a tile for a rule deterministically from the cell's coordinates.
//
// The same cell always yields the same tile, so repainting a region never
// reshuffles its artwork and tests observe stable results.
//
// Terrain::variant_period decides how: zero picks by weight from a hash of the
// coordinates, and a positive period lays the variants down as fixed phases of
// one repeating pattern instead.
absl::StatusOr<int> SelectVariant(const Terrain& terrain, const TerrainRule& rule, int tile_x,
                                  int tile_y);

// Recomputes the artwork for a cell already owned by terrain, without changing
// which terrain occupies it or the geometry it holds. Exposed for tests and for
// bulk refresh after a tileset edit.
absl::Status ResolveTerrainCell(Level& level, TerrainIndex& index, const Terrain& terrain,
                                TerrainTileProvider& provider, TileShape shape, int tile_x,
                                int tile_y);

// Writes terrain_id at (tile_x, tile_y) with the given collision geometry, and
// re-resolves the neighbouring cells of the same terrain so their edges and
// corners stay consistent.
//
// A refresh may change a neighbour's artwork; it can never change a neighbour's
// shape, because it hands each cell back the geometry that cell already had.
absl::Status PaintTerrain(Level& level, TerrainIndex& index, TerrainTileProvider& provider,
                          int terrain_id, TileShape shape, int tile_x, int tile_y);

// Clears the cell and re-resolves the neighbours that belonged to whatever
// terrain occupied it. Erasing a cell holding no terrain still clears it.
absl::Status EraseTerrain(Level& level, TerrainIndex& index, TerrainTileProvider& provider,
                          int tile_x, int tile_y);

}  // namespace zebes
