#include "artwork/layered_puppet_diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/semantic_layer_import.h"
#include "common/image_io.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

struct Bounds {
  int minimum_x = 0;
  int minimum_y = 0;
  int maximum_x = 0;
  int maximum_y = 0;
};

size_t PixelIndex(int width, int x, int y) { return static_cast<size_t>(y) * width + x; }

double SignedArea(ProfileControlPoint first, ProfileControlPoint second,
                  ProfileControlPoint third) {
  return (second.x - first.x) * (third.y - first.y) - (second.y - first.y) * (third.x - first.x);
}

absl::StatusOr<Bounds> OpaqueBounds(const RgbaImage& image) {
  Bounds bounds{
      .minimum_x = image.width,
      .minimum_y = image.height,
      .maximum_x = 0,
      .maximum_y = 0,
  };
  bool found = false;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (image.pixels[PixelIndex(image.width, x, y) * 4 + 3] == 0) continue;
      found = true;
      bounds.minimum_x = std::min(bounds.minimum_x, x);
      bounds.minimum_y = std::min(bounds.minimum_y, y);
      bounds.maximum_x = std::max(bounds.maximum_x, x + 1);
      bounds.maximum_y = std::max(bounds.maximum_y, y + 1);
    }
  }
  if (!found) return absl::InvalidArgumentError("layered puppet layer contains no opaque pixels");
  return bounds;
}

bool BoundsOverlap(const Bounds& first, const Bounds& second) {
  return first.minimum_x < second.maximum_x && second.minimum_x < first.maximum_x &&
         first.minimum_y < second.maximum_y && second.minimum_y < first.maximum_y;
}

bool SameCanvas(const RgbaImage& first, const RgbaImage& second) {
  return first.width == second.width && first.height == second.height;
}

bool RestCentroidIsOpaque(const LayeredPuppetMesh& mesh, const LayeredPuppetMeshTriangle& triangle,
                          const RgbaImage& artwork) {
  double x = 0.0;
  double y = 0.0;
  for (const size_t vertex : triangle.vertices) {
    x += mesh.vertices[vertex].source.x / 3.0;
    y += mesh.vertices[vertex].source.y / 3.0;
  }
  const int sample_x = static_cast<int>(std::lround(x));
  const int sample_y = static_cast<int>(std::lround(y));
  if (sample_x < 0 || sample_y < 0 || sample_x >= artwork.width || sample_y >= artwork.height) {
    return false;
  }
  return artwork.pixels[PixelIndex(artwork.width, sample_x, sample_y) * 4 + 3] != 0;
}

}  // namespace

absl::StatusOr<LayeredPuppetTriangleReport> MeasureLayeredPuppetTriangles(
    const LayeredPuppetMesh& mesh, absl::Span<const ProfileControlPoint> deformed,
    const RgbaImage& artwork) {
  if (!artwork.IsValid()) {
    return absl::InvalidArgumentError("layered puppet triangle report requires valid artwork");
  }
  if (mesh.vertices.empty() || mesh.triangles.empty()) {
    return absl::InvalidArgumentError("layered puppet triangle report requires a mesh");
  }
  if (deformed.size() != mesh.vertices.size()) {
    return absl::InvalidArgumentError(
        "layered puppet triangle report needs one deformed point per mesh vertex");
  }
  for (const ProfileControlPoint point : deformed) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      return absl::InvalidArgumentError("layered puppet deformed vertices must be finite");
    }
  }
  LayeredPuppetTriangleReport report{.triangles = mesh.triangles.size()};
  for (const LayeredPuppetMeshTriangle& triangle : mesh.triangles) {
    for (const size_t vertex : triangle.vertices) {
      if (vertex >= mesh.vertices.size()) {
        return absl::InvalidArgumentError("layered puppet triangle references an unknown vertex");
      }
    }
    const double rest = SignedArea(mesh.vertices[triangle.vertices[0]].source,
                                   mesh.vertices[triangle.vertices[1]].source,
                                   mesh.vertices[triangle.vertices[2]].source);
    const double posed = SignedArea(deformed[triangle.vertices[0]], deformed[triangle.vertices[1]],
                                    deformed[triangle.vertices[2]]);
    // A rest triangle with no area has no winding to compare against, so it is
    // a mesh-construction fault rather than a deformation result.
    if (rest == 0.0) {
      return absl::InvalidArgumentError("layered puppet rest mesh contains a zero-area triangle");
    }
    if (posed == 0.0) {
      ++report.degenerate;
      continue;
    }
    if ((rest > 0.0) != (posed > 0.0)) {
      ++report.inverted;
      if (RestCentroidIsOpaque(mesh, triangle, artwork)) ++report.inverted_over_artwork;
    }
  }
  return report;
}

