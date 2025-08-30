#pragma once

#include <functional>

#include "absl/status/statusor.h"
#include "common/config.h"

namespace zebes {

class HudTileConfig {
 public:
  struct Options {
    TileConfig* hud_config;
    TileConfig* original_config;
    std::function<void()> save_config_callback;
  };

  static absl::StatusOr<HudTileConfig> Create(const Options& options);

  void Render();

 private:
  HudTileConfig(const Options& options);

  TileConfig* hud_config_;
  TileConfig* original_config_;
  std::function<void()> save_config_callback_;
};

}  // namespace zebes
