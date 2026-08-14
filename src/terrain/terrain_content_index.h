#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "objects/tileset.h"

namespace zebes {

// Which tile of an atlas already holds a given picture.
//
// Derived terrain artwork is deduplicated by content rather than by key. Two
// cell keys often render identically -- a ramp meeting a wall and a ramp
// meeting another ramp measure zero pixels apart -- and encoding that as a rule
// would be a claim about the renderer needing re-proof every time the renderer
// changed. Rendering first and comparing the result is true by construction.
//
// The index is derived, never stored: it is rebuilt from the atlas at load, so
// nothing new goes on disk and there is no cache to invalidate. PNG is
// lossless, which is what makes a byte comparison across sessions meaningful.
//
// Pixels are the key, not a hash of them, so two different pictures can never
// alias. A tileset's whole atlas is under a megabyte at the sizes in use, and
// paying that to remove a collision case is the right trade.
class TerrainContentIndex {
 public:
  // Indexes every tile of `tileset` by the pixels it occupies in `atlas`.
  //
  // A tile whose source rect falls outside the atlas is a corrupt tileset and
  // is reported, not skipped: silently dropping it would let the caller append
  // a duplicate of a tile that was already there.
  static absl::StatusOr<TerrainContentIndex> Build(const Tileset& tileset, const RgbaImage& atlas);

  // The tile already holding exactly these pixels, if any.
  std::optional<int> Find(const RgbaImage& tile) const;

  // Records a tile just appended to the atlas. Fails when those pixels are
  // already claimed, because that means the caller skipped a Find that would
  // have reused the existing tile.
  absl::Status Insert(const RgbaImage& tile, int tile_id);

  // Distinct pictures indexed. Lower than the tileset's tile count exactly when
  // that tileset holds tiles drawn identically.
  size_t size() const { return tile_by_content_.size(); }

 private:
  absl::flat_hash_map<std::vector<uint8_t>, int> tile_by_content_;
};

// Copies one tile-sized rect out of a larger image.
//
// Unlike CopyTile in blob47_compose this takes independent width and height,
// because a tileset's cells are not required to be square.
absl::StatusOr<RgbaImage> CropRegion(const RgbaImage& source, int x, int y, int width, int height);

}  // namespace zebes
