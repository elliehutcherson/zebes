#pragma once

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"
#include "objects/camera.h"
#include "objects/game_view.h"
#include "objects/level.h"

namespace zebes {

// One deterministic camera sample along a level-composition review route.
// key_roles identifies samples that also serve as isolated-pass evidence; an
// ordinary overlapping route frame has an empty role list.
struct LevelReviewCameraSample {
  std::string id;
  double progress = 0.0;
  Camera camera;
  std::vector<std::string> key_roles;
};

// One zone-owned authored-content track at one review zoom. A level without
// zones receives one world track with zone_id -1 so its world layers remain
// reviewable.
struct LevelReviewRoute {
  std::string id;
  int zone_id = -1;
  std::string zone_name;
  int track_index = 0;
  bool horizontal = true;
  double zoom = 1.0;
  CameraCenterRoute centers;
  std::vector<LevelReviewCameraSample> samples;
};

// Plans stable camera samples. The first track at each zoom overlaps adjacent
// frames by at least 50 percent; additional content tracks sample edge to edge
// to avoid redundant seam evidence. The complete viewport must fit inside the
// level at every requested zoom; a smaller world is rejected rather than
// rendered with undefined out-of-world content.
absl::StatusOr<std::vector<LevelReviewRoute>> PlanLevelReviewRoutes(const Level& level,
                                                                    const GameViewSize& game_view,
                                                                    absl::Span<const double> zooms);

}  // namespace zebes
