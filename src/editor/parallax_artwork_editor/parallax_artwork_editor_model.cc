#include "editor/parallax_artwork_editor/parallax_artwork_editor_model.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "absl/status/status.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "terrain/terrain_palette.h"

namespace zebes {
namespace {

const ParallaxArtworkStageDiagnostic* FindDiagnostic(const ParallaxArtworkPipelineResult& artwork,
                                                     ParallaxArtworkStage stage) {
  const auto found = std::find_if(artwork.stages.begin(), artwork.stages.end(),
                                  [stage](const ParallaxArtworkStageDiagnostic& diagnostic) {
                                    return diagnostic.stage == stage;
                                  });
  return found == artwork.stages.end() ? nullptr : &*found;
}

}  // namespace

const char* ParallaxArtworkPreviewStageLabel(ParallaxArtworkPreviewStage stage) {
  switch (stage) {
    case ParallaxArtworkPreviewStage::kSource:
      return "Accepted source";
    case ParallaxArtworkPreviewStage::kFraming:
      return "Framing";
    case ParallaxArtworkPreviewStage::kMatteExtraction:
      return "Matte extraction";
    case ParallaxArtworkPreviewStage::kRasterization:
      return "Rasterization";
    case ParallaxArtworkPreviewStage::kFinished:
      return "Finished output";
    case ParallaxArtworkPreviewStage::kRepeatX:
      return "Horizontal repetition";
    case ParallaxArtworkPreviewStage::kRepeatY:
      return "Vertical repetition";
  }
  return "Unknown";
}

void ParallaxArtworkEditorModel::ClearPrepared() {
  prepared_.emplace<std::monostate>();
  prepared_committed_ = false;
  preview_stage_ = ParallaxArtworkPreviewStage::kSource;
}

void ParallaxArtworkEditorModel::MarkInputsChanged() {
  ++revision_;
  ClearPrepared();
}

absl::Status ParallaxArtworkEditorModel::SelectSource(SourceArtwork source, RgbaImage pixels) {
  if (active_recipe_.has_value()) {
    return absl::FailedPreconditionError(
        "existing parallax artwork keeps its retained source; use Save As first");
  }
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (!pixels.IsValid() || pixels.width != source.width || pixels.height != source.height) {
    return absl::FailedPreconditionError("selected source pixels do not match its definition");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  if (digest != source.content_digest) {
    return absl::FailedPreconditionError("selected source pixels do not match its digest");
  }
  source_to_open_ = source.id;
  source_ = std::move(source);
  source_pixels_ = std::move(pixels);
  MarkInputsChanged();
  return absl::OkStatus();
}

absl::Status ParallaxArtworkEditorModel::AttachTerrain(const TerrainRecipe& terrain_recipe) {
  ASSIGN_OR_RETURN(const ResolvedTerrainPalette palette,
                   ResolveTerrainPalette(terrain_recipe.config));
  settings_.terrain_recipe_id = terrain_recipe.id;
  settings_.style.palette = palette.OpaqueColors();
  settings_.style.quantize_to_palette = true;
  terrain_recipe_ = terrain_recipe;
  has_style_ = true;
  MarkInputsChanged();
  return absl::OkStatus();
}

void ParallaxArtworkEditorModel::DetachTerrain() {
  if (!terrain_recipe_.has_value() && !settings_.terrain_recipe_id.has_value()) return;
  terrain_recipe_.reset();
  settings_.terrain_recipe_id.reset();
  MarkInputsChanged();
}

absl::Status ParallaxArtworkEditorModel::LoadRecipe(const ParallaxArtworkRecipe& recipe,
                                                    SourceArtwork source, RgbaImage pixels,
                                                    std::optional<TerrainRecipe> terrain_recipe) {
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (recipe.source_artwork_id != source.id) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe and retained source do not match");
  }
  if (recipe.terrain_recipe_id.has_value()) {
    if (!terrain_recipe.has_value() || terrain_recipe->id != *recipe.terrain_recipe_id) {
      return absl::FailedPreconditionError(
          "parallax artwork recipe's attached terrain is unavailable");
    }
  } else if (terrain_recipe.has_value()) {
    return absl::InvalidArgumentError(
        "detached parallax artwork recipe was given an attached terrain");
  }
  if (!pixels.IsValid() || pixels.width != source.width || pixels.height != source.height) {
    return absl::FailedPreconditionError("retained source pixels do not match their definition");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  if (digest != source.content_digest) {
    return absl::FailedPreconditionError("retained source pixels do not match their digest");
  }

  name_ = recipe.name;
  source_to_open_ = source.id;
  recipe_to_open_ = recipe.id;
  source_ = std::move(source);
  source_pixels_ = std::move(pixels);
  terrain_recipe_ = std::move(terrain_recipe);
  active_recipe_ = recipe;
  settings_ = ParallaxArtworkRegenerationSettings{
      .terrain_recipe_id = recipe.terrain_recipe_id,
      .style = recipe.style,
      .pipeline = recipe.pipeline,
  };
  has_style_ = true;
  status_.clear();
  ++revision_;
  ClearPrepared();
  return absl::OkStatus();
}

void ParallaxArtworkEditorModel::StartNewRecipe() { *this = ParallaxArtworkEditorModel(); }

void ParallaxArtworkEditorModel::StartRecipeCopy() {
  if (!active_recipe_.has_value()) return;
  active_recipe_.reset();
  recipe_to_open_.clear();
  name_ += " copy";
  MarkInputsChanged();
  SetInfo("This copy will create new texture and recipe IDs.");
}

bool ParallaxArtworkEditorModel::CanPrepare() const {
  return !name_.empty() && source_.has_value() && source_pixels_.has_value() && has_style_;
}

bool ParallaxArtworkEditorModel::HasPreparedResult() const {
  return !std::holds_alternative<std::monostate>(prepared_);
}

bool ParallaxArtworkEditorModel::HasUncommittedPreparedResult() const {
  return HasPreparedResult() && !prepared_committed_;
}

const PreparedParallaxArtworkAsset* ParallaxArtworkEditorModel::prepared_creation() const {
  return std::get_if<PreparedParallaxArtworkAsset>(&prepared_);
}

const PreparedParallaxArtworkRegeneration* ParallaxArtworkEditorModel::prepared_regeneration()
    const {
  return std::get_if<PreparedParallaxArtworkRegeneration>(&prepared_);
}

absl::Status ParallaxArtworkEditorModel::AcceptPrepared(uint64_t revision,
                                                        PreparedParallaxArtworkAsset prepared) {
  if (revision != revision_) {
    return absl::FailedPreconditionError(
        "discarded parallax artwork preview for superseded settings");
  }
  if (active_recipe_.has_value()) {
    return absl::FailedPreconditionError("creation result cannot replace open parallax artwork");
  }
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkAsset(prepared));
  if (!source_.has_value() || prepared.source.id != source_->id) {
    return absl::FailedPreconditionError(
        "prepared parallax artwork used a different retained source");
  }
  prepared_.emplace<PreparedParallaxArtworkAsset>(std::move(prepared));
  prepared_committed_ = false;
  preview_stage_ = ParallaxArtworkPreviewStage::kFinished;
  return absl::OkStatus();
}

absl::Status ParallaxArtworkEditorModel::AcceptPrepared(
    uint64_t revision, PreparedParallaxArtworkRegeneration prepared) {
  if (revision != revision_) {
    return absl::FailedPreconditionError(
        "discarded parallax artwork preview for superseded settings");
  }
  if (!active_recipe_.has_value() || active_recipe_->id != prepared.recipe_snapshot.id) {
    return absl::FailedPreconditionError(
        "regeneration result no longer matches the open parallax artwork");
  }
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkRegeneration(prepared));
  prepared_.emplace<PreparedParallaxArtworkRegeneration>(std::move(prepared));
  prepared_committed_ = false;
  preview_stage_ = ParallaxArtworkPreviewStage::kFinished;
  return absl::OkStatus();
}

