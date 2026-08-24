#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/vec.h"

namespace zebes {

inline constexpr int kParallaxThemeSchemaVersion = 2;

// One positioned texture inside a camera-relative layer. IDs are stable within
// their owning layer so editor selection survives reorder and deletion.
struct ParallaxElement {
  int id = -1;
  std::string name;
  std::string texture_id;
  Vec position;
  float scale = 1.0f;

  bool operator==(const ParallaxElement& other) const = default;
};

// One camera-relative image plane. Elements are ordered back to front within
// the plane. A positive repeat-period component repeats the complete element
// composition on that axis; zero makes that axis finite.
struct ParallaxLayer {
  std::string name;
  Vec scroll_factor;
  Vec offset;
  Vec repeat_period;
  std::vector<ParallaxElement> elements;

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
