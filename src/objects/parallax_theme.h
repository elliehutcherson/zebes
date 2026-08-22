#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/vec.h"

namespace zebes {

// One camera-relative image plane. Layers are ordered farthest to nearest in a
// ParallaxTheme and are never owned by a Level.
struct ParallaxLayer {
  std::string name;
  std::string texture_id;
  Vec scroll_factor;
  Vec offset;
  float base_scale = 1.0f;
  bool repeat_x = false;
  bool repeat_y = false;

  bool operator==(const ParallaxLayer& other) const = default;
};

// Reusable environment artwork referenced by level zones through `id`.
struct ParallaxTheme {
  std::string id;
  std::string name;
  std::vector<ParallaxLayer> layers;

  bool operator==(const ParallaxTheme& other) const = default;
};

// Saved themes are runtime-complete. Editors may keep incomplete drafts, but a
// manager must not publish one until this succeeds.
absl::Status ValidateParallaxTheme(const ParallaxTheme& theme);

nlohmann::json ParallaxThemeToJson(const ParallaxTheme& theme);
absl::StatusOr<ParallaxTheme> ParallaxThemeFromJson(const nlohmann::json& json);

}  // namespace zebes