void ParallaxArtworkEditorModel::BindCommittedRecipe(const ParallaxArtworkRecipe& recipe) {
  active_recipe_ = recipe;
  recipe_to_open_ = recipe.id;
  name_ = recipe.name;
  prepared_committed_ = HasPreparedResult();
}

const ParallaxArtworkPipelineResult* ParallaxArtworkEditorModel::Artwork() const {
  if (const PreparedParallaxArtworkAsset* creation = prepared_creation(); creation != nullptr) {
    return &creation->artwork;
  }
  if (const PreparedParallaxArtworkRegeneration* regeneration = prepared_regeneration();
      regeneration != nullptr) {
    return &regeneration->artwork;
  }
  return nullptr;
}

std::vector<ParallaxArtworkPreviewStage> ParallaxArtworkEditorModel::AvailablePreviewStages()
    const {
  std::vector<ParallaxArtworkPreviewStage> stages = {ParallaxArtworkPreviewStage::kSource};
  const ParallaxArtworkPipelineResult* artwork = Artwork();
  if (artwork == nullptr) return stages;
  stages.insert(
      stages.end(),
      {ParallaxArtworkPreviewStage::kFraming, ParallaxArtworkPreviewStage::kMatteExtraction,
       ParallaxArtworkPreviewStage::kRasterization, ParallaxArtworkPreviewStage::kFinished});
  if (artwork->repeat_x_preview.has_value()) {
    stages.push_back(ParallaxArtworkPreviewStage::kRepeatX);
  }
  if (artwork->repeat_y_preview.has_value()) {
    stages.push_back(ParallaxArtworkPreviewStage::kRepeatY);
  }
  return stages;
}

