#include "editor/config_editor.h"

#include "SDL_render.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/sdl_wrapper.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ConfigEditor>> ConfigEditor::Create(Api* api, SdlWrapper* sdl) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SdlWrapper must not be null");
  }
  return std::unique_ptr<ConfigEditor>(new ConfigEditor(api, sdl));
}

ConfigEditor::ConfigEditor(Api* api, SdlWrapper* sdl)
    : api_(api), sdl_(sdl), current_config_(*api->GetConfig()), local_config_(current_config_) {
  // Ensure buffer has extra capacity for editing
  window_title_buffer_.resize(256, '\0');
}

void ConfigEditor::Render() {
  window_title_buffer_ = local_config_.window.title;

  if (ImGui::Button("Save Config")) {
    // Update title from buffer before saving
    local_config_.window.title = window_title_buffer_.c_str();
    absl::Status status = api_->SaveConfig(local_config_);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to save config: " << status;
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Reload from Disk")) {
    local_config_ = current_config_;
  }

  ImGui::Separator();
  ImGui::BeginChild("ConfigScrollRegion", ImVec2(0, 0), true);

  if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputInt("Target FPS", &local_config_.fps);
    ImGui::InputInt("Frame Delay (ms)", &local_config_.frame_delay);
  }

  if (ImGui::CollapsingHeader("Window Settings")) {
    ImGui::InputText("Title", window_title_buffer_.data(), window_title_buffer_.size());
    ImGui::InputInt("Width", &local_config_.window.width);
    ImGui::InputInt("Height", &local_config_.window.height);

    // Flags
    bool fullscreen = (local_config_.window.flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
      if (fullscreen)
        local_config_.window.flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
      else
        local_config_.window.flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;

      absl::Status s = sdl_->SetWindowFullscreen(fullscreen);
      if (!s.ok()) LOG(ERROR) << "Failed to set fullscreen: " << s;
    }

    bool resizable = (local_config_.window.flags & SDL_WINDOW_RESIZABLE);
    if (ImGui::Checkbox("Resizable", &resizable)) {
      if (resizable)
        local_config_.window.flags |= SDL_WINDOW_RESIZABLE;
      else
        local_config_.window.flags &= ~SDL_WINDOW_RESIZABLE;

      absl::Status s = sdl_->SetWindowResizable(resizable);
      if (!s.ok()) LOG(ERROR) << "Failed to set resizable: " << s;
    }

    bool high_dpi = (local_config_.window.flags & SDL_WINDOW_ALLOW_HIGHDPI);
    if (ImGui::Checkbox("High DPI", &high_dpi)) {
      if (high_dpi)
        local_config_.window.flags |= SDL_WINDOW_ALLOW_HIGHDPI;
      else
        local_config_.window.flags &= ~SDL_WINDOW_ALLOW_HIGHDPI;
    }
  }

  if (ImGui::CollapsingHeader("Boundaries")) {
    ImGui::InputInt("Min X", &local_config_.boundaries.x_min);
    ImGui::InputInt("Max X", &local_config_.boundaries.x_max);
    ImGui::InputInt("Min Y", &local_config_.boundaries.y_min);
    ImGui::InputInt("Max Y", &local_config_.boundaries.y_max);
  }

  if (ImGui::CollapsingHeader("Tiles")) {
    ImGui::InputInt("Scale", &local_config_.tiles.scale);
    ImGui::InputInt("Source Width", &local_config_.tiles.source_width);
    ImGui::InputInt("Source Height", &local_config_.tiles.source_height);
    ImGui::InputInt("Size X", &local_config_.tiles.size_x);
    ImGui::InputInt("Size Y", &local_config_.tiles.size_y);
  }

  if (ImGui::CollapsingHeader("Collision")) {
    ImGui::InputFloat("Area Width", &local_config_.collisions.area_width);
    ImGui::InputFloat("Area Height", &local_config_.collisions.area_height);
  }

  ImGui::EndChild();
}

}  // namespace zebes