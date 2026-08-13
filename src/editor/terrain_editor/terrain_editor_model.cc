#include "editor/terrain_editor/terrain_editor_model.h"

#include <utility>

#include "common/status_macros.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// Quality used while a control is being dragged. One sample per pixel redraws
// the whole scene in a few milliseconds, which is what makes a slider feel
// attached to the picture.
constexpr int kDraftSupersample = 1;

}  // namespace

void TerrainEditorModel::LoadRecipe(const TerrainRecipe& recipe) {
  active_recipe_ = recipe;
  config_ = recipe.config;
  name_ = recipe.name;
  selected_preset_ = recipe.source_preset;
  source_ = Source::kGenerate;
  recipe_to_open_ = recipe.id;
  result_ = CreatedTerrain{.texture_id = recipe.texture_id,
                           .tileset_id = recipe.tileset_id,
                           .recipe_id = recipe.id,
                           .tile_count = TileCount()};
  preview_stale_ = true;
  status_.clear();
}

void TerrainEditorModel::StartNewRecipe() { *this = TerrainEditorModel(); }

void TerrainEditorModel::StartRecipeCopy() {
  if (!active_recipe_.has_value()) return;
  active_recipe_.reset();
  recipe_to_open_.clear();
  result_.reset();
  name_ += " copy";
  status_ = "This copy will create new texture, tileset, terrain, and tile IDs.";
}

void TerrainEditorModel::ApplyPreset(const TerrainPreset& preset) {
  const int supersample = config_.supersample;
  const uint64_t seed = config_.seed;
  config_ = preset.config;
  config_.supersample = supersample;
  config_.seed = seed;
  selected_preset_ = preset.name;
}

void TerrainEditorModel::SetSource(Source source) {
  if (source_ == source) return;
  source_ = source;
  // An imported terrain's artwork already exists, so there is nothing to draw
  // here; dropping the image stops a stale generated scene from being shown
  // beside manifest controls that did not produce it.
  if (source_ == Source::kImportManifest) {
    // An imported manifest is not generated from this recipe. Keeping the
    // binding would turn its Create button into Regenerate and could overwrite
    // unrelated generated artwork.
    active_recipe_.reset();
    recipe_to_open_.clear();
    result_.reset();
    preview_.reset();
    return;
  }
  preview_stale_ = true;
}

int TerrainEditorModel::TileCount() const {
  const int phases = config_.variant_period * config_.variant_period;
  return kBlob47TileCount * phases + kSlopeShapeCount;
}

absl::Status TerrainEditorModel::RefreshPreviewIfNeeded(bool interacting) {
  if (source_ != Source::kGenerate) return absl::OkStatus();

  const bool settling = preview_is_draft_ && !interacting;
  if (!preview_stale_ && !settling) return absl::OkStatus();

  return RefreshPreview(/*draft=*/preview_stale_);
}

absl::Status TerrainEditorModel::RefreshPreview(bool draft) {
  TerrainGenConfig config = config_;
  if (draft) config.supersample = kDraftSupersample;

  // Cleared first so a failed configuration shows nothing rather than a picture
  // of the last one that worked.
  preview_stale_ = false;
  preview_is_draft_ = draft;

  absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
  if (!renderer.ok()) {
    preview_.reset();
    return renderer.status();
  }

  absl::StatusOr<RgbaImage> scene = RenderTerrainPreviewScene(*renderer);
  if (!scene.ok()) {
    preview_.reset();
    return scene.status();
  }

  preview_ = *std::move(scene);
  return absl::OkStatus();
}

}  // namespace zebes
