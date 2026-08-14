#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/asset_catalog.h"
#include "objects/texture.h"
#include "absl/strings/string_view.h"
#include "objects/tileset.h"
#include "terrain/terrain_detect.h"

namespace zebes {

struct AtlasCell {
  int source_x = 0;
  int source_y = 0;

  bool operator==(const AtlasCell& other) const = default;
};

// Owns TilesetEditor's authoring state and calculations without depending on
// ImGui, SDL, or the application API.
class TilesetEditorModel {
 public:
  using TilesetCatalog = std::map<AssetCatalogKey, Tileset>;
  using TextureCatalog = std::map<AssetCatalogKey, Texture>;

  void SetTilesets(std::vector<Tileset> tilesets);
  void SetTextures(std::vector<Texture> textures);
  const TilesetCatalog& tilesets() const { return tilesets_; }
  const TextureCatalog& textures() const { return textures_; }

  absl::Status SelectTileset(const std::string& id);
  void ClearTilesetSelection();
  const std::string& selected_tileset_id() const { return selected_tileset_id_; }
  bool has_tileset_selection() const { return !selected_tileset_id_.empty(); }

  void BeginNewTileset();
  absl::Status BeginEditingSelectedTileset();
  void CloseActiveTileset();
  bool has_active_tileset() const { return active_tileset_.has_value(); }

  // Whether the tileset being edited differs from the state it was opened or
  // last saved at. Closing the editor throws those edits away, so the panel
  // needs to know whether there is anything to warn about; asking the model
  // keeps that judgement out of the view.
  bool has_unsaved_changes() const;
  bool is_new_tileset() const;
  Tileset* active_tileset();
  const Tileset* active_tileset() const;

  absl::StatusOr<Tileset> BuildSaveRequest() const;
  absl::Status FinishSave(const std::string& saved_id);
  void FinishDelete();

  absl::Status SelectTexture(const std::string& texture_id);
  const Texture* active_texture() const;

  absl::Status SelectTile(int tile_id);
  void ClearTileSelection();
  int selected_tile_id() const { return selected_tile_id_; }
  Tile* selected_tile();
  const Tile* selected_tile() const;
  absl::Status AddTile();
  absl::Status DeleteSelectedTile();

  // Adds every tile and the terrain described by a compose_blob47 manifest.
  // This is the exact import path: the manifest states which mask each atlas
  // cell depicts, so nothing is inferred from layout.
  //
  // Defining a 47-tile terrain by hand would take 47 add-and-click cycles, so
  // this is the only practical way to author one.
  absl::Status ImportTerrainManifest(absl::string_view manifest_json);

  // Splices an already-built terrain into the active tileset. Callers that
  // build their own candidate -- the procedural generator does -- must number
  // it with NextTileId() and NextTerrainId() first, so that adding a terrain
  // never renumbers the tiles already defined.
  absl::Status AddTerrainCandidate(TerrainCandidate candidate);
  int NextTileId() const;
  int NextTerrainId() const;

  // Fallback for atlases with no manifest: scans the tiles already defined on
  // the active tileset for blob-47 blocks and adds a terrain for each. Returns
  // the number of terrains added, which is zero for hand-authored tilesets.
  absl::StatusOr<int> DetectTerrains();

  absl::Status DeleteTerrain(int terrain_id);

  // Marks a tile as belonging to a terrain for neighbour-masking purposes
  // without making it something the brush paints. This is how hand-drawn
  // set-pieces such as slopes stop reading as empty air to painted ground.
  //
  // An empty terrain_id removes the tile from every terrain. A tile the brush
  // already paints cannot also be a member.
  absl::Status SetTileTerrainMembership(int tile_id, std::optional<int> terrain_id);

  // Returns the terrain the tile is a member of, or empty.
  std::optional<int> GetTileTerrainMembership(int tile_id) const;

  absl::StatusOr<AtlasCell> CalculateAtlasCell(double world_x, double world_y, int texture_width,
                                               int texture_height) const;
  absl::Status SetSelectedTileSource(AtlasCell cell);

  // Adds one tile per atlas cell in the rectangle spanned by two corners, given
  // in either order, and returns how many were added.
  //
  // Cutting an atlas by hand costs roughly four interactions per tile: Add in
  // the navigator, click the cell in the viewport, then name it and pick a
  // shape. A sheet of forty cells is therefore not worth doing, which is why
  // the import and generate paths exist at all. One drag replaces the first two
  // steps for the whole region.
  //
  // Cells an existing tile already sources from are skipped, so dragging over
  // work already done adds only what is missing rather than duplicating it.
  absl::StatusOr<int> AddTilesForRegion(AtlasCell first, AtlasCell last);

 private:
  const Tileset* FindTileset(const std::string& id) const;
  const Texture* FindTexture(const std::string& id) const;

  TilesetCatalog tilesets_;
  TextureCatalog textures_;
  std::string selected_tileset_id_;
  std::optional<Tileset> active_tileset_;
  // The active tileset as it stood when editing began or when it was last
  // saved. Comparing against a snapshot rather than setting a dirty flag means
  // no mutating path can forget to mark itself, and editing a field back to its
  // original value correctly reports clean.
  std::optional<Tileset> baseline_tileset_;
  int selected_tile_id_ = 0;
};

}  // namespace zebes
