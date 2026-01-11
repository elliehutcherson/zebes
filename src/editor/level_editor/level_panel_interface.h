#pragma once

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "objects/level.h"

namespace zebes {

struct LevelResult {
  enum Type : uint8_t { kNone = 0, kChanged = 1, kAttach = 2, kDetach = 3 };
  Type type = Type::kNone;
  std::string level_id;
};

class LevelPanelInterface {
 public:
  virtual ~LevelPanelInterface() = default;

  // Renders the level panel UI.
  // Returns kChanged if the level was modified.
  virtual absl::StatusOr<LevelResult> Render(std::optional<Level>& level) = 0;
};

}  // namespace zebes
