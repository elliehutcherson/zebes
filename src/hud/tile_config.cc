#include "hud/tile_config.h"

#include "absl/log/log.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<HudTileConfig> HudTileConfig::Create(
    const HudTileConfig::Options& options) {
  if (options.hud_config == nullptr)
    return absl::InvalidArgumentError("Hud config must not be null.");
  if (options.original_config == nullptr)
    return absl::InvalidArgumentError("Original config must not be null.");
  return HudTileConfig(options);
}

HudTileConfig::HudTileConfig(const Options& options)
    : hud_config_(options.hud_config),
      original_config_(options.original_config),
      save_config_callback_(options.save_config_callback) {}

void HudTileConfig::Render() {
  ImGui::InputInt("tile_scale", &hud_config_->scale);
  ImGui::InputInt("tile_source_width", &hud_config_->source_width);
  ImGui::InputInt("tile_source_height", &hud_config_->source_height);
  ImGui::InputInt("tile_size_x", &hud_config_->size_x);
  ImGui::InputInt("tile_size_y", &hud_config_->size_y);
  ImGui::Text("tile_render_width: %d",
              hud_config_->source_width * hud_config_->scale);
  ImGui::Text("tile_render_height: %d",
              hud_config_->source_height * hud_config_->scale);

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
    *hud_config_ = TileConfig();
  }
}

}  // namespace zebes
