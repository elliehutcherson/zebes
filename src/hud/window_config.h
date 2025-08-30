#pragma once

#include <functional>

#include "absl/status/statusor.h"
#include "common/config.h"

namespace zebes {

class HudWindowConfig {
 public:
  struct Options {
    WindowConfig* hud_config;
    WindowConfig* original_config;
    std::function<void()> save_config_callback;
  };

  static absl::StatusOr<HudWindowConfig> Create(const Options& options);

  void Render();

 private:
  HudWindowConfig(const Options& options);

  WindowConfig* hud_config_;
  WindowConfig* original_config_;
  std::function<void()> save_config_callback_;
};

}  // namespace zebes
