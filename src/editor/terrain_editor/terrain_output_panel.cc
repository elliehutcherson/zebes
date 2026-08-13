#include "editor/terrain_editor/terrain_output_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr float kFieldWidth = 200.0f;

const char* SourceLabel(TerrainEditorModel::Source source) {
  return source == TerrainEditorModel::Source::kGenerate ? "Generate" : "Import manifest";
}

}  // namespace

absl::StatusOr<std::unique_ptr<TerrainOutputPanel>> TerrainOutputPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("TerrainOutputPanel requires a GUI");
  return absl::WrapUnique(new TerrainOutputPanel(gui));
}

bool TerrainOutputPanel::RenderSourceSelector(TerrainEditorModel& model) {
  bool changed = false;
  ScopedCombo combo = gui_->CreateScopedCombo("Source##TerrainOut", SourceLabel(model.source()));
  if (!combo.IsActive()) return changed;

  for (const TerrainEditorModel::Source source :
       {TerrainEditorModel::Source::kGenerate, TerrainEditorModel::Source::kImportManifest}) {
    if (!gui_->Selectable(SourceLabel(source), source == model.source())) continue;
    model.SetSource(source);
    changed = true;
  }
  return changed;
}

void TerrainOutputPanel::RenderTexturePicker(TerrainEditorModel& model,
                                             const std::vector<Texture>& textures) {
  std::string preview = "(choose)";
  for (const Texture& texture : textures) {
    if (texture.id == model.texture_id()) preview = texture.name;
  }

  ScopedCombo combo = gui_->CreateScopedCombo("Texture##TerrainOut", preview.c_str());
  if (!combo.IsActive()) return;
  for (const Texture& texture : textures) {
    if (!gui_->Selectable(texture.name.c_str(), texture.id == model.texture_id())) continue;
    model.texture_id() = texture.id;
  }
}

void TerrainOutputPanel::RenderSummary(TerrainEditorModel& model) {
  if (model.source() != TerrainEditorModel::Source::kGenerate) {
    gui_->TextDisabled("Tiles come from the manifest.");
    return;
  }

  const TerrainGenConfig& config = model.config();
  gui_->TextDisabled("%s", absl::StrFormat("%d tiles at %dpx", model.TileCount(),
                                           config.tile_size)
                               .c_str());
}

absl::StatusOr<TerrainOutputPanel::Action> TerrainOutputPanel::Render(
    TerrainEditorModel& model, const std::vector<Texture>& textures) {
  gui_->Text("Output");

  RenderSourceSelector(model);

  gui_->SetNextItemWidth(kFieldWidth);
  gui_->InputText("Name##TerrainOut", &model.name());
  if (gui_->IsItemHovered()) {
    gui_->SetTooltip("Names the tileset, the terrain, and the artwork file.");
  }

  if (model.source() == TerrainEditorModel::Source::kImportManifest) {
    RenderTexturePicker(model, textures);
  } else {
    // Quality only affects what Create writes: the preview always draws at
    // draft quality so it can keep up with a slider. It sits here rather than
    // with the tuning controls so it does not read as something that changes
    // the picture.
    gui_->SetNextItemWidth(kFieldWidth);
    gui_->SliderInt("Quality##TerrainOut", &model.config().supersample, 1, 4);
    if (gui_->IsItemHovered()) {
      gui_->SetTooltip("Samples per pixel when the artwork is written. Does not affect the preview.");
    }
  }

  RenderSummary(model);
  gui_->Separator();

  const bool importing = model.source() == TerrainEditorModel::Source::kImportManifest;
  const bool missing_texture = importing && model.texture_id().empty();
  const bool missing_manifest = importing && model.manifest_path().empty();
  const bool blocked = model.name().empty() || missing_texture || missing_manifest;

  Action action = Action::kNone;
  gui_->BeginDisabled(blocked);
  if (gui_->Button("Create##TerrainOut")) action = Action::kCreate;
  gui_->EndDisabled();

  // Say which requirement is unmet rather than leaving a dead button.
  if (model.name().empty()) gui_->TextWrapped("Name it first.");
  if (missing_manifest) gui_->TextWrapped("Choose a manifest file.");
  if (missing_texture) gui_->TextWrapped("Choose the texture the manifest describes.");

  if (!model.status().empty()) gui_->TextWrapped("%s", model.status().c_str());

  if (model.result().has_value()) {
    gui_->TextDisabled("%s", absl::StrFormat("Tileset %s, %d tiles",
                                             model.result()->tileset_id.c_str(),
                                             model.result()->tile_count)
                                 .c_str());
  }
  return action;
}

}  // namespace zebes
