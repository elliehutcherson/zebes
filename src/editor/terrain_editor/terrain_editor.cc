#include "editor/terrain_editor/terrain_editor.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/terrain_editor/terrain_creation.h"

namespace zebes {
namespace {

// Height reserved under the canvas for the status line, matching the other
// viewports in the editor.
constexpr float kStatusBarHeight = 25.0f;

// Fraction of the viewport the framed scene fills, so it has visible margin
// rather than touching the edges.
constexpr double kFrameFill = 0.85;

absl::StatusOr<std::string> ReadFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("Could not open ", path));
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

}  // namespace

TerrainEditor::TerrainEditor(Api* api, GuiInterface* gui, PreviewTextureSink* preview)
    : api_(api),
      gui_(gui),
      preview_(preview),
      canvas_(Canvas::Options{.gui = gui, .grid_size = 32.0f}) {}

absl::StatusOr<std::unique_ptr<TerrainEditor>> TerrainEditor::Create(Api* api, GuiInterface* gui,
                                                                     PreviewTextureSink* preview) {
  if (api == nullptr) return absl::InvalidArgumentError("Api is null.");
  if (gui == nullptr) return absl::InvalidArgumentError("GuiInterface is null.");
  if (preview == nullptr) return absl::InvalidArgumentError("PreviewTextureSink is null.");

  auto editor = absl::WrapUnique(new TerrainEditor(api, gui, preview));
  RETURN_IF_ERROR(editor->Init());
  return editor;
}

absl::Status TerrainEditor::Init() {
  ASSIGN_OR_RETURN(controls_panel_, TerrainControlsPanel::Create(gui_));
  ASSIGN_OR_RETURN(output_panel_, TerrainOutputPanel::Create(gui_));
  return absl::OkStatus();
}

absl::Status TerrainEditor::RenderControls() {
  if (controls_panel_->Render(model_)) model_.MarkPreviewStale();

  // Asked of the whole panel rather than of the last widget: the item rendered
  // last is whatever the bottom section ended with, not the slider under the
  // cursor, so asking it whether it is active always answered about the wrong
  // control.
  // A configuration the generator refuses is ordinary while tuning, so it is
  // reported beside the controls rather than failing the frame.
  const absl::Status refreshed = model_.RefreshPreviewIfNeeded(gui_->IsAnyItemActive());
  if (!refreshed.ok()) model_.SetStatus(std::string(refreshed.message()));
  return absl::OkStatus();
}

void TerrainEditor::FrameScene(const ImVec2& viewport_size) {
  if (!model_.preview().has_value()) return;
  const RgbaImage& image = *model_.preview();
  if (image.width <= 0 || image.height <= 0) return;
  if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) return;

  const double fit_x = viewport_size.x / static_cast<double>(image.width);
  const double fit_y = viewport_size.y / static_cast<double>(image.height);
  camera_.zoom = std::clamp(std::min(fit_x, fit_y) * kFrameFill, 0.1, 10.0);
  camera_.position = Vec{image.width / 2.0, image.height / 2.0};
}

absl::Status TerrainEditor::RenderViewport() {
  if (model_.source() == TerrainEditorModel::Source::kImportManifest) {
    gui_->TextWrapped(
        "An imported manifest describes artwork that already exists, so there is nothing to "
        "preview here. Check it in the Tileset Editor after creating it.");
    return absl::OkStatus();
  }
  if (!model_.preview().has_value()) {
    gui_->Text("No preview.");
    return absl::OkStatus();
  }

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y -= kStatusBarHeight;
  if (canvas_size.x <= 0.0f || canvas_size.y <= 0.0f) return absl::OkStatus();

  if (frame_pending_) {
    FrameScene(canvas_size);
    frame_pending_ = false;
  }

  const RgbaImage& image = *model_.preview();
  ASSIGN_OR_RETURN(const ImTextureID texture, preview_->Upload(image));

  // World bounds are deliberately left unset. Canvas clamps a bounded world to
  // fill the viewport, which would force this small scene to be blown up with
  // no way to see it whole with margin around it.
  canvas_.SetGridSize(static_cast<float>(model_.config().tile_size));
  canvas_.Begin("TerrainCanvas", canvas_size, camera_);
  auto canvas_end = absl::MakeCleanup([this] { canvas_.End(); });

  if (ImDrawList* draw_list = canvas_.GetDrawList(); draw_list != nullptr) {
    const ImVec2 min = canvas_.WorldToScreen({0, 0});
    const ImVec2 max = canvas_.WorldToScreen(
        {static_cast<double>(image.width), static_cast<double>(image.height)});
    draw_list->AddImage(texture, min, max);
    canvas_.DrawGrid();
  }
  canvas_.HandleInput();

  const float zoom = canvas_.GetZoom();
  std::move(canvas_end).Invoke();

  gui_->Text("WASD to pan, wheel to zoom  |  Zoom: %.2f", zoom);
  gui_->SameLine();
  if (gui_->Button("Fit##TerrainView")) frame_pending_ = true;
  return absl::OkStatus();
}

