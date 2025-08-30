#include "hud/collision_config.h"

#include "absl/log/log.h"
#include "common/config.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<HudCollisionConfig> HudCollisionConfig::Create(
    const HudCollisionConfig::Options& options) {
  if (options.hud_config == nullptr)
    return absl::InvalidArgumentError("Hud config must not be null.");
  if (options.original_config == nullptr)
    return absl::InvalidArgumentError("Original config must not be null.");
  return HudCollisionConfig(options);
}

HudCollisionConfig::HudCollisionConfig(const Options& options)
    : hud_config_(options.hud_config),
      original_config_(options.original_config), 
      save_config_callback_(options.save_config_callback) {}


void HudCollisionConfig::Render() {
  ImGui::InputFloat("area_width", &hud_config_->area_width);
  ImGui::InputFloat("area_height", &hud_config_->area_height);
  if (ImGui::Button("Apply")) {
    save_config_callback_();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    *hud_config_ = *original_config_;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    *hud_config_ = CollisionConfig();
  }
}

}  // namespace zebes
