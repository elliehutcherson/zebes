#include "editor/parallax_artwork_editor/parallax_artwork_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "common/utils.h"
#include "editor/image_generation/image_generation_lifecycle_panel.h"
#include "editor/imgui_scoped.h"
#include "editor/source_artwork_retention.h"
#include "editor/texture_preview.h"

namespace zebes {
namespace {

constexpr char kSourceDialogKey[] = "ParallaxArtworkSourceDlg";
constexpr float kControlWidth = 180.0f;
constexpr float kPreviewFrameFill = 0.9f;
constexpr float kPreviewStatusHeight = 88.0f;
constexpr ImGuiTreeNodeFlags kSectionFlags = ImGuiTreeNodeFlags_DefaultOpen;

std::string OriginalFilename(const std::string& path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? path : path.substr(separator + 1);
}

const char* FramePolicyLabel(ParallaxArtworkFramePolicy policy) {
  switch (policy) {
    case ParallaxArtworkFramePolicy::kCropToFill:
      return "Crop to fill";
    case ParallaxArtworkFramePolicy::kFitInside:
      return "Fit inside";
  }
  return "Invalid";
}

const char* AlphaRoleLabel(ParallaxArtworkAlphaRole role) {
  switch (role) {
    case ParallaxArtworkAlphaRole::kOpaquePlate:
      return "Opaque plate";
    case ParallaxArtworkAlphaRole::kTransparentOverlay:
      return "Transparent overlay";
  }
  return "Invalid";
}

const char* ExtractionLabel(ParallaxArtworkOverlayExtraction extraction) {
  switch (extraction) {
    case ParallaxArtworkOverlayExtraction::kPreserveAlpha:
      return "Preserve source alpha";
    case ParallaxArtworkOverlayExtraction::kRemoveSolidMatte:
      return "Remove solid matte";
  }
  return "Invalid";
}

const char* OverlayAlphaLabel(ParallaxArtworkOverlayAlphaPolicy policy) {
  switch (policy) {
    case ParallaxArtworkOverlayAlphaPolicy::kPreserve:
      return "Preserve alpha";
    case ParallaxArtworkOverlayAlphaPolicy::kBinary:
      return "Binary alpha";
  }
  return "Invalid";
}

uint8_t ColorChannel(float channel) {
  return static_cast<uint8_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

ParallaxArtworkEditor::ParallaxArtworkEditor(const ParallaxArtworkEditorOptions& options)
    : api_(options.api),
      gui_(options.gui),
      preview_(options.preview),
      preview_canvas_(Canvas::Options{.gui = options.gui, .grid_size = 32.0f}) {}

ParallaxArtworkEditor::~ParallaxArtworkEditor() {
  const absl::Status discarded = DiscardSessionSource();
  if (!discarded.ok()) {
    LOG(ERROR) << "Could not discard uncommitted parallax source during shutdown: " << discarded;
  }
}

absl::StatusOr<std::unique_ptr<ParallaxArtworkEditor>> ParallaxArtworkEditor::Create(
    const ParallaxArtworkEditorOptions& options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("ParallaxArtworkEditor requires an Api");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("ParallaxArtworkEditor requires a GUI");
  }
  if (options.preview == nullptr) {
    return absl::InvalidArgumentError("ParallaxArtworkEditor requires a preview sink");
  }
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationRequestController> generation,
                   ImageGenerationRequestController::Create(options.generation_providers));
  auto editor = absl::WrapUnique(new ParallaxArtworkEditor(options));
  editor->generation_ = std::move(generation);
  return editor;
}

bool ParallaxArtworkEditor::HasPendingWork() const {
  return !std::holds_alternative<std::monostate>(pending_work_);
}

absl::Status ParallaxArtworkEditor::DiscardSessionSource() {
  if (!session_source_id_.has_value()) return absl::OkStatus();
  RETURN_IF_ERROR(api_->DeleteSourceArtwork(*session_source_id_));
  session_source_id_.reset();
  return absl::OkStatus();
}

void ParallaxArtworkEditor::SelectGenerationProvider(size_t index) {
  const absl::Status selected = generation_->SelectProvider(index);
  if (!selected.ok()) {
    model_.SetError(std::string(selected.message()));
    return;
  }
  model_.SetInfo(
      absl::StrCat("Using ", generation_->providers()[index].name, " for image generation."));
}

void ParallaxArtworkEditor::StartGeneration() {
  if (generation_->in_flight()) {
    model_.SetInfo("A generation is already running.");
    return;
  }
  if (model_.active_recipe().has_value()) {
    model_.SetInfo("Use Save As before generating a different retained source.");
    return;
  }
  if (model_.prompt().empty()) {
    model_.SetInfo("Describe the background before generating a source for it.");
    return;
  }
  if (generation_->providers().empty() ||
      generation_->selected_provider() >= generation_->providers().size()) {
    model_.SetError("No image generation provider is available.");
    return;
  }
  const ImageGenerationProvider& provider =
      generation_->providers()[generation_->selected_provider()];
  if (!provider.available()) {
    model_.SetError(provider.unavailable_reason);
    return;
  }

  const ParallaxArtworkPipelineConfig& pipeline = model_.settings().pipeline;
  const ImageGenerationCapabilities capabilities = generation_->capabilities();
  std::string role;
  if (pipeline.alpha_role == ParallaxArtworkAlphaRole::kOpaquePlate) {
    role = kOpaquePlateGenerationGuidance;
  } else if (capabilities.supports_transparency) {
    role = kTransparentOverlayGenerationGuidance;
  } else {
    role = kMatteOverlayGenerationGuidance;
  }
  const std::string repetition = pipeline.review_repeat_x
                                     ? kSeamlessHorizontalGenerationGuidance
                                     : kNonRepeatingHorizontalGenerationGuidance;
  const std::string palette =
      model_.terrain_recipe().has_value()
          ? absl::StrCat("Palette and style: follow the attached terrain style '",
                         model_.terrain_recipe()->name, "' with broad color groups.")
          : kDefaultBackgroundPaletteGuidance;
  std::string instructions = AppendArtworkGenerationStyle(
      absl::StrCat(model_.generation_instructions(), "\n\n", role,
                   "\nTarget aspect: ", pipeline.target_width, ":", pipeline.target_height, ".\n",
                   palette, "\n", repetition),
      model_.style_guidance());
  ImageGenerationSpec spec{
      .prompt = model_.prompt(),
      .instructions = instructions,
      .requested_candidates = model_.requested_candidates(),
      .target_aspect = {.width = pipeline.target_width, .height = pipeline.target_height},
      .transparency = pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
                              capabilities.supports_transparency
                          ? ImageTransparencyPreference::kPreferTransparent
                          : ImageTransparencyPreference::kNoPreference,
  };
  const absl::Status submitted = generation_->Submit(std::move(spec));
  if (!submitted.ok()) {
    model_.SetError(std::string(submitted.message()));
    return;
  }
  model_.SetInfo("Generating background artwork. This can take a minute.");
}

void ParallaxArtworkEditor::CancelGeneration() {
  const absl::Status cancelled = generation_->Cancel();
  if (!cancelled.ok()) {
    LOG(ERROR) << "Could not cancel parallax generation: " << cancelled;
  }
}

void ParallaxArtworkEditor::PollGeneration() {
  absl::StatusOr<bool> polled = generation_->Poll();
  if (!polled.ok()) {
    model_.SetError(std::string(polled.status().message()));
    return;
  }
  if (!*polled) return;
  model_.SetReady("Review the generated candidates, then accept one as retained source artwork.");
}

absl::Status ParallaxArtworkEditor::RetainCandidateAsSource(
    const ImageGenerationReview& review, const ImageGenerationCandidate& candidate) {
  if (model_.active_recipe().has_value()) {
    return absl::FailedPreconditionError(
        "existing parallax artwork keeps its retained source; use Save As before accepting "
        "another");
  }
  const std::string source_name =
      model_.name().empty() ? review.submitted_prompt : absl::StrCat(model_.name(), " source");
  ASSIGN_OR_RETURN(
      const std::string id,
      RetainGeneratedSourceArtwork(*api_, source_name, review, candidate,
                                   [this](const SourceArtwork& source, const RgbaImage& pixels) {
                                     RETURN_IF_ERROR(DiscardSessionSource());
                                     return model_.SelectSource(source, pixels);
                                   }));
  session_source_id_ = id;
  return absl::OkStatus();
}

void ParallaxArtworkEditor::AcceptCandidate() {
  if (!generation_->review().has_value()) {
    model_.SetInfo("Generate a candidate before accepting one.");
    return;
  }
  const std::string prompt = generation_->review()->submitted_prompt;
  const absl::Status retained = generation_->AcceptCandidate(
      [this](const ImageGenerationReview& review, const ImageGenerationCandidate& candidate) {
        return RetainCandidateAsSource(review, candidate);
      });
  if (!retained.ok()) {
    model_.SetError(absl::StrCat("Could not retain generated source: ", retained.message()));
    return;
  }
  model_.SetSuccess(absl::StrCat("Accepted a generated source for '", prompt,
                                 "'. Choose a terrain style and process it."));
}

void ParallaxArtworkEditor::StartImport(std::string path) {
  if (HasPendingWork()) {
    model_.SetInfo("Parallax artwork work is already running.");
    return;
  }
  if (model_.active_recipe().has_value()) {
    model_.SetInfo("Use Save As before importing a different retained source.");
    return;
  }
  const std::string original_filename = OriginalFilename(path);
  if (original_filename.empty()) {
    model_.SetError("Import failed: choose a PNG file with a filename.");
    return;
  }
  absl::StatusOr<BackgroundTask<RgbaImage>> work =
      BackgroundTask<RgbaImage>::Start([path = std::move(path)] { return ReadPng(path); });
  if (!work.ok()) {
    model_.SetError(absl::StrCat("Import failed: ", work.status().message()));
    return;
  }
  const std::string source_name =
      model_.name().empty() ? original_filename : absl::StrCat(model_.name(), " source");
  pending_work_.emplace<PendingImport>(PendingImport{
      .source_name = source_name,
      .original_filename = original_filename,
      .imported_at_utc = CurrentUtcTimestamp(),
      .work = *std::move(work),
  });
  model_.SetInfo(absl::StrCat("Reading '", original_filename, "' in the background..."));
}

void ParallaxArtworkEditor::SelectSource() {
  absl::StatusOr<SourceArtwork*> source = api_->GetSourceArtwork(model_.source_to_open());
  if (!source.ok()) {
    model_.SetError(absl::StrCat("Could not select source: ", source.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> pixels = api_->ReadSourceArtworkPixels((*source)->id);
  if (!pixels.ok()) {
    model_.SetError(absl::StrCat("Could not read source pixels: ", pixels.status().message()));
    return;
  }
  const SourceArtwork snapshot = **source;
  if (session_source_id_.has_value() && *session_source_id_ != snapshot.id) {
    const absl::Status discarded = DiscardSessionSource();
    if (!discarded.ok()) {
      model_.SetError(
          absl::StrCat("Could not discard the current imported source: ", discarded.message()));
      return;
    }
  }
  const absl::Status selected = model_.SelectSource(snapshot, *std::move(pixels));
  if (!selected.ok()) {
    model_.SetError(absl::StrCat("Could not select source: ", selected.message()));
    return;
  }
  generation_->DiscardCandidates();
  model_.SetSuccess(absl::StrCat("Selected retained source '", snapshot.name, "'."));
}

void ParallaxArtworkEditor::DeleteSelectedSource() {
  if (!model_.source().has_value() || model_.active_recipe().has_value()) {
    model_.SetInfo("Choose an uncommitted retained source before deleting it.");
    return;
  }
  const SourceArtwork source = *model_.source();
  const absl::Status deleted = api_->DeleteSourceArtwork(source.id);
  if (!deleted.ok()) {
    model_.SetError(absl::StrCat("Could not delete source: ", deleted.message()));
    return;
  }
  if (session_source_id_ == source.id) session_source_id_.reset();
  generation_->DiscardCandidates();
  model_.StartNewRecipe();
  model_.SetSuccess(absl::StrCat("Deleted retained source '", source.name, "'."));
}

void ParallaxArtworkEditor::OpenRecipe() {
  absl::StatusOr<ParallaxArtworkRecipe*> recipe =
      api_->GetParallaxArtworkRecipe(model_.recipe_to_open());
  if (!recipe.ok()) {
    model_.SetError(absl::StrCat("Could not open artwork: ", recipe.status().message()));
    return;
  }
  const ParallaxArtworkRecipe recipe_snapshot = **recipe;
  absl::StatusOr<SourceArtwork*> source = api_->GetSourceArtwork(recipe_snapshot.source_artwork_id);
  if (!source.ok()) {
    model_.SetError(absl::StrCat("Could not open retained source: ", source.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> pixels =
      api_->ReadSourceArtworkPixels(recipe_snapshot.source_artwork_id);
  if (!pixels.ok()) {
    model_.SetError(absl::StrCat("Could not read retained source: ", pixels.status().message()));
    return;
  }
  std::optional<TerrainRecipe> terrain;
  if (recipe_snapshot.terrain_recipe_id.has_value()) {
    absl::StatusOr<TerrainRecipe*> loaded =
        api_->GetTerrainRecipe(*recipe_snapshot.terrain_recipe_id);
    if (!loaded.ok()) {
      model_.SetError(absl::StrCat("Could not open terrain style: ", loaded.status().message()));
      return;
    }
    terrain = **loaded;
  }
  const SourceArtwork source_snapshot = **source;
  if (session_source_id_.has_value() && *session_source_id_ != source_snapshot.id) {
    const absl::Status discarded = DiscardSessionSource();
    if (!discarded.ok()) {
      model_.SetError(
          absl::StrCat("Could not discard the current imported source: ", discarded.message()));
      return;
    }
  }
  const absl::Status loaded =
      model_.LoadRecipe(recipe_snapshot, source_snapshot, *std::move(pixels), std::move(terrain));
  if (!loaded.ok()) {
    model_.SetError(absl::StrCat("Could not open artwork: ", loaded.message()));
    return;
  }
  generation_->DiscardCandidates();
  model_.SetInfo(
      absl::StrCat("Opened '", recipe_snapshot.name, "'. Process to rebuild its preview."));
}

void ParallaxArtworkEditor::ClearWorkspace() {
  const absl::Status discarded = DiscardSessionSource();
  if (!discarded.ok()) {
    model_.SetError(
        absl::StrCat("Could not discard the uncommitted imported source: ", discarded.message()));
    return;
  }
  generation_->DiscardCandidates();
  model_.StartNewRecipe();
  model_.SetSuccess("Parallax Artwork workspace cleared. Saved artwork bundles were not changed.");
}

void ParallaxArtworkEditor::StartPreparation() {
  if (HasPendingWork()) {
    model_.SetInfo("Parallax artwork work is already running.");
    return;
  }
  if (!model_.CanPrepare()) {
    model_.SetInfo("Choose a retained source, terrain style, and name before processing.");
    return;
  }

  const SourceArtwork source = *model_.source();
  const RgbaImage source_pixels = *model_.source_pixels();
  const ParallaxArtworkRegenerationSettings settings = model_.settings();
  const uint64_t revision = model_.revision();
  if (!model_.active_recipe().has_value()) {
    const PrepareParallaxArtworkAssetRequest request{
        .name = model_.name(),
        .terrain_recipe_id = settings.terrain_recipe_id,
        .style = settings.style,
        .pipeline = settings.pipeline,
        .ids = {.texture_id = GenerateGuid(), .recipe_id = GenerateGuid()},
    };
    absl::StatusOr<BackgroundTask<PreparedParallaxArtworkAsset>> work =
        BackgroundTask<PreparedParallaxArtworkAsset>::Start(
            [source, source_pixels, request]() mutable {
              return PrepareParallaxArtworkAsset(source, source_pixels, request);
            });
    if (!work.ok()) {
      model_.SetError(absl::StrCat("Processing failed to start: ", work.status().message()));
      return;
    }
    pending_work_.emplace<PendingCreation>(
        PendingCreation{.revision = revision, .work = *std::move(work)});
    model_.SetInfo(absl::StrCat("Processing '", model_.name(), "' in the background..."));
    return;
  }

  const ParallaxArtworkRecipe recipe = *model_.active_recipe();
  absl::StatusOr<Texture*> texture = api_->GetTexture(recipe.texture_id);
  if (!texture.ok()) {
    model_.SetError(absl::StrCat("Could not load generated texture: ", texture.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> texture_pixels = api_->ReadTexturePixels(recipe.texture_id);
  if (!texture_pixels.ok()) {
    model_.SetError(
        absl::StrCat("Could not read generated texture: ", texture_pixels.status().message()));
    return;
  }
  absl::StatusOr<BackgroundTask<PreparedParallaxArtworkRegeneration>> work =
      BackgroundTask<PreparedParallaxArtworkRegeneration>::Start(
          [source, source_pixels, recipe, texture = **texture,
           texture_pixels = *std::move(texture_pixels), settings]() mutable {
            return PrepareParallaxArtworkRegeneration(source, source_pixels, recipe, texture,
                                                      texture_pixels, settings);
          });
  if (!work.ok()) {
    model_.SetError(absl::StrCat("Reprocessing failed to start: ", work.status().message()));
    return;
  }
  pending_work_.emplace<PendingRegeneration>(
      PendingRegeneration{.revision = revision, .work = *std::move(work)});
  model_.SetInfo(absl::StrCat("Reprocessing '", model_.name(), "' in the background..."));
}

void ParallaxArtworkEditor::CommitPrepared() {
  if (!model_.HasUncommittedPreparedResult()) {
    model_.SetInfo("Process the artwork before saving it.");
    return;
  }
  if (const PreparedParallaxArtworkAsset* prepared = model_.prepared_creation();
      prepared != nullptr) {
    absl::StatusOr<std::string> created = api_->CreateGeneratedParallaxArtwork(*prepared);
    if (!created.ok()) {
      model_.SetError(absl::StrCat("Create artwork failed: ", created.status().message()));
      return;
    }
    if (session_source_id_ == prepared->source.id) session_source_id_.reset();
    model_.BindCommittedRecipe(prepared->recipe);
    model_.SetSuccess(absl::StrCat("Created '", prepared->recipe.name,
                                   "'. Its managed texture is ready for a parallax theme."));
    return;
  }
  const PreparedParallaxArtworkRegeneration* prepared = model_.prepared_regeneration();
  if (prepared == nullptr) {
    model_.SetError("The prepared artwork does not match this editor operation.");
    return;
  }
  const absl::Status committed = api_->RegenerateGeneratedParallaxArtwork(*prepared);
  if (!committed.ok()) {
    model_.SetError(absl::StrCat("Apply regeneration failed: ", committed.message()));
    return;
  }
  model_.BindCommittedRecipe(prepared->updated_recipe);
  model_.SetSuccess(absl::StrCat("Regenerated '", prepared->updated_recipe.name,
                                 "' without changing its texture or recipe ID."));
}

void ParallaxArtworkEditor::RenameArtwork() {
  if (!model_.active_recipe().has_value()) {
    model_.SetInfo("Create or open parallax artwork before renaming it.");
    return;
  }
  if (model_.HasUncommittedPreparedResult()) {
    model_.SetInfo("Apply or replace the pending regeneration before renaming the artwork.");
    return;
  }
  const ParallaxArtworkRecipe snapshot = *model_.active_recipe();
  const absl::Status renamed = api_->RenameGeneratedParallaxArtwork(snapshot.id, model_.name());
  if (!renamed.ok()) {
    model_.SetError(absl::StrCat("Rename artwork failed: ", renamed.message()));
    return;
  }
  ParallaxArtworkRecipe updated = snapshot;
  updated.name = model_.name();
  model_.BindCommittedRecipe(updated);
  model_.SetSuccess(
      absl::StrCat("Renamed parallax artwork to '", updated.name, "' without changing its IDs."));
}

void ParallaxArtworkEditor::DeleteArtwork() {
  if (!model_.active_recipe().has_value()) {
    model_.SetInfo("Open parallax artwork before deleting it.");
    return;
  }
  const ParallaxArtworkRecipe recipe = *model_.active_recipe();
  const absl::Status deleted = api_->DeleteGeneratedParallaxArtwork(recipe.id);
  if (!deleted.ok()) {
    model_.SetError(absl::StrCat("Delete artwork failed: ", deleted.message()));
    return;
  }
  generation_->DiscardCandidates();
  model_.StartNewRecipe();
  model_.SetSuccess(absl::StrCat("Deleted '", recipe.name, "' and its generated bundle."));
}

absl::Status ParallaxArtworkEditor::FinishImport(PendingImport completed) {
  if (model_.active_recipe().has_value()) {
    return absl::FailedPreconditionError(
        "existing parallax artwork keeps its retained source; use Save As before importing "
        "another");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, completed.work.TakeResult());
  SourceArtworkProvenance provenance = ImportedArtworkProvenance{
      .original_filename = std::move(completed.original_filename),
      .imported_at_utc = std::move(completed.imported_at_utc),
  };
  ASSIGN_OR_RETURN(
      const std::string id,
      RetainSourceArtwork(*api_, std::move(completed.source_name), std::move(provenance), pixels,
                          [this](const SourceArtwork& source, const RgbaImage& retained_pixels) {
                            RETURN_IF_ERROR(DiscardSessionSource());
                            return model_.SelectSource(source, retained_pixels);
                          }));
  session_source_id_ = id;
  generation_->DiscardCandidates();
  model_.SetSuccess(absl::StrCat("Accepted source '", model_.source()->name,
                                 "'. Choose a terrain style and process it."));
  return absl::OkStatus();
}

void ParallaxArtworkEditor::PollWork() {
  if (PendingImport* pending = std::get_if<PendingImport>(&pending_work_); pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetError(absl::StrCat("Import failed: ", ready.status().message()));
      return;
    }
    if (!*ready) return;

    PendingImport completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    const absl::Status accepted = FinishImport(std::move(completed));
    if (!accepted.ok()) {
      model_.SetError(absl::StrCat("Import failed: ", accepted.message()));
    }
    return;
  }

  if (PendingCreation* pending = std::get_if<PendingCreation>(&pending_work_); pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetError(absl::StrCat("Processing failed: ", ready.status().message()));
      return;
    }
    if (!*ready) return;
    PendingCreation completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    absl::StatusOr<PreparedParallaxArtworkAsset> prepared = completed.work.TakeResult();
    if (!prepared.ok()) {
      model_.SetError(absl::StrCat("Processing failed: ", prepared.status().message()));
      return;
    }
    const absl::Status accepted = model_.AcceptPrepared(completed.revision, *std::move(prepared));
    if (!accepted.ok()) {
      model_.SetError(absl::StrCat("Could not accept processed preview: ", accepted.message()));
      return;
    }
    model_.SetReady(
        "Preview is ready but has not been saved. Review the output and repetition previews, "
        "then click Create artwork.");
    return;
  }

  if (PendingRegeneration* pending = std::get_if<PendingRegeneration>(&pending_work_);
      pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetError(absl::StrCat("Reprocessing failed: ", ready.status().message()));
      return;
    }
    if (!*ready) return;
    PendingRegeneration completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    absl::StatusOr<PreparedParallaxArtworkRegeneration> prepared = completed.work.TakeResult();
    if (!prepared.ok()) {
      model_.SetError(absl::StrCat("Reprocessing failed: ", prepared.status().message()));
      return;
    }
    const absl::Status accepted = model_.AcceptPrepared(completed.revision, *std::move(prepared));
    if (!accepted.ok()) {
      model_.SetError(absl::StrCat("Could not accept regenerated preview: ", accepted.message()));
      return;
    }
    model_.SetReady(
        "Regeneration preview is ready but has not been applied. Review the output and "
        "repetition previews, then click Apply regeneration.");
  }
}

bool ParallaxArtworkEditor::RenderPipelineSettings() {
  if (!gui_->CollapsingHeader("Processing settings##ParallaxArtwork", kSectionFlags)) {
    return false;
  }
  ParallaxArtworkPipelineConfig& pipeline = model_.settings().pipeline;
  ParallaxArtworkStyle& style = model_.settings().style;
  bool changed = false;

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->InputInt("Width##ParallaxArtwork", &pipeline.target_width, 16, 128);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->InputInt("Height##ParallaxArtwork", &pipeline.target_height, 16, 128);

  gui_->SetNextItemWidth(kControlWidth);
  {
    const ParallaxArtworkFramePolicy current = pipeline.frame_policy;
    ScopedCombo combo =
        gui_->CreateScopedCombo("Framing##ParallaxArtwork", FramePolicyLabel(current));
    if (combo.IsActive()) {
      for (const ParallaxArtworkFramePolicy candidate :
           {ParallaxArtworkFramePolicy::kCropToFill, ParallaxArtworkFramePolicy::kFitInside}) {
        if (!gui_->Selectable(FramePolicyLabel(candidate), candidate == current)) continue;
        pipeline.frame_policy = candidate;
        changed |= candidate != current;
      }
    }
  }

  gui_->SetNextItemWidth(kControlWidth);
  {
    const int current = style.pixel_block_size;
    const std::string preview = std::to_string(current);
    ScopedCombo combo = gui_->CreateScopedCombo("Pixel block##ParallaxArtwork", preview.c_str());
    if (combo.IsActive()) {
      static constexpr std::array<int, 5> kPixelBlocks = {1, 2, 4, 8, 16};
      for (const int candidate : kPixelBlocks) {
        if (pipeline.target_width % candidate != 0 || pipeline.target_height % candidate != 0) {
          continue;
        }
        const std::string label = std::to_string(candidate);
        if (!gui_->Selectable(label.c_str(), candidate == current)) continue;
        style.pixel_block_size = candidate;
        changed |= candidate != current;
      }
    }
  }
  changed |=
      gui_->Checkbox("Quantize to terrain palette##ParallaxArtwork", &style.quantize_to_palette);

  gui_->SetNextItemWidth(kControlWidth);
  {
    const ParallaxArtworkAlphaRole current = pipeline.alpha_role;
    ScopedCombo combo =
        gui_->CreateScopedCombo("Alpha role##ParallaxArtwork", AlphaRoleLabel(current));
    if (combo.IsActive()) {
      for (const ParallaxArtworkAlphaRole candidate :
           {ParallaxArtworkAlphaRole::kOpaquePlate,
            ParallaxArtworkAlphaRole::kTransparentOverlay}) {
        if (!gui_->Selectable(AlphaRoleLabel(candidate), candidate == current)) continue;
        pipeline.alpha_role = candidate;
        changed |= candidate != current;
        if (candidate == ParallaxArtworkAlphaRole::kOpaquePlate) {
          pipeline.frame_policy = ParallaxArtworkFramePolicy::kCropToFill;
          pipeline.overlay_extraction = ParallaxArtworkOverlayExtraction::kPreserveAlpha;
          pipeline.overlay_alpha_policy = ParallaxArtworkOverlayAlphaPolicy::kPreserve;
        }
      }
    }
  }

  if (pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay) {
    gui_->SetNextItemWidth(kControlWidth);
    {
      const ParallaxArtworkOverlayExtraction current = pipeline.overlay_extraction;
      ScopedCombo combo =
          gui_->CreateScopedCombo("Extraction##ParallaxArtwork", ExtractionLabel(current));
      if (combo.IsActive()) {
        for (const ParallaxArtworkOverlayExtraction candidate :
             {ParallaxArtworkOverlayExtraction::kPreserveAlpha,
              ParallaxArtworkOverlayExtraction::kRemoveSolidMatte}) {
          if (!gui_->Selectable(ExtractionLabel(candidate), candidate == current)) continue;
          pipeline.overlay_extraction = candidate;
          changed |= candidate != current;
        }
      }
    }
    gui_->SetNextItemWidth(kControlWidth);
    {
      const ParallaxArtworkOverlayAlphaPolicy current = pipeline.overlay_alpha_policy;
      ScopedCombo combo =
          gui_->CreateScopedCombo("Output alpha##ParallaxArtwork", OverlayAlphaLabel(current));
      if (combo.IsActive()) {
        for (const ParallaxArtworkOverlayAlphaPolicy candidate :
             {ParallaxArtworkOverlayAlphaPolicy::kPreserve,
              ParallaxArtworkOverlayAlphaPolicy::kBinary}) {
          if (!gui_->Selectable(OverlayAlphaLabel(candidate), candidate == current)) continue;
          pipeline.overlay_alpha_policy = candidate;
          changed |= candidate != current;
        }
      }
    }
    if (pipeline.overlay_extraction == ParallaxArtworkOverlayExtraction::kRemoveSolidMatte) {
      float matte[3] = {pipeline.matte_color.r / 255.0f, pipeline.matte_color.g / 255.0f,
                        pipeline.matte_color.b / 255.0f};
      if (gui_->ColorEdit3("Matte color##ParallaxArtwork", matte)) {
        pipeline.matte_color = {ColorChannel(matte[0]), ColorChannel(matte[1]),
                                ColorChannel(matte[2]), 255};
        changed = true;
      }
      gui_->SetNextItemWidth(kControlWidth);
      changed |= gui_->InputFloat("Transparent distance##ParallaxArtwork",
                                  &pipeline.matte_transparent_distance, 1.0f, 8.0f, "%.1f");
      gui_->SetNextItemWidth(kControlWidth);
      changed |= gui_->InputFloat("Opaque distance##ParallaxArtwork",
                                  &pipeline.matte_opaque_distance, 1.0f, 8.0f, "%.1f");
    }
    if (pipeline.overlay_alpha_policy == ParallaxArtworkOverlayAlphaPolicy::kBinary) {
      gui_->SetNextItemWidth(kControlWidth);
      changed |= gui_->SliderInt("Alpha threshold##ParallaxArtwork",
                                 &pipeline.binary_alpha_threshold, 1, 255);
    }
  }

  changed |=
      gui_->Checkbox("Review horizontal repetition##ParallaxArtwork", &pipeline.review_repeat_x);
  changed |=
      gui_->Checkbox("Review vertical repetition##ParallaxArtwork", &pipeline.review_repeat_y);
  return changed;
}

void ParallaxArtworkEditor::RenderGeneration() {
  gui_->Separator();
  if (!gui_->CollapsingHeader("Generate source##ParallaxArtwork", kSectionFlags)) return;

  ImageGenerationUiState generation = BuildImageGenerationUiState(*generation_);
  const ImageGenerationLifecycleResult lifecycle =
      RenderImageGenerationLifecycle(*gui_, {.editor_id = "ParallaxArtwork"}, generation);
  switch (lifecycle.action) {
    case ImageGenerationLifecycleAction::kNone:
      break;
    case ImageGenerationLifecycleAction::kSelectProvider:
      SelectGenerationProvider(generation.selected_provider);
      break;
    case ImageGenerationLifecycleAction::kCancel:
      CancelGeneration();
      break;
    case ImageGenerationLifecycleAction::kSelectCandidate:
      generation_->SelectCandidate(generation.selected_candidate);
      break;
    case ImageGenerationLifecycleAction::kAcceptCandidate:
      AcceptCandidate();
      break;
    case ImageGenerationLifecycleAction::kDiscardCandidates:
      generation_->DiscardCandidates();
      model_.SetInfo("Discarded the generated candidates.");
      break;
  }
  if (!lifecycle.show_draft) return;

  const ImageGenerationProviderStatus* selected =
      generation.selected_provider < generation.providers.size()
          ? &generation.providers[generation.selected_provider]
          : nullptr;

  const float text_height = gui_->GetTextLineHeightWithSpacing();
  gui_->Text("Prompt");
  gui_->InputTextMultiline("##ParallaxArtworkPrompt", &model_.prompt(),
                           ImVec2(-1.0f, text_height * 5.0f));
  gui_->Text("Background instructions");
  gui_->InputTextMultiline("##ParallaxArtworkSystemPrompt", &model_.generation_instructions(),
                           ImVec2(-1.0f, text_height * 7.0f));
  gui_->SetNextItemWidth(kControlWidth);
  {
    const ArtworkGenerationStylePreset current = model_.style_preset();
    ScopedCombo combo = gui_->CreateScopedCombo("Style preset##ParallaxArtworkGeneration",
                                                ArtworkGenerationStylePresetLabel(current));
    if (combo.IsActive()) {
      for (const ArtworkGenerationStylePreset preset : kArtworkGenerationStylePresets) {
        if (!gui_->Selectable(ArtworkGenerationStylePresetLabel(preset), preset == current)) {
          continue;
        }
        if (preset != current) model_.SetStylePreset(preset);
      }
    }
  }
  gui_->Text("Style guidance");
  if (gui_->InputTextMultiline("##ParallaxArtworkStyleGuidance", &model_.style_guidance(),
                               ImVec2(-1.0f, text_height * 4.0f))) {
    model_.MarkStyleGuidanceCustom();
  }

  const int maximum = std::max(1, generation_->capabilities().maximum_candidates);
  int requested = model_.requested_candidates();
  if (maximum > 1) {
    gui_->SetNextItemWidth(kControlWidth);
    if (gui_->SliderInt("Candidates##ParallaxArtwork", &requested, 1, maximum)) {
      model_.SetRequestedCandidates(requested, maximum);
    }
  }
  model_.SetRequestedCandidates(model_.requested_candidates(), maximum);

  const bool available = selected != nullptr && selected->available;
  gui_->BeginDisabled(!available || model_.prompt().empty() || model_.active_recipe().has_value());
  if (gui_->Button("Generate##ParallaxArtwork")) StartGeneration();
  gui_->EndDisabled();
  if (!available) {
    const char* reason = selected == nullptr || selected->unavailable_reason.empty()
                             ? "No image generation provider is available."
                             : selected->unavailable_reason.c_str();
    gui_->TextWrapped("Image generation is unavailable: %s", reason);
  }
  if (model_.prompt().empty()) {
    gui_->TextWrapped("Describe the background layer to generate a source for it.");
  }
  if (model_.active_recipe().has_value()) {
    gui_->TextWrapped("An open recipe keeps its retained source. Use Save As first.");
  }
}

absl::Status ParallaxArtworkEditor::RenderInput() {
  gui_->BeginDisabled(HasPendingWork());
  auto enabled = absl::MakeCleanup([this] { gui_->EndDisabled(); });

  gui_->Text("Accepted source");
  const char* source_preview =
      model_.source().has_value() ? model_.source()->name.c_str() : "(choose)";
  gui_->BeginDisabled(model_.active_recipe().has_value());
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Source##ParallaxArtwork", source_preview);
    if (combo.IsActive()) {
      for (const SourceArtwork& source : api_->GetAllSourceArtwork()) {
        const bool selected = model_.source().has_value() && model_.source()->id == source.id;
        if (!gui_->Selectable(source.name.c_str(), selected)) continue;
        model_.source_to_open() = source.id;
        SelectSource();
      }
    }
  }
  if (gui_->Button("Import PNG...##ParallaxArtwork")) {
    gui_->OpenFileDialog(kSourceDialogKey, "Import parallax source", ".png", ".");
  }
  gui_->EndDisabled();

  if (model_.active_recipe().has_value()) {
    gui_->TextWrapped("The retained source is fixed for existing artwork. Use Save As first.");
  } else if (!model_.source().has_value()) {
    gui_->TextWrapped("Import a source or choose one already retained by the project.");
  } else {
    const SourceArtwork& source = *model_.source();
    const std::string question =
        absl::StrCat("Delete retained source '", source.name,
                     "' and its PNG? A saved recipe that references it will block deletion.");
    if (delete_source_prompt_.Render(*gui_, "Delete source", source.id, question,
                                     "ParallaxArtworkSource")) {
      DeleteSelectedSource();
    }
  }

  gui_->Separator();
  gui_->Text("Terrain style");
  const char* terrain_preview = model_.terrain_recipe().has_value()
                                    ? model_.terrain_recipe()->name.c_str()
                                    : (model_.has_style() ? "(detached snapshot)" : "(choose)");
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Terrain##ParallaxArtwork", terrain_preview);
    if (combo.IsActive()) {
      for (const TerrainRecipe& terrain : api_->GetAllTerrainRecipes()) {
        const bool selected =
            model_.terrain_recipe().has_value() && model_.terrain_recipe()->id == terrain.id;
        if (!gui_->Selectable(terrain.name.c_str(), selected)) continue;
        RETURN_IF_ERROR(model_.AttachTerrain(terrain));
      }
    }
  }
  if (model_.terrain_recipe().has_value()) {
    if (gui_->Button("Refresh style##ParallaxArtwork")) {
      ASSIGN_OR_RETURN(TerrainRecipe * terrain,
                       api_->GetTerrainRecipe(model_.terrain_recipe()->id));
      RETURN_IF_ERROR(model_.AttachTerrain(*terrain));
    }
    gui_->SameLine();
    if (gui_->Button("Detach style##ParallaxArtwork")) model_.DetachTerrain();
  }

  gui_->Separator();
  if (RenderPipelineSettings()) model_.MarkInputsChanged();
  RenderGeneration();
  return absl::OkStatus();
}

absl::Status ParallaxArtworkEditor::RenderPreview() {
  const bool reviewing_candidate = generation_->review().has_value();
  if (reviewing_candidate) {
    const ImageGenerationReview& review = *generation_->review();
    gui_->Text("Generated candidate %zu of %zu", review.selected + 1, review.candidates.size());
  } else {
    gui_->Text("%s", ParallaxArtworkPreviewStageLabel(model_.preview_stage()));
  }
  if (!reviewing_candidate && model_.HasPreparedResult()) {
    if (gui_->Button("Previous##ParallaxArtworkPreview")) model_.PreviousPreviewStage();
    gui_->SameLine();
    if (gui_->Button("Next##ParallaxArtworkPreview")) model_.NextPreviewStage();
  }

  const RgbaImage* image =
      reviewing_candidate ? &generation_->SelectedCandidate()->image : model_.PreviewImage();
  if (image == nullptr) {
    gui_->TextWrapped("No preview yet. Accept a source, choose a terrain style, and process it.");
    return absl::OkStatus();
  }
  const size_t generation_candidate = reviewing_candidate ? generation_->review()->selected : 0;
  if (framed_revision_ != model_.revision() || framed_stage_ != model_.preview_stage() ||
      framed_width_ != image->width || framed_height_ != image->height ||
      framed_generation_review_ != reviewing_candidate ||
      framed_generation_candidate_ != generation_candidate) {
    frame_pending_ = true;
    framed_revision_ = model_.revision();
    framed_stage_ = model_.preview_stage();
    framed_width_ = image->width;
    framed_height_ = image->height;
    framed_generation_review_ = reviewing_candidate;
    framed_generation_candidate_ = generation_candidate;
  }

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y = std::max(1.0f, canvas_size.y - kPreviewStatusHeight);
  if (canvas_size.x <= 0.0f || canvas_size.y <= 0.0f) return absl::OkStatus();
  ASSIGN_OR_RETURN(const ImTextureID texture, preview_->Upload(*image));

  preview_canvas_.SetGridSize(
      std::max(1.0f, static_cast<float>(model_.settings().style.pixel_block_size)));
  preview_canvas_.Begin("ParallaxArtworkPreviewCanvas", canvas_size, preview_camera_);
  auto canvas_end = absl::MakeCleanup([this] { preview_canvas_.End(); });
  if (frame_pending_) {
    RETURN_IF_ERROR(FrameImagePreviewCamera(preview_camera_, image->width, image->height,
                                            kPreviewFrameFill, Canvas::NavigationZoomRange()));
    frame_pending_ = false;
  }
  if (ImDrawList* draw_list = preview_canvas_.GetDrawList(); draw_list != nullptr) {
    const ImVec2 image_min = preview_canvas_.WorldToScreen({0, 0});
    const ImVec2 image_max = preview_canvas_.WorldToScreen(
        {static_cast<double>(image->width), static_cast<double>(image->height)});
    draw_list->AddImage(texture, image_min, image_max);
  }
  preview_canvas_.HandleInput();
  const float zoom = preview_canvas_.GetZoom();
  std::move(canvas_end).Invoke();

  gui_->Text("WASD or middle-drag to pan, wheel to zoom  |  Zoom: %.2f", zoom);
  gui_->SameLine();
  if (gui_->Button("Fit##ParallaxArtworkPreview")) frame_pending_ = true;
  if (const ParallaxArtworkStageDiagnostic* diagnostic = model_.PreviewDiagnostic();
      diagnostic != nullptr) {
    gui_->TextDisabled("%dx%d, %zu visible pixels", diagnostic->width, diagnostic->height,
                       diagnostic->visible_pixels);
  } else {
    gui_->TextDisabled("%dx%d", image->width, image->height);
  }
  if (const RepetitionDiagnostics* repetition = model_.repetition_diagnostics();
      repetition != nullptr) {
    gui_->TextDisabled("Left/right edges: %d/%d exact, mean %.2f, max %d",
                       repetition->horizontal.exact_pixel_matches,
                       repetition->horizontal.pixels_compared,
                       repetition->horizontal.mean_absolute_channel_difference,
                       repetition->horizontal.maximum_channel_difference);
    gui_->TextDisabled("Top/bottom edges: %d/%d exact, mean %.2f, max %d",
                       repetition->vertical.exact_pixel_matches,
                       repetition->vertical.pixels_compared,
                       repetition->vertical.mean_absolute_channel_difference,
                       repetition->vertical.maximum_channel_difference);
  }
  return absl::OkStatus();
}

absl::Status ParallaxArtworkEditor::RenderOutput() {
  gui_->BeginDisabled(HasPendingWork() || generation_->in_flight());
  auto enabled = absl::MakeCleanup([this] { gui_->EndDisabled(); });

  gui_->Text("Output");
  const char* recipe_preview = model_.active_recipe().has_value()
                                   ? model_.active_recipe()->name.c_str()
                                   : "New parallax artwork";
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Recipe##ParallaxArtwork", recipe_preview);
    if (combo.IsActive()) {
      for (const ParallaxArtworkRecipe& recipe : api_->GetAllParallaxArtworkRecipes()) {
        const bool selected =
            model_.active_recipe().has_value() && model_.active_recipe()->id == recipe.id;
        if (!gui_->Selectable(recipe.name.c_str(), selected)) continue;
        model_.recipe_to_open() = recipe.id;
        OpenRecipe();
      }
    }
  }
  if (model_.active_recipe().has_value()) {
    if (gui_->Button("Save As##ParallaxArtwork")) model_.StartRecipeCopy();
    const ParallaxArtworkRecipe& recipe = *model_.active_recipe();
    const std::string question =
        absl::StrCat("Delete parallax artwork '", recipe.name,
                     "', its texture, and its unshared source? Theme references will block "
                     "deletion.");
    if (delete_artwork_prompt_.Render(*gui_, "Delete", recipe.id, question,
                                      "ParallaxArtworkOutput")) {
      DeleteArtwork();
    }
  }

  const std::string clear_target =
      model_.active_recipe().has_value()
          ? absl::StrCat("recipe:", model_.active_recipe()->id)
          : absl::StrCat("source:", model_.source().has_value() ? model_.source()->id : "none");
  constexpr char kClearQuestion[] =
      "Clear the Parallax Artwork workspace? Unsaved settings and an uncommitted imported "
      "source will be discarded. Saved artwork bundles are not deleted.";
  if (clear_prompt_.Render(*gui_, "Clear workspace", clear_target, kClearQuestion,
                           "ParallaxArtworkClear")) {
    ClearWorkspace();
  }

  gui_->SetNextItemWidth(kControlWidth);
  gui_->BeginDisabled(model_.active_recipe().has_value() && model_.HasUncommittedPreparedResult());
  const bool name_changed = gui_->InputText("Name##ParallaxArtwork", &model_.name());
  gui_->EndDisabled();
  if (name_changed && !model_.active_recipe().has_value()) model_.MarkInputsChanged();
  const bool rename_pending =
      model_.active_recipe().has_value() && model_.name() != model_.active_recipe()->name;
  if (model_.active_recipe().has_value()) {
    gui_->SameLine();
    gui_->BeginDisabled(!rename_pending || model_.name().empty() ||
                        model_.HasUncommittedPreparedResult());
    if (gui_->Button("Rename##ParallaxArtwork")) RenameArtwork();
    gui_->EndDisabled();
  }

  gui_->Separator();
  gui_->BeginDisabled(!model_.CanPrepare() || rename_pending);
  if (gui_->Button(model_.HasPreparedResult() ? "Reprocess##ParallaxArtwork"
                                              : "Process##ParallaxArtwork")) {
    StartPreparation();
  }
  gui_->EndDisabled();

  gui_->BeginDisabled(!model_.HasUncommittedPreparedResult());
  const char* commit_label = model_.active_recipe().has_value()
                                 ? "Apply regeneration##ParallaxArtwork"
                                 : "Create artwork##ParallaxArtwork";
  if (gui_->Button(commit_label)) CommitPrepared();
  gui_->EndDisabled();

  if (rename_pending) gui_->TextWrapped("Apply or undo the pending name change before processing.");
  if (model_.name().empty()) gui_->TextWrapped("Name the artwork first.");
  if (!model_.source().has_value()) gui_->TextWrapped("Choose or import a retained source.");
  if (!model_.has_style()) gui_->TextWrapped("Choose a terrain style.");
  if (model_.HasUncommittedPreparedResult() &&
      (model_.status().empty() || model_.status_kind() != ParallaxArtworkStatusKind::kReady)) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "READY — NOT SAVED");
    gui_->TextWrapped("The preview is processed. Use the save action above to publish it.");
  } else if (model_.active_recipe().has_value() &&
             (model_.status().empty() ||
              model_.status_kind() != ParallaxArtworkStatusKind::kSuccess)) {
    gui_->TextColored({0.35f, 0.9f, 0.45f, 1.0f}, "SAVED ARTWORK");
  }
  if (model_.active_recipe().has_value()) {
    gui_->TextDisabled("Texture ID: %s", model_.active_recipe()->texture_id.c_str());
  }
  if (!model_.status().empty()) {
    switch (model_.status_kind()) {
      case ParallaxArtworkStatusKind::kInfo:
        gui_->TextColored({0.55f, 0.75f, 1.0f, 1.0f}, "STATUS");
        break;
      case ParallaxArtworkStatusKind::kReady:
        gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "READY — NOT SAVED");
        break;
      case ParallaxArtworkStatusKind::kSuccess:
        gui_->TextColored({0.35f, 0.9f, 0.45f, 1.0f}, "SUCCESS");
        break;
      case ParallaxArtworkStatusKind::kError:
        gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "ERROR");
        break;
    }
    gui_->TextWrapped("%s", model_.status().c_str());
    if (gui_->Button("Dismiss status##ParallaxArtwork")) model_.ClearStatus();
  }
  return absl::OkStatus();
}

absl::Status ParallaxArtworkEditor::Render() {
  PollGeneration();
  PollWork();

  constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
  ScopedTable table = gui_->CreateScopedTable("ParallaxArtworkEditorLayout", 3, kTableFlags);
  if (table) {
    gui_->TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.56f);
    gui_->TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    gui_->TableNextRow();
    gui_->TableNextColumn();
    {
      ScopedChild child = gui_->CreateScopedChild("ParallaxArtworkInput", ImVec2(0, 0), false);
      RETURN_IF_ERROR(RenderInput());
    }
    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderPreview());
    gui_->TableNextColumn();
    {
      ScopedChild child = gui_->CreateScopedChild("ParallaxArtworkOutput", ImVec2(0, 0), false);
      RETURN_IF_ERROR(RenderOutput());
    }
  }

  if (std::optional<std::string> selected = gui_->DisplayFileDialog(kSourceDialogKey);
      selected.has_value()) {
    StartImport(*std::move(selected));
  }
  return absl::OkStatus();
}

}  // namespace zebes