void ParallaxArtworkEditorModel::SetPreviewStage(ParallaxArtworkPreviewStage stage) {
  const std::vector<ParallaxArtworkPreviewStage> stages = AvailablePreviewStages();
  if (std::find(stages.begin(), stages.end(), stage) != stages.end()) preview_stage_ = stage;
}

void ParallaxArtworkEditorModel::PreviousPreviewStage() {
  const std::vector<ParallaxArtworkPreviewStage> stages = AvailablePreviewStages();
  const auto current = std::find(stages.begin(), stages.end(), preview_stage_);
  if (current != stages.end() && current != stages.begin()) preview_stage_ = *(current - 1);
}

void ParallaxArtworkEditorModel::NextPreviewStage() {
  const std::vector<ParallaxArtworkPreviewStage> stages = AvailablePreviewStages();
  const auto current = std::find(stages.begin(), stages.end(), preview_stage_);
  if (current != stages.end() && current + 1 != stages.end()) preview_stage_ = *(current + 1);
}

const RgbaImage* ParallaxArtworkEditorModel::PreviewImage() const {
  const ParallaxArtworkPipelineResult* artwork = Artwork();
  switch (preview_stage_) {
    case ParallaxArtworkPreviewStage::kSource:
      return source_pixels_.has_value() ? &*source_pixels_ : nullptr;
    case ParallaxArtworkPreviewStage::kFraming:
      return artwork == nullptr ? nullptr : &artwork->framed;
    case ParallaxArtworkPreviewStage::kMatteExtraction:
      return artwork == nullptr ? nullptr : &artwork->matte_extracted;
    case ParallaxArtworkPreviewStage::kRasterization:
      return artwork == nullptr ? nullptr : &artwork->rasterized;
    case ParallaxArtworkPreviewStage::kFinished:
      return artwork == nullptr ? nullptr : &artwork->finished;
    case ParallaxArtworkPreviewStage::kRepeatX:
      return artwork == nullptr || !artwork->repeat_x_preview.has_value()
                 ? nullptr
                 : &*artwork->repeat_x_preview;
    case ParallaxArtworkPreviewStage::kRepeatY:
      return artwork == nullptr || !artwork->repeat_y_preview.has_value()
                 ? nullptr
                 : &*artwork->repeat_y_preview;
  }
  return nullptr;
}

const ParallaxArtworkStageDiagnostic* ParallaxArtworkEditorModel::PreviewDiagnostic() const {
  const ParallaxArtworkPipelineResult* artwork = Artwork();
  if (artwork == nullptr) return nullptr;
  switch (preview_stage_) {
    case ParallaxArtworkPreviewStage::kFraming:
      return FindDiagnostic(*artwork, ParallaxArtworkStage::kFraming);
    case ParallaxArtworkPreviewStage::kMatteExtraction:
      return FindDiagnostic(*artwork, ParallaxArtworkStage::kMatteExtraction);
    case ParallaxArtworkPreviewStage::kRasterization:
      return FindDiagnostic(*artwork, ParallaxArtworkStage::kRasterization);
    case ParallaxArtworkPreviewStage::kFinished:
      return FindDiagnostic(*artwork, ParallaxArtworkStage::kFinished);
    case ParallaxArtworkPreviewStage::kSource:
    case ParallaxArtworkPreviewStage::kRepeatX:
    case ParallaxArtworkPreviewStage::kRepeatY:
      return nullptr;
  }
  return nullptr;
}

const RepetitionDiagnostics* ParallaxArtworkEditorModel::repetition_diagnostics() const {
  const ParallaxArtworkPipelineResult* artwork = Artwork();
  return artwork == nullptr ? nullptr : &artwork->repetition;
}

}  // namespace zebes
