#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr float kControlWidth = 180.0f;
constexpr ImGuiTreeNodeFlags kSectionFlags = ImGuiTreeNodeFlags_DefaultOpen;
const char* AttachmentModeLabel(PropAttachmentMode mode) {
  switch (mode) {
    case PropAttachmentMode::kGrounded:
      return "Grounded";
    case PropAttachmentMode::kCeiling:
      return "Ceiling";
    case PropAttachmentMode::kFree:
      return "Free / background";
  }
  return "Invalid";
}

}  // namespace

absl::StatusOr<std::unique_ptr<PropArtworkControlsPanel>> PropArtworkControlsPanel::Create(
    GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Prop artwork controls require a GUI");
  return absl::WrapUnique(new PropArtworkControlsPanel(gui));
}

PropArtworkControlsPanel::Action PropArtworkControlsPanel::RenderSource(
    PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources) {
  gui_->Text("Accepted source");
  const char* preview = model.source().has_value() ? model.source()->name.c_str() : "(choose)";
  Action action = Action::kNone;

  gui_->BeginDisabled(model.active_recipe().has_value());
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Source##PropArtwork", preview);
    if (combo.IsActive()) {
      for (const SourceArtwork& source : sources) {
        const bool selected = model.source().has_value() && model.source()->id == source.id;
        if (!gui_->Selectable(source.name.c_str(), selected)) continue;
        model.source_to_open() = source.id;
        action = Action::kOpenSource;
      }
    }
  }
  if (gui_->Button("Import PNG...##PropArtwork")) action = Action::kBrowseSource;
  gui_->EndDisabled();

  if (model.active_recipe().has_value()) {
    gui_->TextWrapped("The retained source is fixed for an existing prop. Use Save As first.");
  } else if (!model.source().has_value()) {
    gui_->TextWrapped("Import a source or choose one already retained by the project.");
  } else {
    const SourceArtwork& source = *model.source();
    const std::string question =
        absl::StrCat("Delete retained source '", source.name,
                     "' and its PNG? A saved prop that references it will block deletion.");
    if (delete_source_prompt_.Render(*gui_, "Delete source", source.id, question,
                                     "PropArtworkSource")) {
      action = Action::kDeleteSource;
    }
  }
  return action;
}

absl::Status PropArtworkControlsPanel::RenderTerrain(
    PropArtworkEditorModel& model, const std::vector<TerrainRecipe>& terrain_recipes) {
  gui_->Separator();
  gui_->Text("Terrain style");
  const char* preview = model.terrain_recipe().has_value()
                            ? model.terrain_recipe()->name.c_str()
                            : (model.has_style() ? "(detached snapshot)" : "(choose)");
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Terrain##PropArtwork", preview);
    if (combo.IsActive()) {
      for (const TerrainRecipe& terrain : terrain_recipes) {
        const bool selected =
            model.terrain_recipe().has_value() && model.terrain_recipe()->id == terrain.id;
        if (!gui_->Selectable(terrain.name.c_str(), selected)) continue;
        RETURN_IF_ERROR(model.AttachTerrain(terrain));
      }
    }
  }

  if (!model.terrain_recipe().has_value()) return absl::OkStatus();
  if (gui_->Button("Refresh style##PropArtwork")) {
    const std::string attached_id = model.terrain_recipe()->id;
    const auto found = std::find_if(
        terrain_recipes.begin(), terrain_recipes.end(),
        [&attached_id](const TerrainRecipe& recipe) { return recipe.id == attached_id; });
    if (found == terrain_recipes.end()) {
      return absl::NotFoundError("attached terrain recipe is no longer available");
    }
    RETURN_IF_ERROR(model.AttachTerrain(*found));
  }
  gui_->SameLine();
  if (gui_->Button("Detach style##PropArtwork")) model.DetachTerrain();
  return absl::OkStatus();
}

