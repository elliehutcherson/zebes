#include "hud/window_config.h"

#include "absl/log/log.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<HudWindowConfig> HudWindowConfig::Create(
    const HudWindowConfig::Options& options) {
  if (options.hud_config == nullptr)
    return absl::InvalidArgumentError("Hud config must not be null.");
  if (options.original_config == nullptr)
    return absl::InvalidArgumentError("Original config must not be null.");
  return HudWindowConfig(options);
}

HudWindowConfig::HudWindowConfig(const Options& options)
    : hud_config_(options.hud_config),
      original_config_(options.original_config),
      save_config_callback_(options.save_config_callback) {}

void HudWindowConfig::Render() {
  static bool full_screen = hud_config_->full_screen();
  ImGui::PushItemWidth(100);
  ImGui::InputInt("window_width", &hud_config_->width);

  ImGui::PushItemWidth(100);
  ImGui::InputInt("window_height", &hud_config_->height);

  ImGui::PushItemWidth(100);
  if (ImGui::Checkbox("FullScreen ", &full_screen)) {
    hud_config_->flags = full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    LOG(INFO) << "Setting full screen to: " << full_screen;
  }

  if (ImGui::Button("Apply")) {
    save_config_callback_();
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting window config...";
    *hud_config_ = *original_config_;
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default window config...";
    *hud_config_ = WindowConfig();
  }
}

}  // namespace zebes
