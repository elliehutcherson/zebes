#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include "absl/status/status.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "terrain/terrain_palette.h"

namespace zebes {
namespace {

constexpr std::array<PropPreviewStage, 8> kPreviewStages = {
    PropPreviewStage::kSource,       PropPreviewStage::kIsolation,
    PropPreviewStage::kComposition,  PropPreviewStage::kRasterization,
    PropPreviewStage::kQuantization, PropPreviewStage::kEdgeTreatment,
    PropPreviewStage::kCleanup,      PropPreviewStage::kContext,
};

size_t StageIndex(PropPreviewStage stage) {
  for (size_t index = 0; index < kPreviewStages.size(); ++index) {
    if (kPreviewStages[index] == stage) return index;
  }
  return 0;
}

const RgbaImage* StageImage(const PropArtworkPipelineResult& artwork, PropPreviewStage stage) {
  switch (stage) {
    case PropPreviewStage::kSource:
      break;
    case PropPreviewStage::kIsolation:
      return &artwork.isolated;
    case PropPreviewStage::kComposition:
      return &artwork.composed.image;
    case PropPreviewStage::kRasterization:
      return &artwork.rasterized.image;
    case PropPreviewStage::kQuantization:
      return &artwork.quantized.image;
    case PropPreviewStage::kEdgeTreatment:
      return &artwork.edge_treated.image;
    case PropPreviewStage::kCleanup:
      return &artwork.finished.image;
    case PropPreviewStage::kContext:
      break;
  }
  return nullptr;
}

const PropArtwork* StageArtwork(const PropArtworkPipelineResult& artwork, PropPreviewStage stage) {
  switch (stage) {
    case PropPreviewStage::kComposition:
      return &artwork.composed;
    case PropPreviewStage::kRasterization:
      return &artwork.rasterized;
    case PropPreviewStage::kQuantization:
      return &artwork.quantized;
    case PropPreviewStage::kEdgeTreatment:
      return &artwork.edge_treated;
    case PropPreviewStage::kCleanup:
      return &artwork.finished;
    case PropPreviewStage::kSource:
    case PropPreviewStage::kIsolation:
    case PropPreviewStage::kContext:
      return nullptr;
  }
  return nullptr;
}

void DiscardIntermediateImages(PropArtworkPipelineResult& artwork) {
  artwork.isolated = RgbaImage{};
  artwork.composed = PropArtwork{};
  artwork.rasterized = PropArtwork{};
  artwork.quantized = PropArtwork{};
  artwork.edge_treated = PropArtwork{};
}

void ClampFreeAnchor(PropRegenerationSettings& settings) {
  PropAttachmentConfig& attachment = settings.pipeline.composition.attachment;
  if (attachment.mode != PropAttachmentMode::kFree || !attachment.free_anchor.has_value()) return;
  const int maximum_x =
      settings.style.tile_size * settings.pipeline.composition.canvas_tiles_wide - 1;
  const int maximum_y =
      settings.style.tile_size * settings.pipeline.composition.canvas_tiles_high - 1;
  attachment.free_anchor->x = std::clamp(attachment.free_anchor->x, 0, maximum_x);
  attachment.free_anchor->y = std::clamp(attachment.free_anchor->y, 0, maximum_y);
}

}  // namespace

const char* PropPreviewStageLabel(PropPreviewStage stage) {
  switch (stage) {
    case PropPreviewStage::kSource:
      return "Accepted source";
    case PropPreviewStage::kIsolation:
      return "Isolate subject";
    case PropPreviewStage::kComposition:
      return "Compose / anchor";
    case PropPreviewStage::kRasterization:
      return "Rasterize";
    case PropPreviewStage::kQuantization:
      return "Quantize";
    case PropPreviewStage::kEdgeTreatment:
      return "Edge treatment";
    case PropPreviewStage::kCleanup:
      return "Cleanup / validate";
    case PropPreviewStage::kContext:
      return "Finished in context";
  }
  return "Unknown";
}

void PropArtworkEditorModel::ClearPrepared() {
  prepared_.emplace<std::monostate>();
  context_preview_.reset();
}

void PropArtworkEditorModel::MarkInputsChanged() {
  ++revision_;
  ClearPrepared();
}

absl::Status PropArtworkEditorModel::SelectSource(SourceArtwork source, RgbaImage pixels) {
  if (active_recipe_.has_value()) {
    return absl::FailedPreconditionError(
        "an existing prop keeps its retained source; use Save As before choosing another");
  }
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (!pixels.IsValid() || pixels.width != source.width || pixels.height != source.height) {
    return absl::FailedPreconditionError("selected source pixels do not match its definition");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  if (digest != source.content_digest) {
    return absl::FailedPreconditionError("selected source pixels do not match its digest");
  }
  source_ = std::move(source);
  source_pixels_ = std::move(pixels);
  source_to_open_ = source_->id;
  MarkInputsChanged();
  return absl::OkStatus();
}

absl::Status PropArtworkEditorModel::AttachTerrain(const TerrainRecipe& terrain_recipe) {
  ASSIGN_OR_RETURN(const ResolvedTerrainPalette palette,
                   ResolveTerrainPalette(terrain_recipe.config));
  const int prior_pixel_block = settings_.style.pixel_block_size;
  const int pixel_block =
      prior_pixel_block > 0 && terrain_recipe.config.tile_size % prior_pixel_block == 0
          ? prior_pixel_block
          : 1;
  settings_.style = PropArtworkStyle{
      .tile_size = terrain_recipe.config.tile_size,
      .pixel_block_size = pixel_block,
      .palette = palette,
  };
  settings_.terrain_recipe_id = terrain_recipe.id;
  ClampFreeAnchor(settings_);
  terrain_recipe_ = terrain_recipe;
  has_style_ = true;
  MarkInputsChanged();
  return absl::OkStatus();
}

void PropArtworkEditorModel::DetachTerrain() {
  if (!terrain_recipe_.has_value() && !settings_.terrain_recipe_id.has_value()) return;
  terrain_recipe_.reset();
  settings_.terrain_recipe_id.reset();
  MarkInputsChanged();
}

absl::Status PropArtworkEditorModel::LoadRecipe(const PropRecipe& recipe, SourceArtwork source,
                                                RgbaImage pixels,
                                                std::optional<TerrainRecipe> terrain_recipe) {
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (recipe.source_artwork_id != source.id) {
    return absl::FailedPreconditionError("prop recipe and retained source do not match");
  }
  if (recipe.terrain_recipe_id.has_value()) {
    if (!terrain_recipe.has_value() || terrain_recipe->id != *recipe.terrain_recipe_id) {
      return absl::FailedPreconditionError("prop recipe's attached terrain is unavailable");
    }
  } else if (terrain_recipe.has_value()) {
    return absl::InvalidArgumentError("detached prop recipe was given an attached terrain");
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
  settings_ = PropRegenerationSettings{
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

void PropArtworkEditorModel::StartNewRecipe() { *this = PropArtworkEditorModel(); }

void PropArtworkEditorModel::StartRecipeCopy() {
  if (!active_recipe_.has_value()) return;
  active_recipe_.reset();
  recipe_to_open_.clear();
  name_ += " copy";
  MarkInputsChanged();
  status_ = "This copy will create new texture, sprite, blueprint, and recipe IDs.";
}

bool PropArtworkEditorModel::CanPrepare() const {
  return !name_.empty() && source_.has_value() && source_pixels_.has_value() && has_style_;
}

bool PropArtworkEditorModel::HasPreparedResult() const {
  return !std::holds_alternative<std::monostate>(prepared_);
}

const PreparedPropAsset* PropArtworkEditorModel::prepared_creation() const {
  return std::get_if<PreparedPropAsset>(&prepared_);
}

const PreparedPropRegeneration* PropArtworkEditorModel::prepared_regeneration() const {
  return std::get_if<PreparedPropRegeneration>(&prepared_);
}

absl::Status PropArtworkEditorModel::AcceptPrepared(
    uint64_t revision, PreparedPropAsset prepared,
    std::optional<PropArtworkContextPreview> context_preview) {
  if (revision != revision_) {
    return absl::FailedPreconditionError("discarded prop preview for superseded settings");
  }
  if (active_recipe_.has_value()) {
    return absl::FailedPreconditionError("creation result cannot replace an open prop recipe");
  }
  RETURN_IF_ERROR(ValidatePreparedPropAsset(prepared));
  if (!source_.has_value() || prepared.source.id != source_->id) {
    return absl::FailedPreconditionError("prepared prop used a different retained source");
  }
  if (preview_policy_ == PropPreviewPolicy::kFinishedOnly) {
    DiscardIntermediateImages(prepared.artwork);
  }
  prepared_.emplace<PreparedPropAsset>(std::move(prepared));
  context_preview_ = std::move(context_preview);
  preview_stage_ = preview_policy_ == PropPreviewPolicy::kReviewEachStage
                       ? PropPreviewStage::kIsolation
                       : (context_preview_.has_value() ? PropPreviewStage::kContext
                                                       : PropPreviewStage::kCleanup);
  return absl::OkStatus();
}

absl::Status PropArtworkEditorModel::AcceptPrepared(
    uint64_t revision, PreparedPropRegeneration prepared,
    std::optional<PropArtworkContextPreview> context_preview) {
  if (revision != revision_) {
    return absl::FailedPreconditionError("discarded prop preview for superseded settings");
  }
  if (!active_recipe_.has_value() || active_recipe_->id != prepared.recipe_snapshot.id) {
    return absl::FailedPreconditionError("regeneration result no longer matches the open prop");
  }
  RETURN_IF_ERROR(ValidatePreparedPropRegeneration(prepared));
  if (preview_policy_ == PropPreviewPolicy::kFinishedOnly) {
    DiscardIntermediateImages(prepared.artwork);
  }
  prepared_.emplace<PreparedPropRegeneration>(std::move(prepared));
  context_preview_ = std::move(context_preview);
  preview_stage_ = preview_policy_ == PropPreviewPolicy::kReviewEachStage
                       ? PropPreviewStage::kIsolation
                       : (context_preview_.has_value() ? PropPreviewStage::kContext
                                                       : PropPreviewStage::kCleanup);
  return absl::OkStatus();
}

void PropArtworkEditorModel::BindCommittedRecipe(const PropRecipe& recipe) {
  active_recipe_ = recipe;
  recipe_to_open_ = recipe.id;
  name_ = recipe.name;
}

void PropArtworkEditorModel::SetPreviewPolicy(PropPreviewPolicy policy) {
  if (preview_policy_ == policy) return;
  preview_policy_ = policy;
  if (policy == PropPreviewPolicy::kFinishedOnly && HasPreparedResult()) {
    preview_stage_ =
        context_preview_.has_value() ? PropPreviewStage::kContext : PropPreviewStage::kCleanup;
    return;
  }
  const PropArtworkPipelineResult* artwork = Artwork();
  if (policy == PropPreviewPolicy::kReviewEachStage && artwork != nullptr &&
      !artwork->isolated.IsValid()) {
    ClearPrepared();
    preview_stage_ = PropPreviewStage::kSource;
    status_ = "Reprocess to retain each intermediate preview.";
  }
}

void PropArtworkEditorModel::PreviousPreviewStage() {
  if (preview_policy_ != PropPreviewPolicy::kReviewEachStage) return;
  const size_t index = StageIndex(preview_stage_);
  if (index > 0) preview_stage_ = kPreviewStages[index - 1];
}

void PropArtworkEditorModel::NextPreviewStage() {
  if (preview_policy_ != PropPreviewPolicy::kReviewEachStage) return;
  const size_t index = StageIndex(preview_stage_);
  if (index + 1 >= kPreviewStages.size()) return;
  const PropPreviewStage next = kPreviewStages[index + 1];
  if (next == PropPreviewStage::kContext && !context_preview_.has_value()) return;
  preview_stage_ = next;
}

const PropArtworkPipelineResult* PropArtworkEditorModel::Artwork() const {
  if (const PreparedPropAsset* creation = prepared_creation(); creation != nullptr) {
    return &creation->artwork;
  }
  if (const PreparedPropRegeneration* regeneration = prepared_regeneration();
      regeneration != nullptr) {
    return &regeneration->artwork;
  }
  return nullptr;
}

void PropArtworkEditorModel::SetRequestedCandidates(int candidates, int maximum) {
  if (maximum < 1) return;
  requested_candidates_ = std::clamp(candidates, 1, maximum);
}

absl::Status PropArtworkEditorModel::AcceptGeneration(PropGenerationReview review) {
  if (review.candidates.empty()) {
    return absl::InvalidArgumentError("a generation review needs at least one candidate");
  }
  if (review.selected >= review.candidates.size()) {
    return absl::InvalidArgumentError("selected candidate is outside the generated set");
  }
  for (const ImageGenerationCandidate& candidate : review.candidates) {
    if (!candidate.image.IsValid()) {
      return absl::InvalidArgumentError("a generated candidate has no usable image");
    }
  }
  if (review.provider.empty() || review.model.empty() || review.generated_at_utc.empty()) {
    return absl::InvalidArgumentError("a generation review needs complete provenance");
  }
  generation_ = std::move(review);
  return absl::OkStatus();
}

void PropArtworkEditorModel::ClearGeneration() { generation_.reset(); }

void PropArtworkEditorModel::SelectCandidate(size_t index) {
  if (!generation_.has_value()) return;
  if (index >= generation_->candidates.size()) return;
  generation_->selected = index;
}

const ImageGenerationCandidate* PropArtworkEditorModel::SelectedCandidate() const {
  if (!generation_.has_value()) return nullptr;
  return &generation_->candidates[generation_->selected];
}

const RgbaImage* PropArtworkEditorModel::PreviewImage() const {
  // A candidate under review is the decision in front of the user, so it
  // precedes every pipeline stage until it is accepted or discarded. Nothing
  // downstream describes it: it is not the source yet, and the anchors and
  // diagnostics below belong to a prepared result it had no part in.
  if (const ImageGenerationCandidate* candidate = SelectedCandidate(); candidate != nullptr) {
    return &candidate->image;
  }
  if (preview_policy_ == PropPreviewPolicy::kFinishedOnly) {
    if (context_preview_.has_value()) return &context_preview_->image;
    const PropArtworkPipelineResult* artwork = Artwork();
    if (artwork != nullptr) return &artwork->finished.image;
    return source_pixels_.has_value() ? &*source_pixels_ : nullptr;
  }
  if (preview_stage_ == PropPreviewStage::kSource) {
    return source_pixels_.has_value() ? &*source_pixels_ : nullptr;
  }
  if (preview_stage_ == PropPreviewStage::kContext) {
    return context_preview_.has_value() ? &context_preview_->image : nullptr;
  }
  const PropArtworkPipelineResult* artwork = Artwork();
  return artwork == nullptr ? nullptr : StageImage(*artwork, preview_stage_);
}

std::optional<PropPreviewAnchor> PropArtworkEditorModel::PreviewAnchor() const {
  if (generation_.has_value()) return std::nullopt;
  if (preview_stage_ == PropPreviewStage::kContext && context_preview_.has_value()) {
    return PropPreviewAnchor{.x = context_preview_->anchor_x, .y = context_preview_->anchor_y};
  }
  const PropArtworkPipelineResult* artwork = Artwork();
  if (artwork == nullptr) return std::nullopt;
  const PropArtwork* stage = StageArtwork(*artwork, preview_stage_);
  if (stage == nullptr) return std::nullopt;
  return PropPreviewAnchor{.x = stage->anchor_x, .y = stage->anchor_y};
}

const PropStageDiagnostic* PropArtworkEditorModel::PreviewDiagnostic() const {
  if (generation_.has_value()) return nullptr;
  const PropArtworkPipelineResult* artwork = Artwork();
  if (artwork == nullptr) return nullptr;
  const size_t stage = StageIndex(preview_stage_);
  if (stage == 0 || stage > artwork->diagnostics.size()) return nullptr;
  return &artwork->diagnostics[stage - 1];
}

}  // namespace zebes
