#include "editor/tileset_editor/tileset_editor_model.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "absl/status/status.h"

namespace zebes {

void TilesetEditorModel::SetTilesets(std::vector<Tileset> tilesets) {
  tilesets_.clear();
  for (Tileset& tileset : tilesets) {
    AssetCatalogKey key{.display_name = tileset.name, .id = tileset.id};
    tilesets_.emplace(std::move(key), std::move(tileset));
  }
  if (FindTileset(selected_tileset_id_) == nullptr) ClearTilesetSelection();
}

void TilesetEditorModel::SetTextures(std::vector<Texture> textures) {
  textures_.clear();
  for (Texture& texture : textures) {
    AssetCatalogKey key{.display_name = texture.name, .id = texture.id};
    textures_.emplace(std::move(key), std::move(texture));
  }
}

absl::Status TilesetEditorModel::SelectTileset(const std::string& id) {
  if (FindTileset(id) == nullptr) return absl::NotFoundError("Tileset was not found");
  selected_tileset_id_ = id;
  return absl::OkStatus();
}

void TilesetEditorModel::ClearTilesetSelection() { selected_tileset_id_.clear(); }

void TilesetEditorModel::BeginNewTileset() {
  active_tileset_ = Tileset{.name = "New Tileset", .tile_width = 16, .tile_height = 16};
  selected_tile_id_ = 0;
}

absl::Status TilesetEditorModel::BeginEditingSelectedTileset() {
  const Tileset* selected = FindTileset(selected_tileset_id_);
  if (selected == nullptr) return absl::FailedPreconditionError("No tileset is selected");
  active_tileset_ = *selected;
  selected_tile_id_ = 0;
  return absl::OkStatus();
}

void TilesetEditorModel::CloseActiveTileset() {
  active_tileset_.reset();
  selected_tile_id_ = 0;
}

bool TilesetEditorModel::is_new_tileset() const {
  return active_tileset_.has_value() && active_tileset_->id.empty();
}

Tileset* TilesetEditorModel::active_tileset() {
  return active_tileset_.has_value() ? &*active_tileset_ : nullptr;
}

const Tileset* TilesetEditorModel::active_tileset() const {
  return active_tileset_.has_value() ? &*active_tileset_ : nullptr;
}

absl::StatusOr<Tileset> TilesetEditorModel::BuildSaveRequest() const {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  return *active_tileset_;
}

absl::Status TilesetEditorModel::FinishSave(const std::string& saved_id) {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  if (saved_id.empty()) return absl::InvalidArgumentError("Saved tileset ID cannot be empty");
  active_tileset_->id = saved_id;
  selected_tileset_id_ = saved_id;
  return absl::OkStatus();
}

void TilesetEditorModel::FinishDelete() {
  CloseActiveTileset();
  ClearTilesetSelection();
}

absl::Status TilesetEditorModel::SelectTexture(const std::string& texture_id) {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  if (FindTexture(texture_id) == nullptr) return absl::NotFoundError("Texture was not found");
  active_tileset_->texture_id = texture_id;
  return absl::OkStatus();
}

const Texture* TilesetEditorModel::active_texture() const {
  if (!active_tileset_.has_value()) return nullptr;
  return FindTexture(active_tileset_->texture_id);
}

absl::Status TilesetEditorModel::SelectTile(int tile_id) {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  for (const Tile& tile : active_tileset_->tiles) {
    if (tile.id != tile_id) continue;
    selected_tile_id_ = tile_id;
    return absl::OkStatus();
  }
  return absl::NotFoundError("Tile was not found");
}

void TilesetEditorModel::ClearTileSelection() { selected_tile_id_ = 0; }

Tile* TilesetEditorModel::selected_tile() {
  if (!active_tileset_.has_value()) return nullptr;
  for (Tile& tile : active_tileset_->tiles) {
    if (tile.id == selected_tile_id_) return &tile;
  }
  return nullptr;
}

const Tile* TilesetEditorModel::selected_tile() const {
  if (!active_tileset_.has_value()) return nullptr;
  for (const Tile& tile : active_tileset_->tiles) {
    if (tile.id == selected_tile_id_) return &tile;
  }
  return nullptr;
}

absl::Status TilesetEditorModel::AddTile() {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  int maximum_id = 0;
  for (const Tile& tile : active_tileset_->tiles) maximum_id = std::max(maximum_id, tile.id);
  if (maximum_id == std::numeric_limits<int>::max()) {
    return absl::ResourceExhaustedError("No tile IDs remain");
  }
  selected_tile_id_ = maximum_id + 1;
  active_tileset_->tiles.push_back(Tile{.id = selected_tile_id_});
  return absl::OkStatus();
}

absl::Status TilesetEditorModel::DeleteSelectedTile() {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  auto tile =
      std::find_if(active_tileset_->tiles.begin(), active_tileset_->tiles.end(),
                   [this](const Tile& candidate) { return candidate.id == selected_tile_id_; });
  if (tile == active_tileset_->tiles.end()) return absl::OkStatus();
  active_tileset_->tiles.erase(tile);
  selected_tile_id_ = 0;
  return absl::OkStatus();
}

absl::StatusOr<AtlasCell> TilesetEditorModel::CalculateAtlasCell(double world_x, double world_y,
                                                                 int texture_width,
                                                                 int texture_height) const {
  if (!active_tileset_.has_value()) {
    return absl::FailedPreconditionError("No tileset is being edited");
  }
  if (active_tileset_->tile_width <= 0 || active_tileset_->tile_height <= 0 || texture_width <= 0 ||
      texture_height <= 0) {
    return absl::InvalidArgumentError("Tile and texture dimensions must be positive");
  }
  if (!std::isfinite(world_x) || !std::isfinite(world_y)) {
    return absl::InvalidArgumentError("Atlas coordinates must be finite");
  }

  const double cell_x =
      std::floor(world_x / active_tileset_->tile_width) * active_tileset_->tile_width;
  const double cell_y =
      std::floor(world_y / active_tileset_->tile_height) * active_tileset_->tile_height;
  if (cell_x < 0.0 || cell_y < 0.0 || cell_x + active_tileset_->tile_width > texture_width ||
      cell_y + active_tileset_->tile_height > texture_height ||
      cell_x > std::numeric_limits<int>::max() || cell_y > std::numeric_limits<int>::max()) {
    return absl::OutOfRangeError("Atlas cell is outside the texture");
  }
  return AtlasCell{.source_x = static_cast<int>(cell_x), .source_y = static_cast<int>(cell_y)};
}

absl::Status TilesetEditorModel::SetSelectedTileSource(AtlasCell cell) {
  Tile* tile = selected_tile();
  if (tile == nullptr) return absl::FailedPreconditionError("No tile is selected");
  tile->source_x = cell.source_x;
  tile->source_y = cell.source_y;
  return absl::OkStatus();
}

const Tileset* TilesetEditorModel::FindTileset(const std::string& id) const {
  for (const auto& catalog_entry : tilesets_) {
    if (catalog_entry.second.id == id) return &catalog_entry.second;
  }
  return nullptr;
}

const Texture* TilesetEditorModel::FindTexture(const std::string& id) const {
  for (const auto& catalog_entry : textures_) {
    if (catalog_entry.second.id == id) return &catalog_entry.second;
  }
  return nullptr;
}

}  // namespace zebes
