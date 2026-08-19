#include "editor/config_editor/config_editor.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
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
  // ImGui writes into this buffer in place, so it needs room to grow beyond the
  // current title.
  window_title_buffer_.resize(256, '\0');
}

void ConfigEditor::Render() {
  window_title_buffer_ = local_config_.window.title;

  // Same banner every other authoring tab uses.
  if (error_message_.has_value()) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", error_message_->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss")) error_message_.reset();
  }

  if (gui_->Button("Save Config")) {
    // c_str() rather than the string itself: the buffer is padded with nulls
    // and assigning it whole would carry them into the config.
    local_config_.window.title = window_title_buffer_.c_str();
    absl::Status status = api_->SaveConfig(local_config_);
    if (status.ok()) {
      error_message_.reset();
    } else {
      LOG(ERROR) << "Failed to save config: " << status;
      error_message_ = absl::StrCat("Could not save config: ", status.message());
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

      // The checkbox has already moved, so a failure here leaves the control
      // disagreeing with the window until the user is told why.
      if (gui_->Checkbox("Fullscreen", &local_config_.window.fullscreen)) {
        absl::Status s = sdl_->SetWindowFullscreen(local_config_.window.fullscreen);
        if (!s.ok()) {
          LOG(ERROR) << "Failed to set fullscreen: " << s;
          error_message_ = absl::StrCat("Could not change fullscreen: ", s.message());
        }
      }

      if (gui_->Checkbox("Resizable", &local_config_.window.resizable)) {
        absl::Status s = sdl_->SetWindowResizable(local_config_.window.resizable);
        if (!s.ok()) {
          LOG(ERROR) << "Failed to set resizable: " << s;
          error_message_ = absl::StrCat("Could not change resizable: ", s.message());
        }
      }

      gui_->Checkbox("High DPI", &local_config_.window.high_dpi);
    }
  }
}

}  // namespace zebes
