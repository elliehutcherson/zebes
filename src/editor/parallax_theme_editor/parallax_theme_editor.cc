#include "editor/parallax_theme_editor/parallax_theme_editor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/inspector_ui.h"
#include "editor/level_editor/parallax_layout.h"
#include "editor/level_editor/viewport_scene.h"
#include "imgui.h"

namespace zebes {
namespace {

constexpr CameraZoomRange kAuthoringZoomRange{.minimum = 0.5, .maximum = 2.0};
constexpr float kNavigatorWidth = 280.0f;
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

ParallaxElement* FindElement(ParallaxLayer& layer, std::optional<int> element_id) {
  if (!element_id.has_value()) return nullptr;
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [element_id](const ParallaxElement& element) { return element.id == *element_id; });
  return found == layer.elements.end() ? nullptr : &*found;
}

const ParallaxElement* FindElement(const ParallaxLayer& layer, std::optional<int> element_id) {
  if (!element_id.has_value()) return nullptr;
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [element_id](const ParallaxElement& element) { return element.id == *element_id; });
  return found == layer.elements.end() ? nullptr : &*found;
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
                        api->GetConfig()->game_view.height / 2.0) {
  const CameraCenterRoute route = EnsureNavigableManualCameraRoute(
      {.min = manual_route_min_, .max = manual_route_min_}, api->GetConfig()->game_view);
  manual_route_max_ = route.max;
}

absl::Status ParallaxThemeEditor::OpenTheme(const std::string& theme_id) {
  ASSIGN_OR_RETURN(ParallaxTheme * theme, api_->GetParallaxTheme(theme_id));
  if (theme == nullptr) return absl::FailedPreconditionError("Theme lookup returned null.");
  model_.Open(*theme);
  close_prompt_.Disarm();
  delete_prompt_.Disarm();
  element_drag_.Reset();
  dragged_element_id_.reset();
  diagnostics_.reset();
  context_level_id_.clear();
  context_zone_id_.reset();
  const std::vector<ParallaxThemeUsage> usages =
      FindParallaxThemeUsages(api_->GetAllLevels(), theme->id);
  manual_context_ = usages.empty();
  if (manual_context_) {
    const Vec center{api_->GetConfig()->game_view.width / 2.0,
                     api_->GetConfig()->game_view.height / 2.0};
    const CameraCenterRoute route = EnsureNavigableManualCameraRoute({.min = center, .max = center},
                                                                     api_->GetConfig()->game_view);
    manual_route_min_ = route.min;
    manual_route_max_ = route.max;
  }
  error_.reset();
  return absl::OkStatus();
}

