#pragma once

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "editor/level_editor/viewport_model.h"
#include "objects/camera.h"
#include "objects/parallax_theme.h"

namespace zebes {

// The portion of the world currently visible through a camera.
struct VisibleWorldBounds {
  Vec min;
  Vec max;
};

// Native source dimensions for one layer element. Keeping resource handles out
// of layout makes repetition and culling testable without a rendering backend.
struct ParallaxElementSize {
  int element_id = -1;
  int width = 0;
  int height = 0;
};

// One visible instance of an authored element. The repeat coordinates identify
// which copy of the complete composition owns it; both are zero for a finite
// layer.
struct ParallaxElementLayout {
  int element_id = -1;
  int repeat_column = 0;
  int repeat_row = 0;
  WorldRect bounds;
};

struct ParallaxElementBounds {
  int element_id = -1;
  WorldRect bounds;
};

// Pure rendering geometry for one parallax layer, in deterministic draw order.
struct ParallaxLayout {
  Vec origin;
  std::vector<ParallaxElementLayout> elements;
};

// Returns the local-space union of every scaled element rectangle.
absl::StatusOr<WorldRect> CalculateParallaxCompositionBounds(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes);

// Returns one local-space scaled rectangle per element in authored order.
absl::StatusOr<std::vector<ParallaxElementBounds>> CalculateParallaxElementBounds(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes);

// Camera state that centers and fits a world-space rectangle.
struct CameraFrame {
  Vec position;
  double zoom = 1;
};

VisibleWorldBounds CalculateVisibleWorldBounds(const Camera& camera);

// Calculates a camera view that fits bounds with proportional screen padding.
std::optional<CameraFrame> CalculateCameraFrame(VisibleWorldBounds bounds, int viewport_width,
                                                int viewport_height, double padding_fraction = 0.1);

// Centers the camera on target bounds and fits them when possible. If fitting
// the complete target would make that center invalid within world bounds, the
// zoom is increased just enough to preserve the center. This behaves
// symmetrically for long horizontal and tall vertical targets.
std::optional<CameraFrame> CalculateConstrainedCameraFrame(VisibleWorldBounds target_bounds,
                                                           VisibleWorldBounds world_bounds,
                                                           int viewport_width, int viewport_height,
                                                           double padding_fraction = 0.1);

// Calculates visible instances after repeating the complete composition. Bad
// geometry, missing element dimensions, and layouts large enough to indicate a
// pathological repeat period are rejected explicitly.
absl::StatusOr<ParallaxLayout> CalculateParallaxLayout(
    const Camera& camera, const ParallaxLayer& layer,
    const std::vector<ParallaxElementSize>& element_sizes);

}  // namespace zebes
