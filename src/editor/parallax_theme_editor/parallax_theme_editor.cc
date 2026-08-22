#include "editor/parallax_theme_editor/parallax_theme_editor.h"

#include <algorithm>
#include <map>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/viewport_scene.h"
#include "imgui.h"

namespace zebes {
namespace {

constexpr CameraZoomRange kAuthoringZoomRange{.minimum = 0.5, .maximum = 2.0};
constexpr float kLibraryWidth = 280.0f;
constexpr float kInspectorWidth = 360.0f;
constexpr float kDiagnosticsHeight = 220.0f;

std::string UsageLabel(const ParallaxThemeUsage& usage) {
  return absl::StrCat(usage.level_name, " / ", usage.zone_name, " (", usage.level_id, ":",
                      usage.zone_id, ")");
}

bool MatchesTextureSearch(const Texture& texture, const std::string& search) {
  return search.empty() || absl::StrContainsIgnoreCase(texture.name, search) ||
         absl::StrContainsIgnoreCase(texture.id, search);
}

}  // namespace

absl::StatusOr<std::unique_ptr<ParallaxThemeEditor>> ParallaxThemeEditor::Create(
    Api* api, GuiInterface* gui) {
  if (api == nullptr) return absl::InvalidArgumentError("Api must not be null.");
  if (gui == nullptr) return absl::InvalidArgumentError("Gui must not be null.");
  return absl::WrapUnique(new ParallaxThemeEditor(api, gui));
}

ParallaxThemeEditor::ParallaxThemeEditor(Api* api, GuiInterface* gui)
    : api_(api),
      gui_(gui),
      texture_preview_(*gui),
      preview_canvas_(
          {.gui = gui, .logical_viewport = api->GetConfig()->game_view, .show_rulers = false}),
      viewport_renderer_(preview_canvas_),
      manual_route_min_(api->GetConfig()->game_view.width / 2.0,
                        api->GetConfig()->game_view.height / 2.0),
      manual_route_max_(manual_route_min_) {}

absl::Status ParallaxThemeEditor::OpenTheme(const std::string& theme_id) {
  ASSIGN_OR_RETURN(ParallaxTheme * theme, api_->GetParallaxTheme(theme_id));
  if (theme == nullptr) return absl::FailedPreconditionError("Theme lookup returned null.");
  model_.Open(*theme);
  diagnostics_.reset();
  context_level_id_.clear();
  context_zone_id_.reset();
  manual_context_ = false;
  error_.reset();
  return absl::OkStatus();
}

void ParallaxThemeEditor::SetError(const absl::Status& status) {
  if (!status.ok()) error_ = status.message();
}

absl::Status ParallaxThemeEditor::Save() {
  ASSIGN_OR_RETURN(ParallaxTheme request, model_.BuildSaveRequest());
  if (request.id.empty()) {
    ASSIGN_OR_RETURN(const std::string id, api_->CreateParallaxTheme(std::move(request)));
    model_.FinishSave(id);
  } else {
    const std::string id = request.id;
    RETURN_IF_ERROR(api_->UpdateParallaxTheme(std::move(request)));
    model_.FinishSave(id);
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::Duplicate() {
  const ParallaxTheme* draft = model_.draft();
  if (draft == nullptr || draft->id.empty()) {
    return absl::FailedPreconditionError("Save the theme before duplicating it.");
  }
  ParallaxTheme duplicate = *draft;
  duplicate.id.clear();
  duplicate.name = absl::StrCat(duplicate.name, " Copy");
  ASSIGN_OR_RETURN(const std::string id, api_->CreateParallaxTheme(std::move(duplicate)));
  return OpenTheme(id);
}

absl::Status ParallaxThemeEditor::RenderToolbar(ParallaxTheme& draft) {
  if (gui_->Button("New Theme")) {
    model_.BeginNew();
    diagnostics_.reset();
    context_level_id_.clear();
    context_zone_id_.reset();
    manual_context_ = true;
    return absl::OkStatus();
  }
  gui_->SameLine();
  gui_->SetNextItemWidth(260.0f);
  gui_->InputText("Theme Name", &draft.name);
  gui_->SameLine();
  if (gui_->Button("Save")) SetError(Save());
  gui_->SameLine();
  if (gui_->Button("Duplicate")) {
    SetError(Duplicate());
    return absl::OkStatus();
  }
  if (!draft.id.empty()) {
    gui_->SameLine();
    if (delete_prompt_.Render(*gui_, "Delete Theme", draft.id,
                              absl::StrCat("Delete '", draft.name, "'?"), "ParallaxTheme")) {
      RETURN_IF_ERROR(api_->DeleteParallaxTheme(draft.id));
      model_.Close();
      diagnostics_.reset();
      return absl::OkStatus();
    }
  }
  gui_->SameLine();
  gui_->Checkbox("Diagnostics", &show_diagnostics_);
  if (model_.dirty()) {
    gui_->SameLine();
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "Unsaved changes");
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderLibrary(ParallaxTheme& draft) {
  gui_->Text("THEMES");
  gui_->Separator();
  for (const ParallaxTheme& theme : api_->GetAllParallaxThemes()) {
    const bool selected = !draft.id.empty() && draft.id == theme.id;
    const std::string label = absl::StrCat(theme.name, "##", theme.id);
    if (gui_->Selectable(label.c_str(), selected)) return OpenTheme(theme.id);
  }

  gui_->Spacing();
  gui_->Separator();
  gui_->Text("LAYERS (FAR TO NEAR)");
  gui_->SameLine();
  if (gui_->Button("Add Layer")) RETURN_IF_ERROR(model_.AddLayer());
  for (int index = 0; index < static_cast<int>(draft.layers.size()); ++index) {
    ScopedId id = gui_->CreateScopedId(index);
    const std::string label = absl::StrCat(index + 1, ".  ", draft.layers[index].name);
    if (gui_->Selectable(label.c_str(), model_.selected_layer() == index)) {
      model_.SelectLayer(index);
      diagnostics_.reset();
    }
  }
  gui_->TextDisabled("First draws farthest back; last draws nearest.");
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderTexturePicker(ParallaxLayer& layer,
                                                      const std::vector<Texture>& textures) {
  gui_->InputText("Search Textures", &texture_search_);
  ScopedChild catalog = gui_->CreateScopedChild("TextureCatalog", ImVec2(0, 220), true);
  if (!catalog) return absl::OkStatus();

  for (const Texture& texture : textures) {
    if (!MatchesTextureSearch(texture, texture_search_)) continue;
    ScopedId id = gui_->CreateScopedId(texture.id.c_str());
    ASSIGN_OR_RETURN(const TextureHandle handle, api_->GetTextureHandle(texture.id));
    ASSIGN_OR_RETURN(const AtlasBinding binding, texture_preview_.BindAtlas(handle));
    if (binding.IsValid()) {
      ASSIGN_OR_RETURN(const TexturePreviewLayout layout,
                       CalculateTexturePreviewLayout(binding.width, binding.height, 40.0f, 40.0f));
      gui_->Image(binding.texture_id, {layout.display_width, layout.display_height});
      gui_->SameLine();
    }
    if (gui_->Selectable(texture.name_id().c_str(), texture.id == layer.texture_id, 0,
                         ImVec2(0, 40))) {
      layer.texture_id = texture.id;
      diagnostics_.reset();
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::AnalyzeSelectedTexture() {
  const ParallaxTheme* draft = model_.draft();
  if (draft == nullptr || !model_.selected_layer()) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }
  const ParallaxLayer& layer = draft->layers[*model_.selected_layer()];
  if (layer.texture_id.empty()) {
    return absl::FailedPreconditionError("Select a texture before analyzing it.");
  }
  ASSIGN_OR_RETURN(const RgbaImage pixels, api_->ReadTexturePixels(layer.texture_id));
  ASSIGN_OR_RETURN(const RepetitionDiagnostics repetition, AnalyzeRepetition(pixels));
  diagnostics_ = DiagnosticsSnapshot{.texture_id = layer.texture_id, .repetition = repetition};
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderInspector(ParallaxTheme& draft,
                                                  const std::vector<Texture>& textures) {
  gui_->Text("LAYER INSPECTOR");
  gui_->Separator();
  if (!model_.selected_layer()) {
    gui_->TextDisabled("Select a layer to edit it.");
    return absl::OkStatus();
  }

  ParallaxLayer& layer = draft.layers[*model_.selected_layer()];
  gui_->InputText("Layer Name", &layer.name);
  RETURN_IF_ERROR(RenderTexturePicker(layer, textures));
  gui_->Checkbox("Repeat X", &layer.repeat_x);
  gui_->SameLine();
  gui_->Checkbox("Repeat Y", &layer.repeat_y);

  gui_->Text("Depth preset");
  if (gui_->Button("Far")) SetError(model_.ApplyDepthPreset(ParallaxDepthPreset::kFar));
  gui_->SameLine();
  if (gui_->Button("Middle")) SetError(model_.ApplyDepthPreset(ParallaxDepthPreset::kMiddle));
  gui_->SameLine();
  if (gui_->Button("Near Background")) {
    SetError(model_.ApplyDepthPreset(ParallaxDepthPreset::kNearBackground));
  }
  gui_->InputDouble("Scroll X", &layer.scroll_factor.x);
  gui_->InputDouble("Scroll Y", &layer.scroll_factor.y);
  if (layer.scroll_factor.x < 0.0 || layer.scroll_factor.x > 1.0 || layer.scroll_factor.y < 0.0 ||
      layer.scroll_factor.y > 1.0) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "Background scroll is normally within [0, 1].");
  }
  gui_->InputDouble("Offset X", &layer.offset.x);
  gui_->InputDouble("Offset Y", &layer.offset.y);
  gui_->InputFloat("Scale", &layer.base_scale);
  if (gui_->Button("Move Farther")) SetError(model_.MoveSelectedLayer(-1));
  gui_->SameLine();
  if (gui_->Button("Move Nearer")) SetError(model_.MoveSelectedLayer(1));
  gui_->SameLine();
  if (gui_->Button("Delete Layer")) RETURN_IF_ERROR(model_.DeleteSelectedLayer());
  return absl::OkStatus();
}

ParallaxThemeEditor::PreviewContext ParallaxThemeEditor::RenderContextPicker(
    const ParallaxTheme& draft, const std::vector<Level>& levels) {
  const std::vector<ParallaxThemeUsage> usages = FindParallaxThemeUsages(levels, draft.id);
  if (usages.empty()) {
    manual_context_ = true;
    context_level_id_.clear();
    context_zone_id_.reset();
  } else if (!manual_context_) {
    const bool current_exists =
        std::any_of(usages.begin(), usages.end(), [this](const ParallaxThemeUsage& usage) {
          return usage.level_id == context_level_id_ && usage.zone_id == context_zone_id_;
        });
    if (!current_exists) {
      context_level_id_ = usages.front().level_id;
      context_zone_id_ = usages.front().zone_id;
    }
  }

  const ParallaxThemeUsage* selected_usage = nullptr;
  for (const ParallaxThemeUsage& usage : usages) {
    if (!manual_context_ && usage.level_id == context_level_id_ &&
        usage.zone_id == context_zone_id_) {
      selected_usage = &usage;
      break;
    }
  }

  const std::string preview =
      selected_usage == nullptr ? "Manual camera route" : UsageLabel(*selected_usage);
  if (ScopedCombo combo = gui_->CreateScopedCombo("Camera Context", preview.c_str()); combo) {
    if (gui_->Selectable("Manual camera route", selected_usage == nullptr)) {
      manual_context_ = true;
      context_level_id_.clear();
      context_zone_id_.reset();
      selected_usage = nullptr;
    }
    for (const ParallaxThemeUsage& usage : usages) {
      const bool selected = selected_usage != nullptr &&
                            usage.level_id == selected_usage->level_id &&
                            usage.zone_id == selected_usage->zone_id;
      if (gui_->Selectable(UsageLabel(usage).c_str(), selected)) {
        manual_context_ = false;
        context_level_id_ = usage.level_id;
        context_zone_id_ = usage.zone_id;
        selected_usage = &usage;
      }
    }
  }

  if (selected_usage == nullptr) {
    if (usages.empty()) gui_->TextDisabled("Not assigned to a level zone; using a manual route.");
    return {.route = {.min = manual_route_min_, .max = manual_route_max_}};
  }
  return {.route = selected_usage->route, .world = selected_usage->world};
}

absl::Status ParallaxThemeEditor::RenderViewport(const ParallaxTheme& draft,
                                                 const std::vector<Level>& levels) {
  gui_->Text("GAME VIEW PREVIEW");
  gui_->SameLine();
  gui_->TextDisabled("%dx%d logical px", api_->GetConfig()->game_view.width,
                     api_->GetConfig()->game_view.height);
  gui_->Separator();

  const PreviewContext context = RenderContextPicker(draft, levels);
  gui_->Checkbox("Selected Layer Only", &preview_selected_layer_);
  gui_->SameLine();
  if (gui_->Button("0.5x")) preview_zoom_ = 0.5f;
  gui_->SameLine();
  if (gui_->Button("1x")) preview_zoom_ = 1.0f;
  gui_->SameLine();
  if (gui_->Button("2x")) preview_zoom_ = 2.0f;
  gui_->SliderFloat("Camera Zoom", &preview_zoom_, static_cast<float>(kAuthoringZoomRange.minimum),
                    static_cast<float>(kAuthoringZoomRange.maximum));
  gui_->SliderFloat("Travel X", &travel_x_, 0.0f, 1.0f);
  gui_->SliderFloat("Travel Y", &travel_y_, 0.0f, 1.0f);

  const absl::StatusOr<CameraCenterRoute> resolved = ResolveCameraCenterRoute(
      context.route, api_->GetConfig()->game_view, preview_zoom_, context.world);
  if (!resolved.ok()) {
    const std::string message(resolved.status().message());
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Preview unavailable: %s", message.c_str());
    return absl::OkStatus();
  }
  preview_camera_.position = InterpolateCameraCenter(*resolved, travel_x_, travel_y_);
  preview_camera_.zoom = preview_zoom_;

  std::map<std::string, TextureHandle> handles;
  for (const ParallaxLayer& layer : draft.layers) {
    if (layer.texture_id.empty() || handles.contains(layer.texture_id)) continue;
    ASSIGN_OR_RETURN(handles[layer.texture_id], api_->GetTextureHandle(layer.texture_id));
  }

  const ImVec2 available = gui_->GetContentRegionAvail();
  if (available.x <= 0.0f || available.y <= 0.0f) return absl::OkStatus();
  ASSIGN_OR_RETURN(
      const TexturePreviewLayout layout,
      CalculateTexturePreviewLayout(api_->GetConfig()->game_view.width,
                                    api_->GetConfig()->game_view.height, available.x, available.y));
  const ImVec2 cursor = gui_->GetCursorPos();
  gui_->SetCursorPos({cursor.x + (available.x - layout.display_width) / 2.0f,
                      cursor.y + (available.y - layout.display_height) / 2.0f});

  preview_canvas_.Begin("ThemePreviewCanvas", {layout.display_width, layout.display_height},
                        preview_camera_);
  ParallaxRenderOptions options;
  if (preview_selected_layer_) options.layer_index = model_.selected_layer();
  absl::Status render_status;
  absl::StatusOr<ParallaxRenderBatch> batch =
      ComposeParallaxRenderBatch(draft, preview_camera_, handles, options);
  if (!batch.ok()) {
    render_status = batch.status();
  } else {
    render_status = viewport_renderer_.RenderParallax(*batch);
  }
  preview_canvas_.End();
  return render_status;
}

absl::Status ParallaxThemeEditor::RenderDiagnostics(const ParallaxTheme& draft,
                                                    const std::vector<Level>& levels) {
  gui_->Text("PREVIEW & ASSET DIAGNOSTICS");
  gui_->Separator();
  ScopedTable table = gui_->CreateScopedTable(
      "ParallaxDiagnostics", 3,
      ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame,
      ImVec2(0, 0));
  if (!table) return absl::OkStatus();
  gui_->TableSetupColumn("Camera Route");
  gui_->TableSetupColumn("Coverage");
  gui_->TableSetupColumn("Repetition");
  gui_->TableNextRow();
  gui_->TableNextColumn();

  const PreviewContext context = RenderContextPicker(draft, levels);
  if (manual_context_) {
    gui_->InputDouble("Center Start X", &manual_route_min_.x);
    gui_->InputDouble("Center Start Y", &manual_route_min_.y);
    gui_->InputDouble("Center End X", &manual_route_max_.x);
    gui_->InputDouble("Center End Y", &manual_route_max_.y);
  } else {
    gui_->TextWrapped(
        "The level/zone route is intersected with camera centers reachable inside "
        "the level at each zoom.");
  }

  gui_->TableNextColumn();
  if (!model_.selected_layer()) {
    gui_->TextDisabled("Select a layer to inspect coverage.");
    gui_->TableNextColumn();
    gui_->TextDisabled("Select a layer to inspect repetition.");
    return absl::OkStatus();
  }
  const ParallaxLayer& layer = draft.layers[*model_.selected_layer()];
  if (layer.texture_id.empty()) {
    gui_->TextDisabled("Select a texture to inspect coverage.");
    gui_->TableNextColumn();
    gui_->TextDisabled("Select a texture to inspect repetition.");
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(const TextureHandle handle, api_->GetTextureHandle(layer.texture_id));
  ASSIGN_OR_RETURN(const AtlasBinding binding, texture_preview_.BindAtlas(handle));
  if (!binding.IsValid()) return absl::OkStatus();
  gui_->Text("Texture: %dx%d px", binding.width, binding.height);
  const absl::StatusOr<CameraCoverageDiagnostics> coverage = AnalyzeCameraCoverage(
      layer, binding.width, binding.height, context.route.min, context.route.max,
      api_->GetConfig()->game_view, kAuthoringZoomRange, context.world);
  if (!coverage.ok()) {
    const std::string message(coverage.status().message());
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", message.c_str());
  } else {
    if (coverage->horizontal.repeated) {
      gui_->Text("Horizontal: repeated");
    } else {
      gui_->Text("Horizontal margins: %.1f / %.1f", coverage->horizontal.minimum_start_margin,
                 coverage->horizontal.minimum_end_margin);
    }
    if (coverage->vertical.repeated) {
      gui_->Text("Vertical: repeated");
    } else {
      gui_->Text("Vertical margins: %.1f / %.1f", coverage->vertical.minimum_start_margin,
                 coverage->vertical.minimum_end_margin);
    }
    if (!coverage->horizontal.covers() || !coverage->vertical.covers()) {
      gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "Negative margin = uncovered camera area.");
    }
  }

  gui_->TableNextColumn();
  if (gui_->Button("Analyze Repetition")) SetError(AnalyzeSelectedTexture());
  if (diagnostics_ && diagnostics_->texture_id == layer.texture_id) {
    const OpposingEdgeDifference& horizontal = diagnostics_->repetition.horizontal;
    const OpposingEdgeDifference& vertical = diagnostics_->repetition.vertical;
    gui_->TextWrapped("Left/right: %.2f mean RGBA delta; %d max; %d/%d exact.",
                      horizontal.mean_absolute_channel_difference,
                      horizontal.maximum_channel_difference, horizontal.exact_pixel_matches,
                      horizontal.pixels_compared);
    gui_->TextWrapped("Top/bottom: %.2f mean RGBA delta; %d max; %d/%d exact.",
                      vertical.mean_absolute_channel_difference,
                      vertical.maximum_channel_difference, vertical.exact_pixel_matches,
                      vertical.pixels_compared);
    gui_->TextDisabled("Measurements aid human seam review; they do not prove seamlessness.");
  }
  if (layer.repeat_x) {
    gui_->Text("Wrapped X preview");
    const float width = std::min(90.0f, gui_->GetContentRegionAvail().x / 3.0f);
    if (width > 0.0f) {
      ASSIGN_OR_RETURN(const TexturePreviewLayout layout,
                       CalculateTexturePreviewLayout(binding.width, binding.height, width, 60.0f));
      for (int index = 0; index < 3; ++index) {
        if (index > 0) gui_->SameLine();
        gui_->Image(binding.texture_id, {layout.display_width, layout.display_height});
      }
    }
  }
  if (layer.repeat_y) {
    gui_->Text("Wrapped Y preview");
    ASSIGN_OR_RETURN(const TexturePreviewLayout layout,
                     CalculateTexturePreviewLayout(binding.width, binding.height, 120.0f, 36.0f));
    for (int index = 0; index < 3; ++index) {
      gui_->Image(binding.texture_id, {layout.display_width, layout.display_height});
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::Render() {
  if (error_) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", error_->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss")) error_.reset();
  }

  ParallaxTheme* draft = model_.draft();
  if (draft == nullptr) {
    if (gui_->Button("New Theme")) {
      model_.BeginNew();
      manual_context_ = true;
    }
    gui_->SameLine();
    gui_->TextDisabled("Select a theme below or create a new one.");
    gui_->Separator();
    for (const ParallaxTheme& theme : api_->GetAllParallaxThemes()) {
      const std::string label = absl::StrCat(theme.name, " (", theme.id, ")");
      if (gui_->Selectable(label.c_str())) RETURN_IF_ERROR(OpenTheme(theme.id));
    }
    return absl::OkStatus();
  }

  RETURN_IF_ERROR(RenderToolbar(*draft));
  draft = model_.draft();
  if (draft == nullptr) return absl::OkStatus();
  const std::vector<Level> levels = api_->GetAllLevels();
  ASSIGN_OR_RETURN(const std::vector<Texture> textures, api_->GetAllTextures());

  const ImVec2 available = gui_->GetContentRegionAvail();
  const float diagnostics_height =
      show_diagnostics_ ? std::min(kDiagnosticsHeight, available.y / 3.0f) : 0.0f;
  const float workspace_height = std::max(0.0f, available.y - diagnostics_height);
  {
    ScopedTable workspace =
        gui_->CreateScopedTable("ParallaxThemeWorkspace", 3,
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                                    ImGuiTableFlags_SizingStretchProp,
                                ImVec2(0, workspace_height));
    if (workspace) {
      gui_->TableSetupColumn("Library", ImGuiTableColumnFlags_WidthFixed, kLibraryWidth);
      gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, kInspectorWidth);
      gui_->TableNextRow();
      gui_->TableNextColumn();
      if (ScopedChild library = gui_->CreateScopedChild("ThemeLibrary", ImVec2(0, 0), false);
          library) {
        RETURN_IF_ERROR(RenderLibrary(*draft));
      }
      draft = model_.draft();
      if (draft == nullptr) return absl::OkStatus();
      gui_->TableNextColumn();
      if (ScopedChild preview = gui_->CreateScopedChild("ThemeViewport", ImVec2(0, 0), false);
          preview) {
        RETURN_IF_ERROR(RenderViewport(*draft, levels));
      }
      gui_->TableNextColumn();
      if (ScopedChild inspector = gui_->CreateScopedChild("ThemeInspector", ImVec2(0, 0), false);
          inspector) {
        RETURN_IF_ERROR(RenderInspector(*draft, textures));
      }
    }
  }

  if (show_diagnostics_) {
    if (ScopedChild diagnostics =
            gui_->CreateScopedChild("ThemeDiagnostics", ImVec2(0, diagnostics_height), true);
        diagnostics) {
      RETURN_IF_ERROR(RenderDiagnostics(*draft, levels));
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes
