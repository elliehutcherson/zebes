#pragma once

#include <optional>
#include <vector>

#include "objects/camera.h"
#include "objects/level.h"

namespace zebes {

// The portion of the world currently visible through a camera.
struct VisibleWorldBounds {
  Vec min;
  Vec max;
};

// Pure rendering geometry for one parallax layer. The renderer only needs to
// resolve a texture and emit the tiles described here.
struct ParallaxLayout {
  Vec origin;
  double tile_width = 0;
  double tile_height = 0;
  int first_column = 0;
  int last_column = 0;
  int first_row = 0;
  int last_row = 0;
};

// Stable result of resolving the environment at a world-space reference
// point. Later zones have priority when bounds overlap, matching their existing
// draw order.
struct ActiveParallaxZone {
  int zone_id = -1;
  int theme_id = -1;
};

// Camera state that centers and fits a world-space rectangle.
struct CameraFrame {
  Vec position;
  double zoom = 1;
};

VisibleWorldBounds CalculateVisibleWorldBounds(const Camera& camera);

const ParallaxZone* FindParallaxZoneById(const std::vector<ParallaxZone>& zones, int zone_id);

// Resolves one active zone using half-open bounds: min is inclusive and max is
// exclusive. Viewport size and zoom do not affect the result.
std::optional<ActiveParallaxZone> ResolveActiveParallaxZone(
    const std::vector<ParallaxZone>& zones, Vec reference_point);

// Calculates a camera view that fits bounds with proportional screen padding.
std::optional<CameraFrame> CalculateCameraFrame(VisibleWorldBounds bounds, int viewport_width,
                                                int viewport_height,
                                                double padding_fraction = 0.1);

// Centers the camera on target bounds and fits them when possible. If fitting
// the complete target would make that center invalid within world bounds, the
// zoom is increased just enough to preserve the center. This behaves
// symmetrically for long horizontal and tall vertical targets.
std::optional<CameraFrame> CalculateConstrainedCameraFrame(
    VisibleWorldBounds target_bounds, VisibleWorldBounds world_bounds, int viewport_width,
    int viewport_height, double padding_fraction = 0.1);

// Returns no layout when the camera, texture, or layer scale cannot produce
// valid geometry.
std::optional<ParallaxLayout> CalculateParallaxLayout(const Camera& camera,
                                                      const ParallaxLayer& layer,
                                                      int texture_width,
                                                      int texture_height);

}  // namespace zebes
