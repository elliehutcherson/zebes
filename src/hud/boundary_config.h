#pragma once

#include <functional>

#include "absl/status/statusor.h"
#include "common/config.h"

namespace zebes {

class HudBoundaryConfig {
 public:
  struct Options {
    BoundaryConfig* hud_config;
    BoundaryConfig* original_config;
    std::function<void()> save_config_callback;
  };

  static absl::StatusOr<HudBoundaryConfig> Create(const Options& options);

  void Render();

 private:
  HudBoundaryConfig(const Options& options);

  BoundaryConfig* hud_config_;
  BoundaryConfig* original_config_;
  std::function<void()> save_config_callback_;
};

}  // namespace zebes