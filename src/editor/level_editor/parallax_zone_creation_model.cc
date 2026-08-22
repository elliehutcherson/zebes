#include "editor/level_editor/parallax_zone_creation_model.h"

#include <algorithm>
#include <cmath>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {

absl::Status ParallaxZoneCreationModel::Begin(const Level& level) {
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width <= 0.0 ||
      level.height <= 0.0) {
    return absl::FailedPreconditionError(
        "Set positive world dimensions before adding a parallax zone.");
  }
  draft_ = ParallaxZone{
      .name = absl::StrCat("Zone ", level.zones.size() + 1),
      .min_point = {0, 0},
      .max_point = {level.width, level.height},
  };
  return absl::OkStatus();
}

absl::Status ParallaxZoneCreationModel::Validate(const Level& level,
                                                 const std::vector<ParallaxTheme>& themes) const {
  if (!draft_) return absl::FailedPreconditionError("No parallax zone is being created.");
  if (draft_->name.empty()) return absl::InvalidArgumentError("Zone name cannot be empty.");
  const bool theme_exists =
      std::any_of(themes.begin(), themes.end(),
                  [this](const ParallaxTheme& theme) { return theme.id == draft_->theme_id; });
  if (!theme_exists) return absl::InvalidArgumentError("Choose an available parallax theme.");
  if (!std::isfinite(draft_->min_point.x) || !std::isfinite(draft_->min_point.y) ||
      !std::isfinite(draft_->max_point.x) || !std::isfinite(draft_->max_point.y) ||
      draft_->min_point.x < 0.0 || draft_->min_point.y < 0.0 || draft_->max_point.x > level.width ||
      draft_->max_point.y > level.height || draft_->min_point.x >= draft_->max_point.x ||
      draft_->min_point.y >= draft_->max_point.y) {
    return absl::InvalidArgumentError(
        "Zone bounds must have positive area inside the level world bounds.");
  }
  return absl::OkStatus();
}

absl::StatusOr<int> ParallaxZoneCreationModel::Commit(Level& level,
                                                      const std::vector<ParallaxTheme>& themes) {
  RETURN_IF_ERROR(Validate(level, themes));
  int new_id = 0;
  for (const ParallaxZone& zone : level.zones) new_id = std::max(new_id, zone.id + 1);
  draft_->id = new_id;
  level.zones.push_back(*draft_);
  draft_.reset();
  return new_id;
}

}  // namespace zebes
