#pragma once

#include <functional>

#include "absl/status/statusor.h"
#include "common/config.h"

namespace zebes {

class HudCollisionConfig {
 public:
  struct Options {
    CollisionConfig* hud_config;
    CollisionConfig* original_config;
    std::function<void()> save_config_callback;
  };

  static absl::StatusOr<HudCollisionConfig> Create(const Options& options);

  void Render();

 private:
  HudCollisionConfig(const Options& options);

  CollisionConfig* hud_config_;
  CollisionConfig* original_config_;
  std::function<void()> save_config_callback_;
};

}  // namespace zebes