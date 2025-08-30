#include "boundary_config.h"

#include "absl/log/log.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<HudBoundaryConfig> HudBoundaryConfig::Create(
    const HudBoundaryConfig::Options& options) {
  if (options.hud_config == nullptr)
    return absl::InvalidArgumentError("Hud config must not be null.");
  if (options.original_config == nullptr)
    return absl::InvalidArgumentError("Original config must not be null.");
  return HudBoundaryConfig(options);
}

HudBoundaryConfig::HudBoundaryConfig(const Options& options)
    : hud_config_(options.hud_config),
      original_config_(options.original_config), 
      save_config_callback_(options.save_config_callback) {}

void HudBoundaryConfig::Render() {
  ImGui::InputInt("boundary_x_min", &hud_config_->x_min);
  ImGui::InputInt("boundary_x_max", &hud_config_->x_max);
  ImGui::InputInt("boundary_y_min", &hud_config_->y_min);
  ImGui::InputInt("boundary_y_max", &hud_config_->y_max);
  if (ImGui::Button("Apply")) {
    save_config_callback_();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting boundary config...";
    *hud_config_ = *original_config_;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default boundary config...";
    *hud_config_ = BoundaryConfig();
  }
}

}  // namespace zebes