void TerrainEditor::CreateTerrain() {
  absl::StatusOr<CreatedTerrain> created = absl::InternalError("Unknown terrain source");

  if (model_.source() == TerrainEditorModel::Source::kGenerate) {
    created = CreateGeneratedTerrainTileset(*api_, model_.name(), model_.config(),
                                            model_.selected_preset());
  } else {
    absl::StatusOr<std::string> manifest = ReadFile(model_.manifest_path());
    created = manifest.ok() ? CreateImportedTerrainTileset(*api_, model_.name(),
                                                           model_.texture_id(), *manifest)
                            : absl::StatusOr<CreatedTerrain>(manifest.status());
  }

  if (!created.ok()) {
    model_.SetStatus(std::string(created.status().message()));
    return;
  }
  model_.SetResult(*created);
  if (!created->recipe_id.empty()) {
    absl::StatusOr<TerrainRecipe*> recipe = api_->GetTerrainRecipe(created->recipe_id);
    if (recipe.ok()) model_.LoadRecipe(**recipe);
  }
  model_.SetStatus(absl::StrCat("Created '", model_.name(), "'. Open it in the Tileset Editor."));
}

void TerrainEditor::OpenRecipe() {
  absl::StatusOr<TerrainRecipe*> recipe = api_->GetTerrainRecipe(model_.recipe_to_open());
  if (!recipe.ok()) {
    model_.SetStatus(std::string(recipe.status().message()));
    return;
  }
  model_.LoadRecipe(**recipe);
  frame_pending_ = true;
  model_.SetStatus(absl::StrCat("Opened '", (*recipe)->name, "'."));
}

void TerrainEditor::RegenerateTerrain() {
  if (!model_.active_recipe().has_value()) {
    model_.SetStatus("Open a terrain recipe before regenerating it.");
    return;
  }
  const TerrainRecipe recipe = *model_.active_recipe();
  const absl::Status status =
      RegenerateTerrainTileset(*api_, recipe, model_.config());
  if (!status.ok()) {
    model_.SetStatus(std::string(status.message()));
    return;
  }
  absl::StatusOr<TerrainRecipe*> saved = api_->GetTerrainRecipe(recipe.id);
  if (saved.ok()) model_.LoadRecipe(**saved);
  model_.SetStatus(absl::StrCat("Regenerated '", recipe.name, "' without changing asset IDs."));
}

void TerrainEditor::DeleteTerrain() {
  if (!model_.active_recipe().has_value()) {
    model_.SetStatus("Open a terrain recipe before deleting it.");
    return;
  }
  const TerrainRecipe recipe = *model_.active_recipe();
  const absl::Status status = api_->DeleteGeneratedTerrain(recipe.id);
  if (!status.ok()) {
    // A refusal names every referrer, so it is the whole message rather than a
    // reason appended to one.
    model_.SetStatus(std::string(status.message()));
    return;
  }

  // Resets the whole model rather than only the recipe binding: the tuning on
  // screen described artwork that no longer exists, and leaving it would invite
  // a Create that silently made a second terrain under the same name.
  model_.StartNewRecipe();
  frame_pending_ = true;
  model_.SetStatus(absl::StrCat("Deleted '", recipe.name, "', its tileset, and its artwork."));
}

absl::Status TerrainEditor::RenderOutput() {
  std::vector<Texture> textures;
  if (model_.source() == TerrainEditorModel::Source::kImportManifest) {
    absl::StatusOr<std::vector<Texture>> loaded = api_->GetAllTextures();
    if (loaded.ok()) textures = *std::move(loaded);
  }

  ASSIGN_OR_RETURN(const TerrainOutputPanel::Action action,
                   output_panel_->Render(model_, textures, api_->GetAllTerrainRecipes()));
  switch (action) {
    case TerrainOutputPanel::Action::kNone:
      break;
    case TerrainOutputPanel::Action::kCreate:
      CreateTerrain();
      break;
    case TerrainOutputPanel::Action::kOpenRecipe:
      OpenRecipe();
      break;
    case TerrainOutputPanel::Action::kNewRecipe:
      model_.StartNewRecipe();
      frame_pending_ = true;
      break;
    case TerrainOutputPanel::Action::kCopyRecipe:
      model_.StartRecipeCopy();
      break;
    case TerrainOutputPanel::Action::kRegenerate:
      RegenerateTerrain();
      break;
    case TerrainOutputPanel::Action::kDeleteTerrain:
      DeleteTerrain();
      break;
  }
  return absl::OkStatus();
}

absl::Status TerrainEditor::Render() {
  if (error_message_.has_value()) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", error_message_->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss##TerrainEditor")) error_message_ = std::nullopt;
  }

  constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
  ScopedTable table = gui_->CreateScopedTable("TerrainEditorLayout", 3, kTableFlags);
  if (!table) return absl::OkStatus();

  gui_->TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch, 0.2f);
  gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.6f);
  gui_->TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 0.2f);
  gui_->TableNextRow();

  // Each column reports its own failure rather than abandoning the frame, so a
  // bad configuration does not take the whole tab down mid-tune.
  gui_->TableNextColumn();
  {
    ScopedChild child = gui_->CreateScopedChild("TerrainControls", ImVec2(0, 0), false);
    if (absl::Status status = RenderControls(); !status.ok()) {
      error_message_ = std::string(status.message());
    }
  }

  gui_->TableNextColumn();
  if (absl::Status status = RenderViewport(); !status.ok()) {
    error_message_ = std::string(status.message());
  }

  gui_->TableNextColumn();
  if (absl::Status status = RenderOutput(); !status.ok()) {
    error_message_ = std::string(status.message());
  }

  return absl::OkStatus();
}

}  // namespace zebes
