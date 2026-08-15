#include "editor/level_editor/derived_terrain_session.h"

#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

const Terrain* FindDerivedTerrain(const Tileset& tileset) {
  for (const Terrain& terrain : tileset.terrains) {
    if (terrain.scheme == TerrainScheme::kDerived) return &terrain;
  }
  return nullptr;
}

}  // namespace

void DerivedTerrainSession::Close() {
  provider_.reset();
  tileset_id_.clear();
  texture_id_.clear();
  shown_tiles_ = 0;
  committed_tiles_ = 0;
}

absl::Status DerivedTerrainSession::OpenFor(Api& api, Tileset& tileset) {
  if (is_open() && tileset_id_ == tileset.id) return absl::OkStatus();

  const Terrain* derived = FindDerivedTerrain(tileset);
  if (derived == nullptr) {
    Close();
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(const std::optional<TerrainRecipe> recipe,
                   api.FindTerrainRecipeForTileset(tileset.id));
  if (!recipe.has_value()) {
    return absl::FailedPreconditionError(
        absl::StrCat("terrain '", derived->name, "' derives its artwork but tileset ", tileset.id,
                     " has no recipe to render it from"));
  }
  if (tileset.texture_id.empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("tileset '", tileset.name, "' has no atlas to grow"));
  }

  ASSIGN_OR_RETURN(RgbaImage atlas, api.ReadTexturePixels(tileset.texture_id));
  ASSIGN_OR_RETURN(TerrainRenderer renderer, TerrainRenderer::Create(recipe->config));
  ASSIGN_OR_RETURN(DerivedTileProvider provider,
                   DerivedTileProvider::Create(std::move(renderer), tileset, std::move(atlas)));

  Close();
  provider_.emplace(std::move(provider));
  tileset_id_ = tileset.id;
  texture_id_ = tileset.texture_id;
  return absl::OkStatus();
}

TerrainTileProvider* DerivedTerrainSession::provider() {
  return provider_.has_value() ? &*provider_ : nullptr;
}

bool DerivedTerrainSession::has_unsaved_artwork() const {
  return provider_.has_value() && provider_->appended_tile_count() > committed_tiles_;
}

absl::Status DerivedTerrainSession::ShowNewArtwork(Api& api) {
  if (!provider_.has_value()) return absl::OkStatus();
  if (provider_->appended_tile_count() == shown_tiles_) return absl::OkStatus();

  const RgbaImage& atlas = provider_->atlas();
  RETURN_IF_ERROR(api.ShowTexturePixels(texture_id_, atlas.width, atlas.height, atlas.pixels));
  shown_tiles_ = provider_->appended_tile_count();
  return absl::OkStatus();
}

absl::Status DerivedTerrainSession::Commit(Api& api) {
  if (!has_unsaved_artwork()) return absl::OkStatus();

  const RgbaImage& atlas = provider_->atlas();
  RETURN_IF_ERROR(api.ReplaceTexturePixels(texture_id_, atlas.width, atlas.height, atlas.pixels));
  RETURN_IF_ERROR(api.UpdateTileset(provider_->tileset()));

  committed_tiles_ = provider_->appended_tile_count();
  // ReplaceTexturePixels reloads the handle from the durable file, so whatever
  // was uploaded for preview has been superseded by identical pixels.
  shown_tiles_ = committed_tiles_;
  return absl::OkStatus();
}

}  // namespace zebes
