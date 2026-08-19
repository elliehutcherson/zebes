#include "editor/prop_artwork_editor/prop_artwork_editor.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "editor/imgui_scoped.h"
#include "editor/prop_artwork_editor/prop_artwork_context.h"
#include "editor/texture_preview.h"

namespace zebes {
namespace {

constexpr char kSourceDialogKey[] = "PropArtworkSourceDlg";
constexpr float kPreviewFrameFill = 0.9f;
constexpr float kPreviewStatusHeight = 48.0f;
constexpr float kAnchorArmLength = 6.0f;

std::string OriginalFilename(const std::string& path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? path : path.substr(separator + 1);
}

std::string CurrentUtcTimestamp() {
  return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", absl::Now(), absl::UTCTimeZone());
}

absl::StatusOr<std::optional<PropArtworkContextPreview>> BuildContext(
    const PropArtwork& artwork, const std::optional<TerrainGenConfig>& terrain,
    PropAttachmentMode attachment_mode) {
  if (!terrain.has_value()) return std::nullopt;
  ASSIGN_OR_RETURN(PropArtworkContextPreview preview,
                   BuildPropArtworkContextPreview(artwork, *terrain, attachment_mode));
  return std::optional<PropArtworkContextPreview>(std::move(preview));
}

absl::StatusOr<PreparedPropCreationPreview> PrepareCreationPreview(
    SourceArtwork source, RgbaImage source_pixels, PreparePropAssetRequest request,
    std::optional<TerrainGenConfig> terrain) {
  ASSIGN_OR_RETURN(PreparedPropAsset prepared, PreparePropAsset(source, source_pixels, request));
  ASSIGN_OR_RETURN(std::optional<PropArtworkContextPreview> context,
                   BuildContext(prepared.artwork.finished, terrain,
                                request.pipeline.composition.attachment.mode));
  return PreparedPropCreationPreview{
      .asset = std::move(prepared),
      .context = std::move(context),
  };
}

absl::StatusOr<PreparedPropRegenerationPreview> PrepareRegenerationPreview(
    SourceArtwork source, RgbaImage source_pixels, PropRecipe recipe, Texture texture,
    RgbaImage texture_pixels, Sprite sprite, PropRegenerationSettings settings,
    std::optional<TerrainGenConfig> terrain) {
  ASSIGN_OR_RETURN(PreparedPropRegeneration prepared,
                   PreparePropRegeneration(source, source_pixels, recipe, texture, texture_pixels,
                                           sprite, settings));
  ASSIGN_OR_RETURN(std::optional<PropArtworkContextPreview> context,
                   BuildContext(prepared.artwork.finished, terrain,
                                settings.pipeline.composition.attachment.mode));
  return PreparedPropRegenerationPreview{
      .asset = std::move(prepared),
      .context = std::move(context),
  };
}

}  // namespace

PropArtworkEditor::PropArtworkEditor(const PropArtworkEditorOptions& options)
    : api_(options.api),
      gui_(options.gui),
      preview_(options.preview),
      generation_(options.generation),
      preview_canvas_(Canvas::Options{.gui = options.gui, .grid_size = 32.0f}) {}

PropArtworkEditor::~PropArtworkEditor() {
  // The engine outlives this editor, so an abandoned request would keep polling
  // a result nobody will collect.
  CancelGeneration();
  const absl::Status discarded = DiscardSessionSource();
  if (!discarded.ok()) {
    LOG(ERROR) << "Could not discard uncommitted prop source during shutdown: " << discarded;
  }
}

absl::StatusOr<std::unique_ptr<PropArtworkEditor>> PropArtworkEditor::Create(
    const PropArtworkEditorOptions& options) {
  if (options.api == nullptr)
    return absl::InvalidArgumentError("PropArtworkEditor requires an Api");
  if (options.gui == nullptr) return absl::InvalidArgumentError("PropArtworkEditor requires a GUI");
  if (options.preview == nullptr) {
    return absl::InvalidArgumentError("PropArtworkEditor requires a preview sink");
  }
  if (options.generation == nullptr) {
    return absl::InvalidArgumentError("PropArtworkEditor requires an image generation engine");
  }
  auto editor = absl::WrapUnique(new PropArtworkEditor(options));
  RETURN_IF_ERROR(editor->Init());
  return editor;
}

absl::Status PropArtworkEditor::Init() {
  ASSIGN_OR_RETURN(controls_panel_, PropArtworkControlsPanel::Create(gui_));
  ASSIGN_OR_RETURN(output_panel_, PropArtworkOutputPanel::Create(gui_));
  return absl::OkStatus();
}

bool PropArtworkEditor::HasPendingWork() const {
  return !std::holds_alternative<std::monostate>(pending_work_);
}

absl::Status PropArtworkEditor::DiscardSessionSource() {
  if (!session_source_id_.has_value()) return absl::OkStatus();
  RETURN_IF_ERROR(api_->DeleteSourceArtwork(*session_source_id_));
  session_source_id_.reset();
  return absl::OkStatus();
}

void PropArtworkEditor::StartImport(std::string path) {
  if (HasPendingWork()) {
    model_.SetStatus("Prop artwork work is already running.");
    return;
  }
  const std::string original_filename = OriginalFilename(path);
  if (original_filename.empty()) {
    model_.SetStatus("Choose a PNG file with a filename.");
    return;
  }
  absl::StatusOr<BackgroundTask<RgbaImage>> work =
      BackgroundTask<RgbaImage>::Start([path] { return ReadPng(path); });
  if (!work.ok()) {
    model_.SetStatus(std::string(work.status().message()));
    return;
  }
  const std::string source_name =
      model_.name().empty() ? original_filename : absl::StrCat(model_.name(), " source");
  pending_work_.emplace<PendingImport>(PendingImport{
      .path = std::move(path),
      .source_name = source_name,
      .original_filename = original_filename,
      .imported_at_utc = CurrentUtcTimestamp(),
      .work = *std::move(work),
  });
  model_.SetStatus(absl::StrCat("Reading '", original_filename, "' in the background..."));
}

void PropArtworkEditor::SelectSource() {
  absl::StatusOr<SourceArtwork*> source = api_->GetSourceArtwork(model_.source_to_open());
  if (!source.ok()) {
    model_.SetStatus(std::string(source.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> pixels = api_->ReadSourceArtworkPixels((*source)->id);
  if (!pixels.ok()) {
    model_.SetStatus(std::string(pixels.status().message()));
    return;
  }
  const SourceArtwork source_snapshot = **source;
  if (session_source_id_.has_value() && *session_source_id_ != source_snapshot.id) {
    const absl::Status discarded = DiscardSessionSource();
    if (!discarded.ok()) {
      model_.SetStatus(
          absl::StrCat("Could not discard the current imported source: ", discarded.message()));
      return;
    }
  }
  const absl::Status selected = model_.SelectSource(source_snapshot, *std::move(pixels));
  if (!selected.ok()) {
    model_.SetStatus(std::string(selected.message()));
    return;
  }
  model_.SetStatus(absl::StrCat("Selected retained source '", source_snapshot.name, "'."));
}

void PropArtworkEditor::StartGeneration() {
  if (pending_generation_id_.has_value()) {
    model_.SetStatus("A generation is already running.");
    return;
  }
  if (model_.prompt().empty()) {
    model_.SetStatus("Describe the prop before generating a source for it.");
    return;
  }
  const ImageGenerationCapabilities capabilities = generation_->Capabilities();
  ImageGenerationSpec spec{
      .prompt = model_.prompt(),
      .requested_candidates = model_.requested_candidates(),
      .target_aspect = {.width = 1, .height = 1},
      .transparency = capabilities.supports_transparency
                          ? ImageTransparencyPreference::kPreferTransparent
                          : ImageTransparencyPreference::kNoPreference,
  };
  absl::StatusOr<uint64_t> id = generation_->Submit(std::move(spec));
  if (!id.ok()) {
    model_.SetStatus(std::string(id.status().message()));
    return;
  }
  // The previous review is dropped here rather than on acceptance: a new
  // prompt supersedes it, and keeping it would show a candidate from a prompt
  // the user has already moved on from.
  model_.ClearGeneration();
  pending_generation_id_ = *id;
  model_.SetStatus("Generating. This can take a minute.");
}

void PropArtworkEditor::CancelGeneration() {
  if (!pending_generation_id_.has_value()) return;
  const absl::Status cancelled = generation_->Cancel(*pending_generation_id_);
  if (!cancelled.ok()) {
    LOG(ERROR) << "Could not cancel prop generation: " << cancelled;
  }
  // The id stays pending: the engine still owes exactly one event for it, and
  // dropping it now would leave that event to be mistaken for a later request.
}

void PropArtworkEditor::PollGeneration() {
  while (std::optional<GenerationEvent> event = generation_->NextEvent()) {
    if (pending_generation_id_ != event->id) continue;
    pending_generation_id_.reset();
    if (!event->result.ok()) {
      model_.SetStatus(std::string(event->result.status().message()));
      continue;
    }
    ImageGenerationResult result = *std::move(event->result);
    const absl::Status accepted = model_.AcceptGeneration(PropGenerationReview{
        .provider = std::move(result.provider),
        .model = std::move(result.model),
        .submitted_prompt = std::move(result.submitted_prompt),
        .provider_request_id = std::move(result.provider_request_id),
        .generated_at_utc = CurrentUtcTimestamp(),
        .candidates = std::move(result.candidates),
    });
    if (!accepted.ok()) {
      model_.SetStatus(std::string(accepted.message()));
      continue;
    }
    model_.SetStatus("Review the candidates, then accept one as the source.");
  }
}

// Retains the selected candidate the way an imported PNG is retained: the
// manager owns the pixels before the model points at them, so a failure
// anywhere leaves no half-attached source behind.
absl::Status PropArtworkEditor::RetainCandidateAsSource() {
  const PropGenerationReview& review = *model_.generation_review();
  const ImageGenerationCandidate& candidate = review.candidates[review.selected];
  SourceArtworkProvenance provenance = GeneratedArtworkProvenance{
      .provider = review.provider,
      .model = review.model,
      .submitted_prompt = review.submitted_prompt,
      .revised_prompt = candidate.revised_prompt,
      .provider_request_id = review.provider_request_id,
      .generated_at_utc = review.generated_at_utc,
  };
  const std::string source_name =
      model_.name().empty() ? review.submitted_prompt : absl::StrCat(model_.name(), " source");
  ASSIGN_OR_RETURN(const std::string id,
                   api_->CreateSourceArtwork(source_name, std::move(provenance), candidate.image));
  auto remove_source = absl::MakeCleanup([this, &id] {
    const absl::Status cleanup = api_->DeleteSourceArtwork(id);
    if (!cleanup.ok()) LOG(ERROR) << "Could not remove a rejected generated source: " << cleanup;
  });

  ASSIGN_OR_RETURN(SourceArtwork * source, api_->GetSourceArtwork(id));
  const SourceArtwork snapshot = *source;
  RETURN_IF_ERROR(DiscardSessionSource());
  RETURN_IF_ERROR(model_.SelectSource(snapshot, candidate.image));

  std::move(remove_source).Cancel();
  session_source_id_ = id;
  return absl::OkStatus();
}

void PropArtworkEditor::AcceptCandidate() {
  if (!model_.generation_review().has_value()) {
    model_.SetStatus("Generate a candidate before accepting one.");
    return;
  }
  const absl::Status retained = RetainCandidateAsSource();
  if (!retained.ok()) {
    model_.SetStatus(std::string(retained.message()));
    return;
  }
  const std::string prompt = model_.generation_review()->submitted_prompt;
  model_.ClearGeneration();
  model_.SetStatus(absl::StrCat("Accepted a generated source for '", prompt,
                                "'. Choose a terrain and process it."));
}

void PropArtworkEditor::DeleteSelectedSource() {
  if (!model_.source().has_value() || model_.active_recipe().has_value()) {
    model_.SetStatus("Choose an uncommitted retained source before deleting it.");
    return;
  }
  const SourceArtwork source = *model_.source();
  const absl::Status deleted = api_->DeleteSourceArtwork(source.id);
  if (!deleted.ok()) {
    model_.SetStatus(std::string(deleted.message()));
    return;
  }
  if (session_source_id_ == source.id) session_source_id_.reset();
  model_.StartNewRecipe();
  model_.SetStatus(absl::StrCat("Deleted retained source '", source.name, "'."));
}

void PropArtworkEditor::OpenRecipe() {
  absl::StatusOr<PropRecipe*> recipe = api_->GetPropRecipe(model_.recipe_to_open());
  if (!recipe.ok()) {
    model_.SetStatus(std::string(recipe.status().message()));
    return;
  }
  const PropRecipe snapshot = **recipe;
  absl::StatusOr<SourceArtwork*> source = api_->GetSourceArtwork(snapshot.source_artwork_id);
  if (!source.ok()) {
    model_.SetStatus(std::string(source.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> pixels = api_->ReadSourceArtworkPixels(snapshot.source_artwork_id);
  if (!pixels.ok()) {
    model_.SetStatus(std::string(pixels.status().message()));
    return;
  }

  std::optional<TerrainRecipe> terrain;
  if (snapshot.terrain_recipe_id.has_value()) {
    absl::StatusOr<TerrainRecipe*> loaded = api_->GetTerrainRecipe(*snapshot.terrain_recipe_id);
    if (!loaded.ok()) {
      model_.SetStatus(std::string(loaded.status().message()));
      return;
    }
    terrain = **loaded;
  }
  const SourceArtwork source_snapshot = **source;
  if (session_source_id_.has_value() && *session_source_id_ != source_snapshot.id) {
    const absl::Status discarded = DiscardSessionSource();
    if (!discarded.ok()) {
      model_.SetStatus(
          absl::StrCat("Could not discard the current imported source: ", discarded.message()));
      return;
    }
  }
  const absl::Status loaded =
      model_.LoadRecipe(snapshot, source_snapshot, *std::move(pixels), std::move(terrain));
  if (!loaded.ok()) {
    model_.SetStatus(std::string(loaded.message()));
    return;
  }
  model_.SetStatus(absl::StrCat("Opened '", snapshot.name, "'. Process to preview it."));
}

void PropArtworkEditor::ClearWorkspace() {
  const absl::Status discarded = DiscardSessionSource();
  if (!discarded.ok()) {
    model_.SetStatus(
        absl::StrCat("Could not discard the uncommitted imported source: ", discarded.message()));
    return;
  }
  model_.StartNewRecipe();
  model_.SetStatus("Prop Artwork workspace cleared. Saved prop bundles were not changed.");
}

void PropArtworkEditor::StartPreparation() {
  if (HasPendingWork()) {
    model_.SetStatus("Prop artwork work is already running.");
    return;
  }
  if (!model_.CanPrepare()) {
    model_.SetStatus("Choose a retained source, terrain style, and name before processing.");
    return;
  }

  const SourceArtwork source = *model_.source();
  const RgbaImage source_pixels = *model_.source_pixels();
  const PropRegenerationSettings settings = model_.settings();
  const std::optional<TerrainGenConfig> terrain =
      model_.terrain_recipe().has_value()
          ? std::optional<TerrainGenConfig>(model_.terrain_recipe()->config)
          : std::nullopt;
  const uint64_t revision = model_.revision();

  if (!model_.active_recipe().has_value()) {
    const PreparePropAssetRequest request{
        .name = model_.name(),
        .terrain_recipe_id = settings.terrain_recipe_id,
        .style = settings.style,
        .pipeline = settings.pipeline,
        .ids =
            PropAssetIds{
                .texture_id = GenerateGuid(),
                .sprite_id = GenerateGuid(),
                .blueprint_id = GenerateGuid(),
                .recipe_id = GenerateGuid(),
            },
    };
    absl::StatusOr<BackgroundTask<PreparedPropCreationPreview>> work =
        BackgroundTask<PreparedPropCreationPreview>::Start(
            [source, source_pixels, request, terrain]() mutable {
              return PrepareCreationPreview(std::move(source), std::move(source_pixels), request,
                                            terrain);
            });
    if (!work.ok()) {
      model_.SetStatus(std::string(work.status().message()));
      return;
    }
    pending_work_.emplace<PendingCreation>(
        PendingCreation{.revision = revision, .work = *std::move(work)});
    model_.SetStatus(absl::StrCat("Processing '", model_.name(), "' in the background..."));
    return;
  }

  const PropRecipe recipe = *model_.active_recipe();
  absl::StatusOr<Texture*> texture = api_->GetTexture(recipe.texture_id);
  if (!texture.ok()) {
    model_.SetStatus(std::string(texture.status().message()));
    return;
  }
  absl::StatusOr<RgbaImage> texture_pixels = api_->ReadTexturePixels(recipe.texture_id);
  if (!texture_pixels.ok()) {
    model_.SetStatus(std::string(texture_pixels.status().message()));
    return;
  }
  absl::StatusOr<Sprite*> sprite = api_->GetSprite(recipe.sprite_id);
  if (!sprite.ok()) {
    model_.SetStatus(std::string(sprite.status().message()));
    return;
  }

  absl::StatusOr<BackgroundTask<PreparedPropRegenerationPreview>> work =
      BackgroundTask<PreparedPropRegenerationPreview>::Start(
          [source, source_pixels, recipe, texture = **texture,
           texture_pixels = *std::move(texture_pixels), sprite = **sprite, settings,
           terrain]() mutable {
            return PrepareRegenerationPreview(
                std::move(source), std::move(source_pixels), std::move(recipe), std::move(texture),
                std::move(texture_pixels), std::move(sprite), settings, terrain);
          });
  if (!work.ok()) {
    model_.SetStatus(std::string(work.status().message()));
    return;
  }
  pending_work_.emplace<PendingRegeneration>(
      PendingRegeneration{.revision = revision, .work = *std::move(work)});
  model_.SetStatus(absl::StrCat("Reprocessing '", model_.name(), "' in the background..."));
}

void PropArtworkEditor::CommitPrepared() {
  if (const PreparedPropAsset* prepared = model_.prepared_creation(); prepared != nullptr) {
    absl::StatusOr<std::string> created = api_->CreateGeneratedProp(*prepared);
    if (!created.ok()) {
      model_.SetStatus(std::string(created.status().message()));
      return;
    }
    if (session_source_id_ == prepared->source.id) session_source_id_.reset();
    model_.BindCommittedRecipe(prepared->recipe);
    model_.SetStatus(absl::StrCat("Created '", prepared->recipe.name,
                                  "'. Its collider-free blueprint is ready for placement."));
    return;
  }
  const PreparedPropRegeneration* prepared = model_.prepared_regeneration();
  if (prepared == nullptr) {
    model_.SetStatus("Process the artwork before committing it.");
    return;
  }
  const absl::Status committed = api_->RegenerateGeneratedProp(*prepared);
  if (!committed.ok()) {
    model_.SetStatus(std::string(committed.message()));
    return;
  }
  model_.BindCommittedRecipe(prepared->updated_recipe);
  model_.SetStatus(absl::StrCat("Regenerated '", prepared->updated_recipe.name,
                                "' without changing asset IDs or its blueprint."));
}

void PropArtworkEditor::DeleteProp() {
  if (!model_.active_recipe().has_value()) {
    model_.SetStatus("Open a prop recipe before deleting it.");
    return;
  }
  const PropRecipe recipe = *model_.active_recipe();
  const absl::Status deleted = api_->DeleteGeneratedProp(recipe.id);
  if (!deleted.ok()) {
    model_.SetStatus(std::string(deleted.message()));
    return;
  }
  model_.StartNewRecipe();
  model_.SetStatus(absl::StrCat("Deleted '", recipe.name, "' and its generated bundle."));
}

void PropArtworkEditor::PollWork() {
  if (PendingImport* pending = std::get_if<PendingImport>(&pending_work_); pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetStatus(std::string(ready.status().message()));
      return;
    }
    if (!*ready) return;

    PendingImport completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    absl::StatusOr<RgbaImage> pixels = completed.work.TakeResult();
    if (!pixels.ok()) {
      model_.SetStatus(std::string(pixels.status().message()));
      return;
    }
    SourceArtworkProvenance provenance = ImportedArtworkProvenance{
        .original_filename = completed.original_filename,
        .imported_at_utc = completed.imported_at_utc,
    };
    absl::StatusOr<std::string> id =
        api_->CreateSourceArtwork(completed.source_name, std::move(provenance), *pixels);
    if (!id.ok()) {
      model_.SetStatus(std::string(id.status().message()));
      return;
    }
    absl::StatusOr<SourceArtwork*> source = api_->GetSourceArtwork(*id);
    if (!source.ok()) {
      const absl::Status cleanup = api_->DeleteSourceArtwork(*id);
      model_.SetStatus(absl::StrCat(
          "Imported source could not be reloaded: ", source.status().message(),
          cleanup.ok() ? "" : absl::StrCat("; cleanup also failed: ", cleanup.message())));
      return;
    }
    const SourceArtwork source_snapshot = **source;
    const absl::Status discarded = DiscardSessionSource();
    if (!discarded.ok()) {
      const absl::Status cleanup = api_->DeleteSourceArtwork(*id);
      model_.SetStatus(absl::StrCat(
          "Could not replace the current imported source: ", discarded.message(),
          cleanup.ok() ? ""
                       : absl::StrCat("; new-source cleanup also failed: ", cleanup.message())));
      return;
    }
    const absl::Status selected = model_.SelectSource(source_snapshot, *std::move(pixels));
    if (!selected.ok()) {
      const absl::Status cleanup = api_->DeleteSourceArtwork(*id);
      model_.StartNewRecipe();
      model_.SetStatus(absl::StrCat(
          selected.message(), cleanup.ok() ? ""
                                           : absl::StrCat("; imported-source cleanup also failed: ",
                                                          cleanup.message())));
      return;
    }
    session_source_id_ = *id;
    model_.SetStatus(absl::StrCat("Accepted source '", source_snapshot.name,
                                  "'. Choose a terrain and process it."));
    return;
  }

  if (PendingCreation* pending = std::get_if<PendingCreation>(&pending_work_); pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetStatus(std::string(ready.status().message()));
      return;
    }
    if (!*ready) return;

    PendingCreation completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    absl::StatusOr<PreparedPropCreationPreview> prepared = completed.work.TakeResult();
    if (!prepared.ok()) {
      model_.SetStatus(std::string(prepared.status().message()));
      return;
    }
    const absl::Status accepted = model_.AcceptPrepared(
        completed.revision, std::move(prepared->asset), std::move(prepared->context));
    if (!accepted.ok()) {
      model_.SetStatus(std::string(accepted.message()));
      return;
    }
    model_.SetStatus("Processing finished. Review the preview, then create the prop.");
    return;
  }

  if (PendingRegeneration* pending = std::get_if<PendingRegeneration>(&pending_work_);
      pending != nullptr) {
    absl::StatusOr<bool> ready = pending->work.IsReady();
    if (!ready.ok()) {
      pending_work_.emplace<std::monostate>();
      model_.SetStatus(std::string(ready.status().message()));
      return;
    }
    if (!*ready) return;

    PendingRegeneration completed = std::move(*pending);
    pending_work_.emplace<std::monostate>();
    absl::StatusOr<PreparedPropRegenerationPreview> prepared = completed.work.TakeResult();
    if (!prepared.ok()) {
      model_.SetStatus(std::string(prepared.status().message()));
      return;
    }
    const absl::Status accepted = model_.AcceptPrepared(
        completed.revision, std::move(prepared->asset), std::move(prepared->context));
    if (!accepted.ok()) {
      model_.SetStatus(std::string(accepted.message()));
      return;
    }
    model_.SetStatus("Reprocessing finished. Review the preview, then apply regeneration.");
  }
}

absl::Status PropArtworkEditor::RenderControls() {
  gui_->BeginDisabled(HasPendingWork());
  auto enabled = absl::MakeCleanup([this] { gui_->EndDisabled(); });
  const PropGenerationStatus generation{
      .capabilities = generation_->Capabilities(),
      .in_flight = pending_generation_id_.has_value(),
  };
  ASSIGN_OR_RETURN(const PropArtworkControlsPanel::Action action,
                   controls_panel_->Render(model_, api_->GetAllSourceArtwork(),
                                           api_->GetAllTerrainRecipes(), generation));
  switch (action) {
    case PropArtworkControlsPanel::Action::kNone:
      break;
    case PropArtworkControlsPanel::Action::kBrowseSource:
      gui_->OpenFileDialog(kSourceDialogKey, "Import prop source", ".png", ".");
      break;
    case PropArtworkControlsPanel::Action::kOpenSource:
      SelectSource();
      break;
    case PropArtworkControlsPanel::Action::kDeleteSource:
      DeleteSelectedSource();
      break;
    case PropArtworkControlsPanel::Action::kGenerate:
      StartGeneration();
      break;
    case PropArtworkControlsPanel::Action::kCancelGeneration:
      CancelGeneration();
      break;
    case PropArtworkControlsPanel::Action::kAcceptCandidate:
      AcceptCandidate();
      break;
    case PropArtworkControlsPanel::Action::kDiscardCandidates:
      model_.ClearGeneration();
      model_.SetStatus("Discarded the generated candidates.");
      break;
  }
  return absl::OkStatus();
}

absl::Status PropArtworkEditor::RenderPreview() {
  const bool reviewing_candidate = model_.generation_review().has_value();
  PropPreviewStage shown_stage =
      model_.HasPreparedResult() ? model_.preview_stage() : PropPreviewStage::kSource;
  if (reviewing_candidate) {
    const PropGenerationReview& review = *model_.generation_review();
    gui_->Text("Generated candidate %zu of %zu", review.selected + 1, review.candidates.size());
  } else {
    gui_->Text("%s", PropPreviewStageLabel(shown_stage));
  }
  // Stage stepping would move the preview off the candidate the buttons in the
  // input column are about, so it waits until the review is resolved.
  if (!reviewing_candidate && model_.preview_policy() == PropPreviewPolicy::kReviewEachStage &&
      model_.HasPreparedResult()) {
    if (gui_->Button("Previous##PropArtworkPreview")) model_.PreviousPreviewStage();
    gui_->SameLine();
    if (gui_->Button("Next##PropArtworkPreview")) model_.NextPreviewStage();
  }
  shown_stage = model_.HasPreparedResult() ? model_.preview_stage() : PropPreviewStage::kSource;

  const RgbaImage* image = model_.PreviewImage();
  if (image == nullptr) {
    gui_->TextWrapped("No preview yet. Accept a source, choose terrain, and process it.");
    return absl::OkStatus();
  }

  const bool prepared = model_.HasPreparedResult();
  if (framed_revision_ != model_.revision() || framed_stage_ != shown_stage ||
      framed_width_ != image->width || framed_height_ != image->height ||
      framed_prepared_ != prepared) {
    frame_pending_ = true;
    framed_revision_ = model_.revision();
    framed_stage_ = shown_stage;
    framed_width_ = image->width;
    framed_height_ = image->height;
    framed_prepared_ = prepared;
  }

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y = std::max(1.0f, canvas_size.y - kPreviewStatusHeight);
  if (canvas_size.x <= 0.0f || canvas_size.y <= 0.0f) return absl::OkStatus();
  ASSIGN_OR_RETURN(const ImTextureID texture, preview_->Upload(*image));

  const float grid_size =
      model_.has_style() ? static_cast<float>(model_.settings().style.tile_size) : 32.0f;
  preview_canvas_.SetGridSize(grid_size);
  preview_canvas_.Begin("PropArtworkPreviewCanvas", canvas_size, preview_camera_);
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
    preview_canvas_.DrawGrid();

    if (const std::optional<PropPreviewAnchor> anchor = model_.PreviewAnchor();
        anchor.has_value()) {
      const ImVec2 point = preview_canvas_.WorldToScreen(
          {static_cast<double>(anchor->x), static_cast<double>(anchor->y)});
      draw_list->AddLine(ImVec2(point.x - kAnchorArmLength, point.y),
                         ImVec2(point.x + kAnchorArmLength, point.y), IM_COL32(255, 96, 96, 255),
                         2.0f);
      draw_list->AddLine(ImVec2(point.x, point.y - kAnchorArmLength),
                         ImVec2(point.x, point.y + kAnchorArmLength), IM_COL32(255, 96, 96, 255),
                         2.0f);
    }
  }
  preview_canvas_.HandleInput();

  const float zoom = preview_canvas_.GetZoom();
  std::move(canvas_end).Invoke();

  gui_->Text("WASD or middle-drag to pan, wheel to zoom  |  Zoom: %.2f", zoom);
  gui_->SameLine();
  if (gui_->Button("Fit##PropArtworkPreview")) frame_pending_ = true;

  if (const PropStageDiagnostic* diagnostic = model_.PreviewDiagnostic(); diagnostic != nullptr) {
    gui_->TextDisabled("%dx%d, %zu visible pixels", diagnostic->width, diagnostic->height,
                       diagnostic->visible_pixels);
  } else {
    gui_->TextDisabled("%dx%d", image->width, image->height);
  }
  return absl::OkStatus();
}

absl::Status PropArtworkEditor::RenderOutput() {
  gui_->BeginDisabled(HasPendingWork());
  auto enabled = absl::MakeCleanup([this] { gui_->EndDisabled(); });
  const PropArtworkOutputPanel::Action action =
      output_panel_->Render(model_, api_->GetAllPropRecipes(), HasPendingWork());
  switch (action) {
    case PropArtworkOutputPanel::Action::kNone:
      break;
    case PropArtworkOutputPanel::Action::kOpenRecipe:
      OpenRecipe();
      break;
    case PropArtworkOutputPanel::Action::kClearWorkspace:
      ClearWorkspace();
      break;
    case PropArtworkOutputPanel::Action::kCopyRecipe:
      model_.StartRecipeCopy();
      break;
    case PropArtworkOutputPanel::Action::kPrepare:
      StartPreparation();
      break;
    case PropArtworkOutputPanel::Action::kCommit:
      CommitPrepared();
      break;
    case PropArtworkOutputPanel::Action::kDelete:
      DeleteProp();
      break;
  }
  return absl::OkStatus();
}

absl::Status PropArtworkEditor::Render() {
  PollGeneration();
  PollWork();

  constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
  ScopedTable table = gui_->CreateScopedTable("PropArtworkEditorLayout", 3, kTableFlags);
  if (table) {
    gui_->TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.56f);
    gui_->TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    gui_->TableNextRow();

    gui_->TableNextColumn();
    {
      ScopedChild child = gui_->CreateScopedChild("PropArtworkControls", ImVec2(0, 0), false);
      RETURN_IF_ERROR(RenderControls());
    }
    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderPreview());
    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderOutput());
  }

  if (std::optional<std::string> selected = gui_->DisplayFileDialog(kSourceDialogKey);
      selected.has_value()) {
    StartImport(*std::move(selected));
  }
  return absl::OkStatus();
}

}  // namespace zebes