bool PropArtworkControlsPanel::RenderPipeline(PropArtworkEditorModel& model) {
  if (!gui_->CollapsingHeader("Transform settings##PropArtwork", kSectionFlags)) return false;

  PropArtworkPipelineConfig& pipeline = model.settings().pipeline;
  bool changed = false;
  const PropAttachmentMode current_mode = pipeline.composition.attachment.mode;
  gui_->SetNextItemWidth(kControlWidth);
  {
    ScopedCombo combo =
        gui_->CreateScopedCombo("Attachment##PropArtwork", AttachmentModeLabel(current_mode));
    if (combo.IsActive()) {
      static constexpr std::array<PropAttachmentMode, 3> kModes = {
          PropAttachmentMode::kGrounded,
          PropAttachmentMode::kCeiling,
          PropAttachmentMode::kFree,
      };
      for (const PropAttachmentMode candidate : kModes) {
        if (!gui_->Selectable(AttachmentModeLabel(candidate), candidate == current_mode)) continue;
        if (candidate == current_mode) continue;
        pipeline.composition.attachment.mode = candidate;
        if (candidate == PropAttachmentMode::kFree) {
          pipeline.composition.attachment.free_anchor = PropFreeAnchor{
              .x = model.settings().style.tile_size * pipeline.composition.canvas_tiles_wide / 2,
              .y = model.settings().style.tile_size * pipeline.composition.canvas_tiles_high / 2,
          };
        } else {
          pipeline.composition.attachment.free_anchor.reset();
        }
        changed = true;
      }
    }
  }
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Canvas width (tiles)##PropArtwork",
                             &pipeline.composition.canvas_tiles_wide, 1, 8);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Canvas height (tiles)##PropArtwork",
                             &pipeline.composition.canvas_tiles_high, 1, 8);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Padding##PropArtwork", &pipeline.composition.padding_fraction, 0.0f,
                               0.25f, "%.2f");

  if (pipeline.composition.attachment.mode == PropAttachmentMode::kFree) {
    if (!pipeline.composition.attachment.free_anchor.has_value()) {
      pipeline.composition.attachment.free_anchor = PropFreeAnchor{};
      changed = true;
    }
    const int maximum_x =
        model.settings().style.tile_size * pipeline.composition.canvas_tiles_wide - 1;
    const int maximum_y =
        model.settings().style.tile_size * pipeline.composition.canvas_tiles_high - 1;
    PropFreeAnchor& anchor = *pipeline.composition.attachment.free_anchor;
    const int clamped_x = std::clamp(anchor.x, 0, maximum_x);
    const int clamped_y = std::clamp(anchor.y, 0, maximum_y);
    changed |= clamped_x != anchor.x || clamped_y != anchor.y;
    anchor.x = clamped_x;
    anchor.y = clamped_y;
    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Anchor X##PropArtwork", &anchor.x, 0, maximum_x);
    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Anchor Y##PropArtwork", &anchor.y, 0, maximum_y);
  }

  if (model.has_style()) {
    static constexpr std::array<int, 5> kPixelBlocks = {1, 2, 4, 8, 16};
    const int current = model.settings().style.pixel_block_size;
    const std::string label = std::to_string(current);
    ScopedCombo combo = gui_->CreateScopedCombo("Pixel block##PropArtwork", label.c_str());
    if (combo.IsActive()) {
      for (const int candidate : kPixelBlocks) {
        if (model.settings().style.tile_size % candidate != 0) continue;
        const std::string candidate_label = std::to_string(candidate);
        if (!gui_->Selectable(candidate_label.c_str(), candidate == current)) continue;
        model.settings().style.pixel_block_size = candidate;
        changed = true;
      }
    }
  }

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Background tolerance##PropArtwork",
                               &pipeline.isolation.background_distance, 1.0f, 96.0f, "%.0f");
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Minimum subject area##PropArtwork",
                             &pipeline.isolation.minimum_subject_area, 1, 4096);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Outline width##PropArtwork", &pipeline.edge.width, 0, 4);
  gui_->SetNextItemWidth(kControlWidth);
  changed |=
      gui_->SliderInt("Debris area##PropArtwork", &pipeline.cleanup.minimum_component_area, 0, 64);
  if (pipeline.composition.attachment.mode != PropAttachmentMode::kFree) {
    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Contact tolerance##PropArtwork",
                               &pipeline.cleanup.contact_tolerance, 0, 12);
  }
  return changed;
}

