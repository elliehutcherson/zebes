#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"

#include <algorithm>
#include <array>

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

absl::StatusOr<PropArtworkControlsPanel::Action> PropArtworkControlsPanel::Render(
    PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources,
    const std::vector<TerrainRecipe>& terrain_recipes) {
  const Action action = RenderSource(model, sources);
  RETURN_IF_ERROR(RenderTerrain(model, terrain_recipes));
  gui_->Separator();
  if (RenderPipeline(model)) model.MarkInputsChanged();
  return action;
}

}  // namespace zebes
