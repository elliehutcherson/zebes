#pragma once

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "objects/tileset.h"

namespace zebes {

// A terrain ready to be added to a tileset, plus any tiles it needs created.
struct TerrainCandidate {
  // Display name proposed to the user, who may rename before accepting.
  std::string suggested_name;

  // Fully populated rule table. Variants already reference the tile IDs the
  // candidate expects, whether those tiles exist or are listed below.
  Terrain terrain;

  // Tiles the caller must add to the tileset. Empty when the candidate was
  // detected from tiles that already exist.
  std::vector<Tile> tiles;
};

// Builds tiles and a terrain from a compose_blob47 manifest. This is the exact
// import path: the manifest states which mask each atlas cell depicts, so no
// layout guessing is involved.
//
// Tile IDs are assigned sequentially from first_tile_id so the caller can
// splice the result into an existing tileset without renumbering.
absl::StatusOr<TerrainCandidate> ImportBlob47Manifest(absl::string_view manifest_json,
                                                      int first_tile_id, int terrain_id);

// Fallback for atlases with no manifest, such as purchased packs or hand-made
// sheets: scans the tileset's existing tiles for contiguous
// kBlob47Columns x kBlob47Rows blocks holding all kBlob47TileCount cells in
// row-major order, and assigns Blob47MaskTable()[index] to each.
//
// Blocks stacked directly below one another are merged as additional variants,
// matching the layout compose_blob47 emits. Returns an empty vector when the
// tileset holds no such block, which is the expected result for a hand-authored
// tileset of slopes and one-off pieces.
absl::StatusOr<std::vector<TerrainCandidate>> DetectBlob47Terrains(const Tileset& tileset);

}  // namespace zebes
