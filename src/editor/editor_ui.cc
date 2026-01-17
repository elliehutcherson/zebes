#define IMGUI_DEFINE_MATH_OPERATORS
#include "editor/editor_ui.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "editor/config_editor/config_editor.h"
#include "editor/editor_utils.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_editor.h"
#include "editor/sprite_editor/sprite_editor.h"
#include "editor/texture_editor/texture_editor.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EditorUi>> EditorUi::Create(SdlWrapper* sdl, Api* api,
                                                           GuiInterface* gui) {
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SDL wrapper is null");
  }
  if (api == nullptr) {
    return absl::InvalidArgumentError("API is null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("Gui interface is null");
  }
  auto editor_ui = absl::WrapUnique(new EditorUi(sdl, api, gui));
  RETURN_IF_ERROR(editor_ui->Init());
  return editor_ui;
}

EditorUi::EditorUi(SdlWrapper* sdl, Api* api, GuiInterface* gui)
    : sdl_(sdl), api_(api), gui_(gui), sprite_path_buffer_(256, '\0') {}

absl::Status EditorUi::Init() {
  ASSIGN_OR_RETURN(texture_editor_, TextureEditor::Create(api_, sdl_, gui_));
  ASSIGN_OR_RETURN(config_editor_, ConfigEditor::Create(api_, sdl_, gui_));
  ASSIGN_OR_RETURN(sprite_editor_, SpriteEditor::Create(api_, sdl_, gui_));
  ASSIGN_OR_RETURN(blueprint_editor_, BlueprintEditor::Create(api_, gui_));
  ASSIGN_OR_RETURN(level_editor_, LevelEditor::Create({.api = api_, .gui = gui_}));
  return absl::OkStatus();
}

void EditorUi::Render() {
  // Set up fullscreen window
  ImGuiViewport* viewport = gui_->GetMainViewport();
  gui_->SetNextWindowPos(viewport->Pos);
  gui_->SetNextWindowSize(viewport->Size);

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

  ScopedWindow window = gui_->CreateScopedWindow("Zebes Editor", nullptr, window_flags);

  if (ScopedTabBar tab_bar = gui_->CreateScopedTabBar("MainTabs"); tab_bar) {
    RenderTab("Texture Editor", [this]() {
      texture_editor_->Render();
      return absl::OkStatus();
    });
    RenderTab("Sprite Editor", [this]() {
      sprite_editor_->Render();
      return absl::OkStatus();
    });
    RenderTab("Blueprint Editor", [this]() { return blueprint_editor_->Render(); });
    RenderTab("Level Editor", [this]() { return level_editor_->Render(); });
    RenderTab("Config Editor", [this]() {
      config_editor_->Render();
      return absl::OkStatus();
    });
  }

  // Show debug state.
  if (gui_->IsKeyPressed(ImGuiKey_F1)) {
    show_debug_metrics_ = !show_debug_metrics_;  // Toggle on F1 press
  }
  // This ensures the metrics window floats *over* the editor,
  // regardless of which tab is open.
  if (show_debug_metrics_) {
    gui_->ShowMetricsWindow(&show_debug_metrics_);
  }
}

bool EditorUi::RenderTab(const char* name, std::function<absl::Status()> render_fn) {
  ScopedTabItem tab = gui_->CreateScopedTabItem(name);
  if (!tab) return false;

  absl::Status status = render_fn();
  if (status.ok()) return true;

  LOG(ERROR) << name << " Render error: " << status;

  // ImGui stack recovery
  ImGuiContext* g = ImGui::GetCurrentContext();

  // Close any unclosed child windows
  while (g->CurrentWindow != g->CurrentWindowStack[0].Window) {
    LOG(WARNING) << "Recovering unclosed window: " << g->CurrentWindow->Name;

    // Check what type of window it is by its flags
    if (g->CurrentWindow->Flags & ImGuiWindowFlags_ChildWindow) {
      gui_->EndChild();
    } else {
      gui_->End();
    }
  }

  // Close any unclosed popup stacks
  while (g->OpenPopupStack.Size > 0) {
    LOG(WARNING) << "Recovering unclosed popup";
    ImGui::EndPopup();
  }

  // Close any unclosed tab bars (more complex - ImGui doesn't expose this easily)
  // This is harder to detect reliably...

  // Attempt to recover
  status = Init();
  if (!status.ok()) {
    LOG(FATAL) << "UNABLE TO RECOVER FROM " << name << " ERROR: " << status;
  }

  return true;
}

}  // namespace zebes