void ParallaxThemeEditor::CloseTheme() {
  model_.Close();
  close_prompt_.Disarm();
  delete_prompt_.Disarm();
  element_drag_.Reset();
  dragged_element_id_.reset();
  diagnostics_.reset();
  context_level_id_.clear();
  context_zone_id_.reset();
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
  close_prompt_.Disarm();
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::Duplicate() {
  const ParallaxTheme* draft = model_.draft();
  if (draft == nullptr || draft->id.empty()) {
    return absl::FailedPreconditionError("Save the theme before duplicating it.");
  }
  CameraCenterRoute duplicate_route{.min = manual_route_min_, .max = manual_route_max_};
  if (!manual_context_) {
    const std::vector<ParallaxThemeUsage> usages =
        FindParallaxThemeUsages(api_->GetAllLevels(), draft->id);
    const auto selected =
        std::find_if(usages.begin(), usages.end(), [this](const ParallaxThemeUsage& usage) {
          return usage.level_id == context_level_id_ && usage.zone_id == context_zone_id_;
        });
    if (selected != usages.end()) {
      duplicate_route = selected->route;
    } else if (!usages.empty()) {
      duplicate_route = usages.front().route;
    }
  }
  duplicate_route = EnsureNavigableManualCameraRoute(duplicate_route, api_->GetConfig()->game_view);

  ParallaxTheme duplicate = *draft;
  duplicate.id.clear();
  duplicate.name = absl::StrCat(duplicate.name, " Copy");
  ASSIGN_OR_RETURN(const std::string id, api_->CreateParallaxTheme(std::move(duplicate)));
  RETURN_IF_ERROR(OpenTheme(id));
  manual_context_ = true;
  manual_route_min_ = duplicate_route.min;
  manual_route_max_ = duplicate_route.max;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderToolbar(ParallaxTheme& draft, bool& save_requested) {
  const std::string close_target = draft.id.empty() ? "new-theme" : draft.id;
  const std::string close_question =
      absl::StrCat("Discard unsaved changes to '", draft.name, "' and return to the theme list?");
  if (close_prompt_.armed()) {
    if (close_prompt_.Render(*gui_, "Back", close_target, close_question, "ParallaxThemeBack")) {
      CloseTheme();
      return absl::OkStatus();
    }
  } else if (gui_->Button("Back")) {
    if (model_.dirty()) {
      close_prompt_.Arm(close_target);
    } else {
      CloseTheme();
      return absl::OkStatus();
    }
  }
  if (!model_.has_draft()) {
    return absl::OkStatus();
  }
  gui_->SameLine();
  gui_->SetNextItemWidth(260.0f);
  gui_->InputText("Theme Name", &draft.name);
  gui_->SameLine();
  if (gui_->Button("Save")) save_requested = true;
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
      CloseTheme();
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

absl::Status ParallaxThemeEditor::RenderLayerNavigator(ParallaxTheme& draft) {
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

absl::Status ParallaxThemeEditor::RenderTexturePicker(ParallaxElement& element,
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
    if (gui_->Selectable(texture.name_id().c_str(), texture.id == element.texture_id, 0,
                         ImVec2(0, 40))) {
      element.texture_id = texture.id;
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
  const ParallaxElement* element = FindElement(layer, model_.selected_element_id());
  if (element == nullptr || element->texture_id.empty()) {
    return absl::FailedPreconditionError("Select a texture before analyzing it.");
  }
  ASSIGN_OR_RETURN(const RgbaImage pixels, api_->ReadTexturePixels(element->texture_id));
  ASSIGN_OR_RETURN(const RepetitionDiagnostics repetition, AnalyzeRepetition(pixels));
  diagnostics_ = DiagnosticsSnapshot{.texture_id = element->texture_id, .repetition = repetition};
  return absl::OkStatus();
}

absl::StatusOr<std::vector<ParallaxElementSize>> ParallaxThemeEditor::ResolveElementSizes(
    const ParallaxLayer& layer) {
  if (layer.elements.empty()) {
    return absl::FailedPreconditionError("Add an element before measuring the composition.");
  }
  std::vector<ParallaxElementSize> sizes;
  sizes.reserve(layer.elements.size());
  for (const ParallaxElement& element : layer.elements) {
    if (element.texture_id.empty()) {
      return absl::FailedPreconditionError(
          absl::StrCat("Element '", element.name, "' needs a texture first."));
    }
    ASSIGN_OR_RETURN(const TextureHandle handle, api_->GetTextureHandle(element.texture_id));
    ASSIGN_OR_RETURN(const AtlasBinding binding, texture_preview_.BindAtlas(handle));
    if (!binding.IsValid()) {
      return absl::FailedPreconditionError(
          absl::StrCat("Element '", element.name, "' texture is unavailable."));
    }
    sizes.push_back({.element_id = element.id, .width = binding.width, .height = binding.height});
  }
  return sizes;
}

absl::Status ParallaxThemeEditor::SetRepeatMode(ParallaxLayer& layer, bool repeat_x,
                                                bool repeat_y) {
  if (!repeat_x && !repeat_y) {
    layer.repeat_period = {0, 0};
    return absl::OkStatus();
  }
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes, ResolveElementSizes(layer));
  ASSIGN_OR_RETURN(const WorldRect bounds, CalculateParallaxCompositionBounds(layer, sizes));
  layer.repeat_period = {
      repeat_x ? bounds.max.x - bounds.min.x : 0.0,
      repeat_y ? bounds.max.y - bounds.min.y : 0.0,
  };
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::FitRepeatPeriodToContent(ParallaxLayer& layer) {
  if (layer.repeat_period.x == 0.0 && layer.repeat_period.y == 0.0) {
    return absl::FailedPreconditionError("Choose a repeating mode before fitting its period.");
  }
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes, ResolveElementSizes(layer));
  ASSIGN_OR_RETURN(const WorldRect bounds, CalculateParallaxCompositionBounds(layer, sizes));
  if (layer.repeat_period.x > 0.0) layer.repeat_period.x = bounds.max.x - bounds.min.x;
  if (layer.repeat_period.y > 0.0) layer.repeat_period.y = bounds.max.y - bounds.min.y;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::AppendElementRight(ParallaxLayer& layer) {
  if (layer.elements.empty()) return model_.AddElementAt({0, 0});
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes, ResolveElementSizes(layer));
  ASSIGN_OR_RETURN(const WorldRect bounds, CalculateParallaxCompositionBounds(layer, sizes));
  return model_.AddElementAt({bounds.max.x, bounds.min.y});
}

absl::Status ParallaxThemeEditor::SnapSelectedElement(ParallaxLayer& layer, int direction) {
  if (direction != -1 && direction != 1) {
    return absl::InvalidArgumentError("Element snap direction must be adjacent.");
  }
  const std::optional<int> selected_id = model_.selected_element_id();
  if (!selected_id.has_value()) {
    return absl::FailedPreconditionError("No parallax element is selected.");
  }
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [selected_id](const ParallaxElement& element) { return element.id == *selected_id; });
  if (found == layer.elements.end()) {
    return absl::FailedPreconditionError("Selected parallax element no longer exists.");
  }
  const int index = static_cast<int>(found - layer.elements.begin());
  const int neighbor_index = index + direction;
  if (neighbor_index < 0 || neighbor_index >= static_cast<int>(layer.elements.size())) {
    return absl::OutOfRangeError("Selected element has no neighbor in that direction.");
  }
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes, ResolveElementSizes(layer));
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementBounds> bounds,
                   CalculateParallaxElementBounds(layer, sizes));
  if (direction < 0) {
    found->position.x += bounds[neighbor_index].bounds.max.x - bounds[index].bounds.min.x;
  } else {
    found->position.x += bounds[neighbor_index].bounds.min.x - bounds[index].bounds.max.x;
  }
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
  gui_->Spacing();
  gui_->Text("COMPOSITION REPEAT");
  gui_->TextDisabled("Zero keeps that axis finite; positive values repeat the whole layer.");
  InputCommittedDouble(*gui_, "Period X (px)", layer.repeat_period.x);
  InputCommittedDouble(*gui_, "Period Y (px)", layer.repeat_period.y);
  if (layer.repeat_period.x < 0.0 || layer.repeat_period.y < 0.0) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Repeat periods cannot be negative.");
  }
  if (gui_->Button("Finite")) SetError(SetRepeatMode(layer, false, false));
  gui_->SameLine();
  if (gui_->Button("Repeat X")) SetError(SetRepeatMode(layer, true, false));
  gui_->SameLine();
  if (gui_->Button("Repeat Y")) SetError(SetRepeatMode(layer, false, true));
  gui_->SameLine();
  if (gui_->Button("Repeat X/Y")) SetError(SetRepeatMode(layer, true, true));
  if (gui_->Button("Fit Period to Content")) SetError(FitRepeatPeriodToContent(layer));
  if (gui_->Button("Move Farther")) {
    SetError(model_.MoveSelectedLayer(-1));
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Move Nearer")) {
    SetError(model_.MoveSelectedLayer(1));
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Delete Layer")) {
    RETURN_IF_ERROR(model_.DeleteSelectedLayer());
    return absl::OkStatus();
  }

  gui_->Spacing();
  gui_->Separator();
  gui_->Text("ELEMENTS (BACK TO FRONT)");
  gui_->SameLine();
  if (gui_->Button("Add Element")) RETURN_IF_ERROR(model_.AddElement());
  gui_->SameLine();
  if (gui_->Button("Append Right")) SetError(AppendElementRight(layer));
  for (const ParallaxElement& item : layer.elements) {
    ScopedId id = gui_->CreateScopedId(item.id);
    if (gui_->Selectable(item.name.c_str(), model_.selected_element_id() == item.id)) {
      model_.SelectElement(item.id);
      diagnostics_.reset();
    }
  }

  ParallaxElement* element = FindElement(layer, model_.selected_element_id());
  if (element == nullptr) {
    gui_->TextDisabled("Add or select an element to edit its artwork.");
    return absl::OkStatus();
  }
  gui_->InputText("Element Name", &element->name);
  RETURN_IF_ERROR(RenderTexturePicker(*element, textures));
  gui_->InputDouble("Position X (px)", &element->position.x);
  gui_->InputDouble("Position Y (px)", &element->position.y);
  gui_->InputFloat("Element Scale", &element->scale);
  if (gui_->Button("Duplicate Element")) {
    RETURN_IF_ERROR(model_.DuplicateSelectedElement());
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Snap After Previous")) SetError(SnapSelectedElement(layer, -1));
  gui_->SameLine();
  if (gui_->Button("Snap Before Next")) SetError(SnapSelectedElement(layer, 1));
  if (gui_->Button("Move Element Back")) {
    SetError(model_.MoveSelectedElement(-1));
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Move Element Front")) {
    SetError(model_.MoveSelectedElement(1));
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Delete Element")) {
    RETURN_IF_ERROR(model_.DeleteSelectedElement());
    return absl::OkStatus();
  }
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

absl::Status ParallaxThemeEditor::UpdatePreviewElementDrag(
    ParallaxLayer& editable_layer, const ParallaxLayer& preview_layer,
    const std::vector<ParallaxElementSize>& element_sizes) {
  ASSIGN_OR_RETURN(const ParallaxLayout layout,
                   CalculateParallaxLayout(preview_camera_, preview_layer, element_sizes));
  const Vec pointer = preview_canvas_.ScreenToWorld(gui_->GetMousePos());
  if (!element_drag_.active() && gui_->IsItemClicked(ImGuiMouseButton_Left)) {
    for (auto item = layout.elements.rbegin(); item != layout.elements.rend(); ++item) {
      if (pointer.x < item->bounds.min.x || pointer.x >= item->bounds.max.x ||
          pointer.y < item->bounds.min.y || pointer.y >= item->bounds.max.y) {
        continue;
      }
      model_.SelectElement(item->element_id);
      diagnostics_.reset();
      dragged_element_id_ = item->element_id;
      element_drag_.Begin(pointer, item->bounds.min);
      dragged_repeat_offset_ = {
          item->repeat_column * preview_layer.repeat_period.x,
          item->repeat_row * preview_layer.repeat_period.y,
      };
      break;
    }
  }

  const bool was_active = element_drag_.active();
  const std::optional<Vec> requested = element_drag_.Update(pointer, gui_->IsItemActive());
  if (!requested.has_value()) {
    if (was_active && !element_drag_.active()) dragged_element_id_.reset();
    return absl::OkStatus();
  }
  if (!dragged_element_id_.has_value()) {
    element_drag_.Reset();
    return absl::InternalError("Active parallax element drag has no element ID.");
  }
  ParallaxElement* element = FindElement(editable_layer, dragged_element_id_);
  if (element == nullptr) {
    element_drag_.Reset();
    dragged_element_id_.reset();
    return absl::FailedPreconditionError("Dragged parallax element no longer exists.");
  }
  element->position = {
      requested->x - layout.origin.x - dragged_repeat_offset_.x,
      requested->y - layout.origin.y - dragged_repeat_offset_.y,
  };
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::RenderViewport(ParallaxTheme& draft,
                                                 const std::vector<Level>& levels) {
  gui_->Text("GAME VIEW PREVIEW");
  gui_->SameLine();
  gui_->TextDisabled("%dx%d logical px", api_->GetConfig()->game_view.width,
                     api_->GetConfig()->game_view.height);
  gui_->Separator();

  const PreviewContext context = RenderContextPicker(draft, levels);
  gui_->Text("Preview scope");
  gui_->SameLine();
  if (gui_->Button("Complete Theme")) preview_scope_ = ParallaxPreviewScope::kCompleteTheme;
  gui_->SameLine();
  if (gui_->Button("Selected Layer")) preview_scope_ = ParallaxPreviewScope::kSelectedLayer;
  gui_->SameLine();
  if (gui_->Button("Selected Element")) preview_scope_ = ParallaxPreviewScope::kSelectedElement;
  gui_->SameLine();
  switch (preview_scope_) {
    case ParallaxPreviewScope::kCompleteTheme:
      gui_->TextDisabled("Complete Theme");
      break;
    case ParallaxPreviewScope::kSelectedLayer:
      gui_->TextDisabled("Selected Layer");
      break;
    case ParallaxPreviewScope::kSelectedElement:
      gui_->TextDisabled("Selected Element");
      break;
  }
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
  CameraCenterRoute resolved_route = *resolved;
  preview_camera_.position = InterpolateCameraCenter(resolved_route, travel_x_, travel_y_);
  preview_camera_.zoom = preview_zoom_;
  gui_->TextDisabled("Camera center: %.1f, %.1f", preview_camera_.position.x,
                     preview_camera_.position.y);
  gui_->SameLine();
  Vec camera_nudge;
  if (gui_->ArrowButton("##CameraLeft", ImGuiDir_Left)) camera_nudge.x -= 32.0;
  gui_->SameLine();
  if (gui_->ArrowButton("##CameraUp", ImGuiDir_Up)) camera_nudge.y -= 32.0;
  gui_->SameLine();
  if (gui_->ArrowButton("##CameraDown", ImGuiDir_Down)) camera_nudge.y += 32.0;
  gui_->SameLine();
  if (gui_->ArrowButton("##CameraRight", ImGuiDir_Right)) camera_nudge.x += 32.0;
  gui_->SameLine();
  gui_->TextDisabled("32 px nudge");
  if (camera_nudge != Vec()) {
    ASSIGN_OR_RETURN(
        const Vec nudged_travel,
        CalculateCameraTravel(resolved_route, {preview_camera_.position.x + camera_nudge.x,
                                               preview_camera_.position.y + camera_nudge.y}));
    travel_x_ = static_cast<float>(nudged_travel.x);
    travel_y_ = static_cast<float>(nudged_travel.y);
    preview_camera_.position = InterpolateCameraCenter(resolved_route, travel_x_, travel_y_);
  }

  const ParallaxPreviewTheme preview = BuildParallaxPreviewTheme(draft);
  std::map<std::string, TextureHandle> handles;
  for (const ParallaxLayer& layer : preview.theme.layers) {
    for (const ParallaxElement& element : layer.elements) {
      if (handles.contains(element.texture_id)) continue;
      ASSIGN_OR_RETURN(handles[element.texture_id], api_->GetTextureHandle(element.texture_id));
    }
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
  auto canvas_end = absl::MakeCleanup([this] { preview_canvas_.End(); });
  preview_canvas_.HandleInput();

  const float navigated_zoom = static_cast<float>(kAuthoringZoomRange.Clamp(preview_camera_.zoom));
  const absl::StatusOr<CameraCenterRoute> navigated_route = ResolveCameraCenterRoute(
      context.route, api_->GetConfig()->game_view, navigated_zoom, context.world);
  if (navigated_route.ok()) {
    resolved_route = *navigated_route;
    preview_zoom_ = navigated_zoom;
  } else {
    preview_camera_.zoom = preview_zoom_;
  }
  ASSIGN_OR_RETURN(const Vec navigated_travel,
                   CalculateCameraTravel(resolved_route, preview_camera_.position));
  travel_x_ = static_cast<float>(navigated_travel.x);
  travel_y_ = static_cast<float>(navigated_travel.y);
  preview_camera_.position = InterpolateCameraCenter(resolved_route, travel_x_, travel_y_);
  preview_camera_.zoom = preview_zoom_;

  ParallaxRenderOptions options;
  bool selected_scope_available = true;
  std::optional<int> preview_layer_index;
  if (preview_scope_ != ParallaxPreviewScope::kCompleteTheme) {
    if (model_.selected_layer().has_value()) {
      preview_layer_index = FindPreviewLayerIndex(preview, *model_.selected_layer());
    }
    options.layer_index = preview_layer_index;
    if (!options.layer_index.has_value()) selected_scope_available = false;
  }
  if (preview_scope_ == ParallaxPreviewScope::kSelectedElement) {
    options.element_id = model_.selected_element_id();
    if (!options.element_id.has_value() || !preview_layer_index.has_value()) {
      selected_scope_available = false;
    } else {
      const ParallaxLayer& layer = preview.theme.layers[*preview_layer_index];
      if (FindElement(layer, options.element_id) == nullptr) selected_scope_available = false;
    }
  }
  absl::Status render_status;
  std::optional<std::string> recoverable_preview_error;
  if (selected_scope_available && !preview.theme.layers.empty()) {
    absl::StatusOr<ParallaxRenderBatch> batch =
        ComposeParallaxRenderBatch(preview.theme, preview_camera_, handles, options);
    if (!batch.ok()) {
      if (IsRecoverableParallaxPreviewError(batch.status())) {
        recoverable_preview_error = std::string(batch.status().message());
      } else {
        render_status = batch.status();
      }
    } else {
      render_status = viewport_renderer_.RenderParallax(*batch);
    }
  }

  if (model_.selected_layer().has_value() && !recoverable_preview_error.has_value()) {
    ParallaxLayer& editable_layer = draft.layers[*model_.selected_layer()];
    const std::optional<int> selected_preview_index =
        FindPreviewLayerIndex(preview, *model_.selected_layer());
    if (selected_preview_index.has_value()) {
      const ParallaxLayer& selected_layer = preview.theme.layers[*selected_preview_index];
      ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes,
                       ResolveElementSizes(selected_layer));
      ASSIGN_OR_RETURN(const ParallaxLayout selected_layout,
                       CalculateParallaxLayout(preview_camera_, selected_layer, sizes));
      if (ImDrawList* draw_list = preview_canvas_.GetDrawList(); draw_list != nullptr) {
        const VisibleWorldBounds visible = CalculateVisibleWorldBounds(preview_camera_);
        auto draw_axis_guides = [&](double period, double origin, bool horizontal) {
          if (period <= 0.0) return;
          const double visible_min = horizontal ? visible.min.y : visible.min.x;
          const double visible_max = horizontal ? visible.max.y : visible.max.x;
          const double axis_min = horizontal ? visible.min.x : visible.min.y;
          const double axis_max = horizontal ? visible.max.x : visible.max.y;
          const int first = static_cast<int>(std::floor((axis_min - origin) / period));
          const int last = static_cast<int>(std::ceil((axis_max - origin) / period));
          if (last - first > 100) return;
          for (int cell = first; cell <= last; ++cell) {
            const double coordinate = origin + cell * period;
            const Vec start =
                horizontal ? Vec{coordinate, visible_min} : Vec{visible_min, coordinate};
            const Vec end =
                horizontal ? Vec{coordinate, visible_max} : Vec{visible_max, coordinate};
            draw_list->AddLine(preview_canvas_.WorldToScreen(start),
                               preview_canvas_.WorldToScreen(end), IM_COL32(255, 180, 40, 180),
                               1.0f);
          }
        };
        draw_axis_guides(selected_layer.repeat_period.x, selected_layout.origin.x, true);
        draw_axis_guides(selected_layer.repeat_period.y, selected_layout.origin.y, false);
        if (model_.selected_element_id().has_value()) {
          for (const ParallaxElementLayout& item : selected_layout.elements) {
            if (item.element_id != *model_.selected_element_id()) continue;
            draw_list->AddRect(preview_canvas_.WorldToScreen(item.bounds.min),
                               preview_canvas_.WorldToScreen(item.bounds.max),
                               IM_COL32(80, 220, 255, 230), 0.0f, 0, 2.0f);
          }
        }
      }
      RETURN_IF_ERROR(UpdatePreviewElementDrag(editable_layer, selected_layer, sizes));
    } else {
      element_drag_.Reset();
      dragged_element_id_.reset();
    }
  } else {
    element_drag_.Reset();
    dragged_element_id_.reset();
  }
  std::move(canvas_end).Invoke();
  if (recoverable_preview_error.has_value()) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "PREVIEW PAUSED");
    gui_->TextWrapped(
        "%s. No instances were drawn. Finish editing or increase the repeat period; the "
        "inspector remains available.",
        recoverable_preview_error->c_str());
  }
  if (preview.omitted_elements > 0) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f},
                      "%zu incomplete element(s) are hidden from preview; complete them before "
                      "saving.",
                      preview.omitted_elements);
  }
  if (!selected_scope_available) {
    gui_->TextDisabled("The selected draft item needs valid artwork before it can be isolated.");
  } else if (preview.theme.layers.empty()) {
    gui_->TextDisabled("Assign a texture to an element to begin previewing the theme.");
  } else {
    gui_->TextDisabled(
        "Left-drag an element in the selected layer. Arrow keys/WASD or middle-drag move the "
        "camera; the scrub bars stay synchronized.");
  }
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
  const ParallaxElement* element = FindElement(layer, model_.selected_element_id());
  if (element == nullptr || element->texture_id.empty()) {
    gui_->TextDisabled("Select a texture to inspect coverage.");
    gui_->TableNextColumn();
    gui_->TextDisabled("Select a texture to inspect repetition.");
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(const TextureHandle handle, api_->GetTextureHandle(element->texture_id));
  ASSIGN_OR_RETURN(const AtlasBinding binding, texture_preview_.BindAtlas(handle));
  if (!binding.IsValid()) return absl::OkStatus();
  gui_->Text("Selected texture: %dx%d px", binding.width, binding.height);
  const absl::StatusOr<std::vector<ParallaxElementSize>> resolved_sizes =
      ResolveElementSizes(layer);
  if (!resolved_sizes.ok()) {
    gui_->TextDisabled("Every element needs a texture before coverage can be measured.");
    gui_->TableNextColumn();
    gui_->TextDisabled("Complete the composition to inspect repetition.");
    return absl::OkStatus();
  }
  const std::vector<ParallaxElementSize>& element_sizes = *resolved_sizes;
  ASSIGN_OR_RETURN(const WorldRect composition_bounds,
                   CalculateParallaxCompositionBounds(layer, element_sizes));
  const absl::StatusOr<CameraCoverageDiagnostics> coverage =
      AnalyzeCameraCoverage(layer, composition_bounds, context.route.min, context.route.max,
                            api_->GetConfig()->game_view, kAuthoringZoomRange, context.world);
  if (!coverage.ok()) {
    const std::string message(coverage.status().message());
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", message.c_str());
  } else {
    if (coverage->horizontal.repeated) {
      gui_->Text("Horizontal: repeated every %.1f px", coverage->horizontal.repeat_period);
      gui_->Text("Bounds span %.1f px; period - span %.1f px",
                 coverage->horizontal.composition_span, coverage->horizontal.period_minus_span);
    } else {
      gui_->Text("Horizontal margins: %.1f / %.1f", coverage->horizontal.minimum_start_margin,
                 coverage->horizontal.minimum_end_margin);
    }
    if (coverage->vertical.repeated) {
      gui_->Text("Vertical: repeated every %.1f px", coverage->vertical.repeat_period);
      gui_->Text("Bounds span %.1f px; period - span %.1f px", coverage->vertical.composition_span,
                 coverage->vertical.period_minus_span);
    } else {
      gui_->Text("Vertical margins: %.1f / %.1f", coverage->vertical.minimum_start_margin,
                 coverage->vertical.minimum_end_margin);
    }
    if (!coverage->horizontal.covers() || !coverage->vertical.covers()) {
      gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "Negative margin = uncovered camera area.");
    }
  }
  ASSIGN_OR_RETURN(const CompositionSeamDiagnostics seams,
                   AnalyzeCompositionSeams(layer, element_sizes));
  auto element_name = [&layer](int id) -> const std::string& {
    const auto found = std::find_if(layer.elements.begin(), layer.elements.end(),
                                    [id](const ParallaxElement& item) { return item.id == id; });
    return found->name;
  };
  for (const ElementSeamDiagnostics& seam : seams.adjacent) {
    gui_->Text("Adjacent %s -> %s: X bounds delta %.1f px",
               element_name(seam.first_element_id).c_str(),
               element_name(seam.second_element_id).c_str(), seam.separation.x);
  }
  if (seams.horizontal_wrap.has_value()) {
    gui_->Text("First/last X wrap bounds delta: %.1f px", seams.horizontal_wrap->separation.x);
  }
  if (seams.vertical_wrap.has_value()) {
    gui_->Text("First/last Y wrap bounds delta: %.1f px", seams.vertical_wrap->separation.y);
  }
  gui_->TextDisabled("Positive bounds delta = gap; negative = overlap. Inspect transparency too.");

  gui_->TableNextColumn();
  if (gui_->Button("Analyze Repetition")) SetError(AnalyzeSelectedTexture());
  if (diagnostics_ && diagnostics_->texture_id == element->texture_id) {
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
  if (layer.repeat_period.x > 0.0 || layer.repeat_period.y > 0.0) {
    gui_->TextWrapped(
        "The game-view preview repeats the complete composition. Scrub Travel X/Y to inspect "
        "the authored period; source-edge measurements above describe only the selected element.");
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
      close_prompt_.Disarm();
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

  bool save_requested = false;
  RETURN_IF_ERROR(RenderToolbar(*draft, save_requested));
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
      gui_->TableSetupColumn("Layers", ImGuiTableColumnFlags_WidthFixed, kNavigatorWidth);
      gui_->TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, kInspectorWidth);
      gui_->TableNextRow();
      gui_->TableNextColumn();
      if (ScopedChild navigator =
              gui_->CreateScopedChild("ThemeLayerNavigator", ImVec2(0, 0), false);
          navigator) {
        RETURN_IF_ERROR(RenderLayerNavigator(*draft));
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

  // Inspector fields commit when editing ends. Process Save afterward so a
  // click on Save persists the value that the click just finished editing.
  if (save_requested) SetError(Save());

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
