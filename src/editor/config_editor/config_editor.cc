#include "editor/config_editor/config_editor.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/sdl_wrapper.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ConfigEditor>> ConfigEditor::Create(Api* api, SdlWrapper* sdl,
                                                                   GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SdlWrapper must not be null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI must not be null");
  }
  return std::unique_ptr<ConfigEditor>(new ConfigEditor(api, sdl, gui));
}

ConfigEditor::ConfigEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui)
    : api_(api),
      sdl_(sdl),
      gui_(gui),
      current_config_(*api->GetConfig()),
      local_config_(current_config_) {
  // Ensure buffer has extra capacity for editing
  window_title_buffer_.resize(256, '\0');
}

void ConfigEditor::Render() {
  window_title_buffer_ = local_config_.window.title;

  if (gui_->Button("Save Config")) {
    // Update title from buffer before saving
    local_config_.window.title = window_title_buffer_.c_str();
    absl::Status status = api_->SaveConfig(local_config_);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to save config: " << status;
    }
  }

  gui_->SameLine();
  if (gui_->Button("Reload from Disk")) {
    local_config_ = current_config_;
  }

  gui_->Separator();
  {
    ScopedChild child = gui_->CreateScopedChild("ConfigScrollRegion", ImVec2(0, 0), true);

    if (gui_->CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
      gui_->InputInt("Target FPS", &local_config_.fps);
      gui_->InputInt("Frame Delay (ms)", &local_config_.frame_delay);
    }

    if (gui_->CollapsingHeader("Game View", ImGuiTreeNodeFlags_DefaultOpen)) {
      gui_->InputInt("Logical Width", &local_config_.game_view.width);
      gui_->InputInt("Logical Height", &local_config_.game_view.height);
    }

    if (gui_->CollapsingHeader("Window Settings")) {
      gui_->InputText("Title", window_title_buffer_.data(), window_title_buffer_.size());
      gui_->InputInt("Width", &local_config_.window.width);
      gui_->InputInt("Height", &local_config_.window.height);
      gui_->Checkbox("Center on Start", &local_config_.window.centered);
      if (!local_config_.window.centered) {
        gui_->InputInt("Position X", &local_config_.window.x);
        gui_->InputInt("Position Y", &local_config_.window.y);
      }

      if (gui_->Checkbox("Fullscreen", &local_config_.window.fullscreen)) {
        absl::Status s = sdl_->SetWindowFullscreen(local_config_.window.fullscreen);
        if (!s.ok()) LOG(ERROR) << "Failed to set fullscreen: " << s;
      }

      if (gui_->Checkbox("Resizable", &local_config_.window.resizable)) {
        absl::Status s = sdl_->SetWindowResizable(local_config_.window.resizable);
        if (!s.ok()) LOG(ERROR) << "Failed to set resizable: " << s;
      }

      gui_->Checkbox("High DPI", &local_config_.window.high_dpi);
    }
  }
}

}  // namespace zebes
