#include "editor/editor_ui.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "editor/config_editor.h"
#include "editor/sprite_editor.h"
#include "editor/texture_editor.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EditorUi>> EditorUi::Create(SdlWrapper* sdl, Api* api) {
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SDL wrapper is null");
  }
  if (api == nullptr) {
    return absl::InvalidArgumentError("API is null");
  }
  auto editor_ui = absl::WrapUnique(new EditorUi(sdl, api));
  RETURN_IF_ERROR(editor_ui->Init());
  return editor_ui;
}

EditorUi::EditorUi(SdlWrapper* sdl, Api* api)
    : sdl_(sdl), api_(api), sprite_path_buffer_(256, '\0') {}

absl::Status EditorUi::Init() {
  ASSIGN_OR_RETURN(texture_editor_, TextureEditor::Create(api_, sdl_));
  ASSIGN_OR_RETURN(config_editor_, ConfigEditor::Create(api_, sdl_));
  ASSIGN_OR_RETURN(sprite_editor_, SpriteEditor::Create(api_, sdl_));
  ASSIGN_OR_RETURN(blueprint_editor_, BlueprintEditor::Create(api_));
  return absl::OkStatus();
}

void EditorUi::Render() {
  // Set up fullscreen window
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

  ImGui::Begin("Zebes Editor", nullptr, window_flags);

  if (ImGui::BeginTabBar("MainTabs")) {
    if (ImGui::BeginTabItem("Texture Editor")) {
      texture_editor_->Render();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sprite Editor")) {
      sprite_editor_->Render();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Blueprint Editor")) {
      blueprint_editor_->Render();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Config Editor")) {
      config_editor_->Render();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

}  // namespace zebes