PropArtworkControlsPanel::Action PropArtworkControlsPanel::RenderGeneration(
    PropArtworkEditorModel& model, PropGenerationStatus& generation) {
  gui_->Separator();
  gui_->Text("Generate source");

  const ImageGenerationLifecycleResult lifecycle = RenderImageGenerationLifecycle(
      *gui_,
      {
          .editor_id = "PropArtwork",
          .can_accept_candidate = !model.active_recipe().has_value(),
          .acceptance_blocked_message =
              "An open prop keeps its retained source. Use Save As before accepting.",
      },
      generation);
  Action action = Action::kNone;
  switch (lifecycle.action) {
    case ImageGenerationLifecycleAction::kNone:
      break;
    case ImageGenerationLifecycleAction::kSelectProvider:
      action = Action::kSelectGenerationProvider;
      break;
    case ImageGenerationLifecycleAction::kCancel:
      action = Action::kCancelGeneration;
      break;
    case ImageGenerationLifecycleAction::kSelectCandidate:
      action = Action::kSelectCandidate;
      break;
    case ImageGenerationLifecycleAction::kAcceptCandidate:
      action = Action::kAcceptCandidate;
      break;
    case ImageGenerationLifecycleAction::kDiscardCandidates:
      action = Action::kDiscardCandidates;
      break;
  }
  if (!lifecycle.show_draft) return action;

  const PropGenerationProviderStatus* selected = nullptr;
  if (generation.selected_provider < generation.providers.size()) {
    selected = &generation.providers[generation.selected_provider];
  }

  const bool provider_available = selected != nullptr && selected->available;
  const float text_height = gui_->GetTextLineHeightWithSpacing();
  gui_->Text("Prompt");
  gui_->InputTextMultiline("##PropArtworkPrompt", &model.prompt(),
                           ImVec2(-1.0f, text_height * 5.0f));
  gui_->Text("System prompt");
  gui_->InputTextMultiline("##PropArtworkSystemPrompt", &model.generation_instructions(),
                           ImVec2(-1.0f, text_height * 7.0f));
  gui_->SetNextItemWidth(kControlWidth);
  {
    const PropArtworkStylePreset current = model.style_preset();
    ScopedCombo combo = gui_->CreateScopedCombo("Style preset##PropArtworkGeneration",
                                                ArtworkGenerationStylePresetLabel(current));
    if (combo.IsActive()) {
      for (const PropArtworkStylePreset preset : kArtworkGenerationStylePresets) {
        if (!gui_->Selectable(ArtworkGenerationStylePresetLabel(preset), preset == current)) {
          continue;
        }
        if (preset != current) model.SetStylePreset(preset);
      }
    }
  }
  gui_->Text("Style guidance");
  if (gui_->InputTextMultiline("##PropArtworkStyleGuidance", &model.style_guidance(),
                               ImVec2(-1.0f, text_height * 4.0f))) {
    model.MarkStyleGuidanceCustom();
  }

  const int maximum = std::max(1, generation.capabilities.maximum_candidates);
  int requested = model.requested_candidates();
  if (maximum > 1) {
    gui_->SetNextItemWidth(kControlWidth);
    if (gui_->SliderInt("Candidates##PropArtwork", &requested, 1, maximum)) {
      model.SetRequestedCandidates(requested, maximum);
    }
  }
  // Re-clamped every frame rather than only on edit: the adapter's ceiling is
  // whatever the running provider reports, not what it reported when the
  // number was chosen.
  model.SetRequestedCandidates(model.requested_candidates(), maximum);

  gui_->BeginDisabled(!provider_available || model.prompt().empty() ||
                      model.active_recipe().has_value());
  const bool generate = gui_->Button("Generate##PropArtwork");
  gui_->EndDisabled();
  if (!provider_available) {
    const char* reason = selected == nullptr || selected->unavailable_reason.empty()
                             ? "No image generation provider is available."
                             : selected->unavailable_reason.c_str();
    gui_->TextWrapped("Image generation is unavailable: %s", reason);
  }
  if (model.prompt().empty()) {
    gui_->TextWrapped("Describe the prop to generate a source for it.");
  }
  if (action != Action::kNone) return action;
  return provider_available && generate ? Action::kGenerate : Action::kNone;
}

absl::StatusOr<PropArtworkControlsPanel::Action> PropArtworkControlsPanel::Render(
    PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources,
    const std::vector<TerrainRecipe>& terrain_recipes, PropGenerationStatus& generation) {
  Action action = RenderSource(model, sources);
  // One click reaches one widget, so at most one section reports an action.
  if (const Action generated = RenderGeneration(model, generation); generated != Action::kNone) {
    action = generated;
  }
  RETURN_IF_ERROR(RenderTerrain(model, terrain_recipes));
  gui_->Separator();
  if (RenderPipeline(model)) model.MarkInputsChanged();
  return action;
}

}  // namespace zebes
