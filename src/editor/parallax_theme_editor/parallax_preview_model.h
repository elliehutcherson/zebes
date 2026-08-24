#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/game_view.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/vec.h"

namespace zebes {

// Capacity errors describe a draft that is unsafe to preview, not a failure of
// the editor itself. The editor must keep its controls available for recovery.
bool IsRecoverableParallaxPreviewError(const absl::Status& status);

struct CameraWorldBounds {
  Vec min;
  Vec max;
};

// Inclusive endpoints for the camera center, not world content bounds.
struct CameraCenterRoute {
  Vec min;
  Vec max;
};

// An unassigned theme has no level route to constrain navigation. Expand a
// single-point fallback by one logical viewport so travel controls and direct
// canvas navigation remain useful.
CameraCenterRoute EnsureNavigableManualCameraRoute(CameraCenterRoute route,
                                                   const GameViewSize& game_view);

struct ParallaxThemeUsage {
  std::string level_id;
  std::string level_name;
  int zone_id = -1;
  std::string zone_name;
  CameraCenterRoute route;
  CameraWorldBounds world;
};

// Editor-only render input derived from a potentially incomplete draft. Source
// layer indices preserve selection after layers with no renderable elements are
// omitted. The authored draft is never weakened or silently made saveable.
struct ParallaxPreviewTheme {
  ParallaxTheme theme;
  std::vector<int> source_layer_indices;
  size_t omitted_elements = 0;
};

std::vector<ParallaxThemeUsage> FindParallaxThemeUsages(const std::vector<Level>& levels,
                                                        const std::string& theme_id);

// Intersects an authored camera-center route with centers actually reachable
// inside optional world bounds at one zoom. It fails clearly when inputs are
// invalid or the camera cannot enter the route at that zoom.
absl::StatusOr<CameraCenterRoute> ResolveCameraCenterRoute(
    CameraCenterRoute route, const GameViewSize& game_view, double zoom,
    std::optional<CameraWorldBounds> world = std::nullopt);

Vec InterpolateCameraCenter(CameraCenterRoute route, double x_progress, double y_progress);

// Inverse of InterpolateCameraCenter for canvas navigation. A stationary route
// axis maps to zero progress because every progress value reaches the same
// center on that axis.
absl::StatusOr<Vec> CalculateCameraTravel(CameraCenterRoute route, Vec camera_center);

// Keeps valid portions of an editor draft visible while a new or temporarily
// invalid element is being edited. Saving still validates the complete draft.
ParallaxPreviewTheme BuildParallaxPreviewTheme(const ParallaxTheme& draft);

std::optional<int> FindPreviewLayerIndex(const ParallaxPreviewTheme& preview,
                                         int source_layer_index);

}  // namespace zebes
