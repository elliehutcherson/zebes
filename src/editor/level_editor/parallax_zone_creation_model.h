#pragma once

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"

namespace zebes {

// A transient zone draft. It enters Level::zones only after every required
// field validates, so cancel and failed commit cannot leave a partial zone.
class ParallaxZoneCreationModel {
 public:
  absl::Status Begin(const Level& level);
  void Cancel() { draft_.reset(); }
  bool active() const { return draft_.has_value(); }
  ParallaxZone* draft() { return draft_ ? &*draft_ : nullptr; }
  const ParallaxZone* draft() const { return draft_ ? &*draft_ : nullptr; }

  absl::Status Validate(const Level& level, const std::vector<ParallaxTheme>& themes) const;
  absl::StatusOr<int> Commit(Level& level, const std::vector<ParallaxTheme>& themes);

 private:
  std::optional<ParallaxZone> draft_;
};

}  // namespace zebes
