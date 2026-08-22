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

struct ThemeUsage {
  std::string level_id;
  std::string level_name;
  int zone_id = -1;
  std::string zone_name;
  Vec route_min;
  Vec route_max;
};

std::vector<ThemeUsage> FindThemeUsages(const std::vector<Level>& levels,
                                        const std::string& theme_id) {
  std::vector<ThemeUsage> usages;
  if (theme_id.empty()) return usages;
  for (const Level& level : levels) {
    for (const ParallaxZone& zone : level.zones) {
      if (zone.theme_id != theme_id) continue;
      usages.push_back({
          .level_id = level.id,
          .level_name = level.name,
          .zone_id = zone.id,
          .zone_name = zone.name,
          .route_min = zone.min_point,
          .route_max = zone.max_point,
      });
    }
  }
  return usages;
}

std::string UsageLabel(const ThemeUsage& usage) {
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
      preview_canvas_({.gui = gui}),
      viewport_renderer_(preview_canvas_),
      manual_route_max_(api->GetConfig()->game_view.width, api->GetConfig()->game_view.height) {}

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

absl::Status ParallaxThemeEditor::RenderTexturePicker(ParallaxLayer& layer,
                                                      const std::vector<Texture>& textures) {
  gui_->InputText("Search Textures", &texture_search_);
  ScopedChild catalog = gui_->CreateScopedChild("TextureCatalog", ImVec2(0, 170), true);
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
    const bool selected = texture.id == layer.texture_id;
    const std::string label = texture.name_id();
    if (gui_->Selectable(label.c_str(), selected, 0, ImVec2(0, 40))) {
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
  diagnostics_ = DiagnosticsSnapshot{
      .texture_id = layer.texture_id,
      .repetition = repetition,
  };
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderInspector(ParallaxTheme& draft,
                                                  const std::vector<Texture>& textures) {
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
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f},
                      "Warning: background scroll is normally within [0, 1].");
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

absl::Status ParallaxThemeEditor::RenderPreview(const ParallaxTheme& draft,
                                                const std::vector<Level>& levels) {
  const std::vector<ThemeUsage> usages = FindThemeUsages(levels, draft.id);
  if (usages.empty()) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "Theme is not assigned to a level zone.");
    context_level_id_.clear();
    context_zone_id_.reset();
  } else if (!manual_context_) {
    const bool current_context_exists =
        std::any_of(usages.begin(), usages.end(), [this](const ThemeUsage& usage) {
          return usage.level_id == context_level_id_ && usage.zone_id == context_zone_id_;
        });
    if (!current_context_exists) {
      context_level_id_ = usages.front().level_id;
      context_zone_id_ = usages.front().zone_id;
    }
  }

  const ThemeUsage* context = nullptr;
  for (const ThemeUsage& usage : usages) {
    if (manual_context_) break;
    if (usage.level_id == context_level_id_ && usage.zone_id == context_zone_id_) {
      context = &usage;
      break;
    }
  }

  if (!usages.empty()) {
    const std::string preview = context == nullptr ? "Manual route" : UsageLabel(*context);
    if (ScopedCombo combo = gui_->CreateScopedCombo("Level / Zone Context", preview.c_str());
        combo) {
      if (gui_->Selectable("Manual route", manual_context_)) {
        manual_context_ = true;
        context_level_id_.clear();
        context_zone_id_.reset();
      }
      for (const ThemeUsage& usage : usages) {
        const std::string label = UsageLabel(usage);
        const bool selected = &usage == context;
        if (gui_->Selectable(label.c_str(), selected)) {
          manual_context_ = false;
          context_level_id_ = usage.level_id;
          context_zone_id_ = usage.zone_id;
        }
      }
    }
    gui_->Text("Referenced by %d level zone(s).", static_cast<int>(usages.size()));
  }

  Vec route_min = manual_route_min_;
  Vec route_max = manual_route_max_;
  if (context != nullptr) {
    route_min = context->route_min;
    route_max = context->route_max;
  } else {
    gui_->Text("Manual camera route");
    gui_->InputDouble("Start X", &manual_route_min_.x);
    gui_->InputDouble("Start Y", &manual_route_min_.y);
    gui_->InputDouble("End X", &manual_route_max_.x);
    gui_->InputDouble("End Y", &manual_route_max_.y);
    route_min = manual_route_min_;
    route_max = manual_route_max_;
  }

  gui_->Checkbox("Selected Layer Only", &preview_selected_layer_);
  gui_->SliderFloat("Travel X", &travel_x_, 0.0f, 1.0f);
  gui_->SliderFloat("Travel Y", &travel_y_, 0.0f, 1.0f);
  gui_->SliderFloat("Preview Zoom", &preview_zoom_, static_cast<float>(kAuthoringZoomRange.minimum),
                    static_cast<float>(kAuthoringZoomRange.maximum));

  preview_camera_.position = {
      route_min.x + (route_max.x - route_min.x) * travel_x_,
      route_min.y + (route_max.y - route_min.y) * travel_y_,
  };
  preview_camera_.zoom = preview_zoom_;

  std::map<std::string, TextureHandle> handles;
  for (const ParallaxLayer& layer : draft.layers) {
    if (layer.texture_id.empty() || handles.contains(layer.texture_id)) continue;
    ASSIGN_OR_RETURN(handles[layer.texture_id], api_->GetTextureHandle(layer.texture_id));
  }

  const float preview_width = std::max(0.0f, gui_->GetContentRegionAvail().x);
  preview_canvas_.Begin("ThemePreviewCanvas", {preview_width, 300.0f}, preview_camera_);
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
  RETURN_IF_ERROR(render_status);

  if (!model_.selected_layer()) return absl::OkStatus();
  const ParallaxLayer& layer = draft.layers[*model_.selected_layer()];
  if (layer.texture_id.empty()) return absl::OkStatus();

  ASSIGN_OR_RETURN(const TextureHandle selected_handle, api_->GetTextureHandle(layer.texture_id));
  ASSIGN_OR_RETURN(const AtlasBinding binding, texture_preview_.BindAtlas(selected_handle));
  if (!binding.IsValid()) return absl::OkStatus();

  gui_->Separator();
  gui_->Text("Selected texture: %dx%d px", binding.width, binding.height);
  if (gui_->Button("Analyze Repetition")) SetError(AnalyzeSelectedTexture());
  if (diagnostics_ && diagnostics_->texture_id == layer.texture_id) {
    const OpposingEdgeDifference& horizontal = diagnostics_->repetition.horizontal;
    const OpposingEdgeDifference& vertical = diagnostics_->repetition.vertical;
    gui_->TextWrapped(
        "Left/right edges: %.2f mean RGBA delta, %d maximum; %d/%d pixels match "
        "exactly.",
        horizontal.mean_absolute_channel_difference, horizontal.maximum_channel_difference,
        horizontal.exact_pixel_matches, horizontal.pixels_compared);
    gui_->TextWrapped(
        "Top/bottom edges: %.2f mean RGBA delta, %d maximum; %d/%d pixels match "
        "exactly.",
        vertical.mean_absolute_channel_difference, vertical.maximum_channel_difference,
        vertical.exact_pixel_matches, vertical.pixels_compared);
    gui_->TextDisabled(
        "These measurements aid visual seam review; they do not prove seamlessness.");
  }

  if (route_min.x <= route_max.x && route_min.y <= route_max.y) {
    ASSIGN_OR_RETURN(
        const CameraCoverageDiagnostics coverage,
        AnalyzeCameraCoverage(layer, binding.width, binding.height, route_min, route_max,
                              api_->GetConfig()->game_view, kAuthoringZoomRange));
    if (coverage.horizontal.repeated) {
      gui_->Text("Horizontal coverage: repeated");
    } else {
      gui_->Text("Horizontal margins: start %.1f, end %.1f world units",
                 coverage.horizontal.minimum_start_margin, coverage.horizontal.minimum_end_margin);
    }
    if (coverage.vertical.repeated) {
      gui_->Text("Vertical coverage: repeated");
    } else {
      gui_->Text("Vertical margins: start %.1f, end %.1f world units",
                 coverage.vertical.minimum_start_margin, coverage.vertical.minimum_end_margin);
    }
    if (!coverage.horizontal.covers() || !coverage.vertical.covers()) {
      gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f},
                        "Warning: a negative margin is an uncovered camera area.");
    }
  } else {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Camera route start must not exceed its end.");
  }

  if (layer.repeat_x) {
    gui_->Text("Wrapped X preview");
    const float width = std::min(100.0f, gui_->GetContentRegionAvail().x / 3.0f);
    if (width > 0.0f) {
      ASSIGN_OR_RETURN(const TexturePreviewLayout layout,
                       CalculateTexturePreviewLayout(binding.width, binding.height, width, 100.0f));
      for (int index = 0; index < 3; ++index) {
        if (index > 0) gui_->SameLine();
        gui_->Image(binding.texture_id, {layout.display_width, layout.display_height});
      }
    }
  }
  if (layer.repeat_y) {
    gui_->Text("Wrapped Y preview");
    ASSIGN_OR_RETURN(const TexturePreviewLayout layout,
                     CalculateTexturePreviewLayout(binding.width, binding.height, 180.0f, 60.0f));
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

  ScopedTable table = gui_->CreateScopedTable("ParallaxThemeEditor", 4,
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders);
  if (!table) return absl::OkStatus();
  gui_->TableSetupColumn("Themes");
  gui_->TableSetupColumn("Layers");
  gui_->TableSetupColumn("Inspector");
  gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  gui_->TableNextRow();
  gui_->TableNextColumn();

  if (gui_->Button("New Theme")) {
    model_.BeginNew();
    diagnostics_.reset();
    context_level_id_.clear();
    context_zone_id_.reset();
    manual_context_ = true;
  }
  for (const ParallaxTheme& theme : api_->GetAllParallaxThemes()) {
    const bool selected = model_.draft() != nullptr && model_.draft()->id == theme.id;
    const std::string label = absl::StrCat(theme.name, " (", theme.id, ")");
    if (gui_->Selectable(label.c_str(), selected)) RETURN_IF_ERROR(OpenTheme(theme.id));
  }

  gui_->TableNextColumn();
  ParallaxTheme* draft = model_.draft();
  if (draft == nullptr) {
    gui_->TextDisabled("Select or create a theme.");
    return absl::OkStatus();
  }
  gui_->InputText("Theme Name", &draft->name);
  if (gui_->Button("Add Layer")) RETURN_IF_ERROR(model_.AddLayer());
  for (int index = 0; index < static_cast<int>(draft->layers.size()); ++index) {
    ScopedId id = gui_->CreateScopedId(index);
    if (gui_->Selectable(draft->layers[index].name.c_str(), model_.selected_layer() == index)) {
      model_.SelectLayer(index);
      diagnostics_.reset();
    }
  }

  ASSIGN_OR_RETURN(const std::vector<Texture> textures, api_->GetAllTextures());
  gui_->TableNextColumn();
  RETURN_IF_ERROR(RenderInspector(*draft, textures));

  gui_->Separator();
  if (gui_->Button("Save Theme")) {
    const absl::Status status = Save();
    if (!status.ok()) SetError(status);
  }
  gui_->SameLine();
  if (gui_->Button("Duplicate")) {
    const absl::Status status = Duplicate();
    if (!status.ok()) SetError(status);
  }
  draft = model_.draft();
  if (draft == nullptr) return absl::OkStatus();
  if (!draft->id.empty() &&
      delete_prompt_.Render(*gui_, "Delete Theme", draft->id,
                            absl::StrCat("Delete '", draft->name, "'?"), "ParallaxTheme")) {
    const absl::Status status = api_->DeleteParallaxTheme(draft->id);
    if (status.ok()) {
      model_.Close();
      diagnostics_.reset();
      return absl::OkStatus();
    } else {
      SetError(status);
    }
  }

  gui_->TableNextColumn();
  RETURN_IF_ERROR(RenderPreview(*draft, api_->GetAllLevels()));
  return absl::OkStatus();
}

}  // namespace zebes
