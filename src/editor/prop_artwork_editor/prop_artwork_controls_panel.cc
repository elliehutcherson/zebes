#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"

#include <algorithm>
#include <array>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr float kControlWidth = 180.0f;
constexpr ImGuiTreeNodeFlags kSectionFlags = ImGuiTreeNodeFlags_DefaultOpen;

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
  }
  return action;
}

absl::Status PropArtworkControlsPanel::RenderTerrain(
    PropArtworkEditorModel& model, const std::vector<TerrainRecipe>& terrain_recipes) {
  gui_->Separator();
  gui_->Text("Terrain style");
  const char* preview =
      model.terrain_recipe().has_value() ? model.terrain_recipe()->name.c_str() : "(choose)";
  ScopedCombo combo = gui_->CreateScopedCombo("Terrain##PropArtwork", preview);
  if (!combo.IsActive()) return absl::OkStatus();

  for (const TerrainRecipe& terrain : terrain_recipes) {
    const bool selected =
        model.terrain_recipe().has_value() && model.terrain_recipe()->id == terrain.id;
    if (!gui_->Selectable(terrain.name.c_str(), selected)) continue;
    RETURN_IF_ERROR(model.AttachTerrain(terrain));
  }
  return absl::OkStatus();
}

bool PropArtworkControlsPanel::RenderPipeline(PropArtworkEditorModel& model) {
  if (!gui_->CollapsingHeader("Transform settings##PropArtwork", kSectionFlags)) return false;

  PropArtworkPipelineConfig& pipeline = model.settings().pipeline;
  bool changed = false;
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Canvas width (tiles)##PropArtwork",
                             &pipeline.composition.canvas_tiles_wide, 1, 8);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Canvas height (tiles)##PropArtwork",
                             &pipeline.composition.canvas_tiles_high, 1, 8);
  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Padding##PropArtwork", &pipeline.composition.padding_fraction, 0.0f,
                               0.25f, "%.2f");

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
  gui_->SetNextItemWidth(kControlWidth);
  changed |=
      gui_->SliderInt("Ground tolerance##PropArtwork", &pipeline.cleanup.grounded_tolerance, 0, 12);
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