absl::StatusOr<LayeredPuppetBackfillReport> MeasureLayeredPuppetBackfill(
    const RgbaImage& moving_layer, absl::Span<const RgbaImage> static_layers) {
  if (!moving_layer.IsValid() || static_layers.empty()) {
    return absl::InvalidArgumentError(
        "layered puppet backfill report requires a moving layer and at least one static layer");
  }
  for (const RgbaImage& layer : static_layers) {
    if (!layer.IsValid() || !SameCanvas(layer, moving_layer)) {
      return absl::InvalidArgumentError("layered puppet backfill layers must share one canvas");
    }
  }
  LayeredPuppetBackfillReport report;
  const size_t pixels = static_cast<size_t>(moving_layer.width) * moving_layer.height;
  for (size_t index = 0; index < pixels; ++index) {
    if (moving_layer.pixels[index * 4 + 3] == 0) continue;
    ++report.moving_pixels;
    const bool covered =
        std::any_of(static_layers.begin(), static_layers.end(),
                    [index](const RgbaImage& layer) { return layer.pixels[index * 4 + 3] != 0; });
    if (!covered) ++report.uncovered_pixels;
  }
  if (report.moving_pixels == 0) {
    return absl::InvalidArgumentError("layered puppet backfill moving layer has no opaque pixels");
  }
  return report;
}

absl::StatusOr<LayeredPuppetOrphanReport> MeasureLayeredPuppetOrphans(
    const RgbaImage& static_layer, const RgbaImage& moving_layer) {
  if (!static_layer.IsValid() || !moving_layer.IsValid() ||
      !SameCanvas(static_layer, moving_layer)) {
    return absl::InvalidArgumentError("layered puppet orphan layers must share one canvas");
  }
  ASSIGN_OR_RETURN(const Bounds footprint, OpaqueBounds(moving_layer));
  ASSIGN_OR_RETURN(const std::vector<SemanticLayerComponent> components,
                   SplitSemanticLayerComponents(static_layer));
  if (components.empty()) {
    return absl::InvalidArgumentError("layered puppet orphan static layer has no opaque pixels");
  }
  const auto largest = std::max_element(
      components.begin(), components.end(),
      [](const SemanticLayerComponent& first, const SemanticLayerComponent& second) {
        return first.pixel_count < second.pixel_count;
      });
  LayeredPuppetOrphanReport report;
  for (auto component = components.begin(); component != components.end(); ++component) {
    if (component == largest) continue;
    const Bounds bounds{
        .minimum_x = component->minimum_x,
        .minimum_y = component->minimum_y,
        .maximum_x = component->maximum_x,
        .maximum_y = component->maximum_y,
    };
    if (!BoundsOverlap(bounds, footprint)) continue;
    ++report.components;
    report.orphan_pixels += component->pixel_count;
  }
  return report;
}

absl::StatusOr<size_t> MeasureLayeredPuppetInteriorHoles(const RgbaImage& pose) {
  if (!pose.IsValid()) {
    return absl::InvalidArgumentError("layered puppet interior hole report requires a valid image");
  }
  const size_t pixels = static_cast<size_t>(pose.width) * pose.height;
  std::vector<bool> outside(pixels, false);
  std::deque<std::pair<int, int>> pending;
  const auto enqueue = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= pose.width || y >= pose.height) return;
    const size_t index = PixelIndex(pose.width, x, y);
    if (outside[index] || pose.pixels[index * 4 + 3] != 0) return;
    outside[index] = true;
    pending.emplace_back(x, y);
  };
  for (int x = 0; x < pose.width; ++x) {
    enqueue(x, 0);
    enqueue(x, pose.height - 1);
  }
  for (int y = 0; y < pose.height; ++y) {
    enqueue(0, y);
    enqueue(pose.width - 1, y);
  }
  while (!pending.empty()) {
    const auto [x, y] = pending.front();
    pending.pop_front();
    enqueue(x - 1, y);
    enqueue(x + 1, y);
    enqueue(x, y - 1);
    enqueue(x, y + 1);
  }
  size_t holes = 0;
  for (size_t index = 0; index < pixels; ++index) {
    if (pose.pixels[index * 4 + 3] == 0 && !outside[index]) ++holes;
  }
  return holes;
}

}  // namespace zebes
