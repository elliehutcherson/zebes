#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "api/api.h"
#include "editor/level_editor/derived_tile_provider.h"
#include "objects/tileset.h"

namespace zebes {

// Keeps a derived terrain's artwork available while a level is painted.
//
// A derived terrain renders artwork for whatever neighbourhood a level asks
// for, so its atlas and tileset grow during an edit. This owns that growth and
// the two separate questions it raises:
//
//  - Visible. A cell referencing a tile the GPU has not seen renders as a hole,
//    so new artwork is uploaded as soon as it appears.
//  - Durable. Nothing reaches disk until the level is saved, so abandoning an
//    edit leaves no artwork behind that no level references. Artwork is part of
//    the work, and unsaved work stays unsaved.
//
// The session is long-lived: the provider carries a content index rebuilt from
// the atlas and a memo of everything rendered this session, both of which would
// be thrown away by rebuilding it per frame.
class DerivedTerrainSession {
 public:
  // Opens for `tileset`, or closes when it holds no derived terrain.
  //
  // `tileset` must outlive the session and is grown in place, because a level
  // stores tile IDs and exactly one tileset may decide what they mean while it
  // is being edited. Reopening for the tileset already open is free.
  //
  // A derived terrain whose recipe is missing is an error rather than a closed
  // session: its artwork cannot be rendered, so painting it would silently do
  // nothing.
  absl::Status OpenFor(Api& api, Tileset& tileset);

  // Null when closed, which is the signal to use the authored-artwork provider.
  TerrainTileProvider* provider();

  // Uploads artwork appended since the last call. Cheap and idempotent when
  // nothing was appended, so callers may run it every frame.
  absl::Status ShowNewArtwork(Api& api);

  // Writes the grown atlas and then the tileset.
  //
  // Must run before the level is saved: the level stores tile IDs, and a level
  // on disk referencing tiles that are not is a level that will not open. The
  // atlas is written first for the same reason one step down -- a tileset
  // naming artwork the atlas does not hold is worse than artwork nothing names.
  absl::Status Commit(Api& api);

  bool is_open() const { return provider_.has_value(); }
  bool has_unsaved_artwork() const;

 private:
  void Close();

  std::string tileset_id_;
  std::string texture_id_;
  std::optional<DerivedTileProvider> provider_;
  int shown_tiles_ = 0;
  int committed_tiles_ = 0;
};

}  // namespace zebes
