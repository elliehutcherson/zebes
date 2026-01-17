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
    ScopedChild child(gui_, "ConfigScrollRegion", ImVec2(0, 0), true);

    if (gui_->CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
      gui_->InputInt("Target FPS", &local_config_.fps);
      gui_->InputInt("Frame Delay (ms)", &local_config_.frame_delay);
    }

    if (gui_->CollapsingHeader("Window Settings")) {
      gui_->InputText("Title", window_title_buffer_.data(), window_title_buffer_.size());
      gui_->InputInt("Width", &local_config_.window.width);
      gui_->InputInt("Height", &local_config_.window.height);

      // Flags
      bool fullscreen = (local_config_.window.flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      if (gui_->Checkbox("Fullscreen", &fullscreen)) {
        if (fullscreen)
          local_config_.window.flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        else
          local_config_.window.flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;

        absl::Status s = sdl_->SetWindowFullscreen(fullscreen);
        if (!s.ok()) LOG(ERROR) << "Failed to set fullscreen: " << s;
      }

      bool resizable = (local_config_.window.flags & SDL_WINDOW_RESIZABLE);
      if (gui_->Checkbox("Resizable", &resizable)) {
        if (resizable)
          local_config_.window.flags |= SDL_WINDOW_RESIZABLE;
        else
          local_config_.window.flags &= ~SDL_WINDOW_RESIZABLE;

        absl::Status s = sdl_->SetWindowResizable(resizable);
        if (!s.ok()) LOG(ERROR) << "Failed to set resizable: " << s;
      }

      bool high_dpi = (local_config_.window.flags & SDL_WINDOW_ALLOW_HIGHDPI);
      if (gui_->Checkbox("High DPI", &high_dpi)) {
        if (high_dpi)
          local_config_.window.flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        else
          local_config_.window.flags &= ~SDL_WINDOW_ALLOW_HIGHDPI;
      }
    }
  }
}

}  // namespace zebes