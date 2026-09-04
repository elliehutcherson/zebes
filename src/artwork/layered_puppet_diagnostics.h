#pragma once

#include <cstddef>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/layered_puppet.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"

namespace zebes {

// Measurements that catch layered-puppet defects the ownership and neutral
// gates cannot see. Ownership only proves each source pixel has exactly one
// owner; it says nothing about whether the character is still whole once a
// moving part leaves its rest position, and nothing about whether the mesh
// folds when a joint bends hard.
//
// Every function here is read-only and deterministic. None of them renders.

struct LayeredPuppetTriangleReport {
  size_t triangles = 0;
  // Signed area changed sign against the rest mesh, so the triangle folded
  // over its neighbors. Folded triangles overwrite each other during
  // rasterization, which loses the area they should have covered.
  size_t inverted = 0;
  // Deformed signed area collapsed to zero. These render nothing.
  size_t degenerate = 0;
  // The subset of inverted triangles whose rest position sits on opaque
  // artwork. Only these cost visible pixels; a fold in the transparent margin
  // of a bounding-box grid samples nothing and is harmless.
  size_t inverted_over_artwork = 0;
};

// Compares each deformed triangle's winding against its rest winding.
// deformed must hold one point per mesh vertex, in mesh vertex order, as
// returned by SolveLayeredPuppetMeshVertices. artwork is the part's source
// texture, sampled at each rest centroid to separate folds that matter from
// folds in empty space.
absl::StatusOr<LayeredPuppetTriangleReport> MeasureLayeredPuppetTriangles(
    const LayeredPuppetMesh& mesh, absl::Span<const ProfileControlPoint> deformed,
    const RgbaImage& artwork);

struct LayeredPuppetBackfillReport {
  size_t moving_pixels = 0;
  // Pixels the moving layer covers at rest that no static layer covers. Each
  // one becomes a transparent hole in the character as soon as the part moves.
  size_t uncovered_pixels = 0;
};

// Checks that something is painted behind a moving part. All layers must share
// one canvas.
absl::StatusOr<LayeredPuppetBackfillReport> MeasureLayeredPuppetBackfill(
    const RgbaImage& moving_layer, absl::Span<const RgbaImage> static_layers);

struct LayeredPuppetOrphanReport {
  size_t components = 0;
  size_t orphan_pixels = 0;
};

// Finds opaque islands of a static layer that sit inside a moving part's
// footprint. They are pixels the moving part should own and does not, so they
// stay welded to the body and read as debris once the part moves. Islands
// elsewhere on the canvas are legitimate detached artwork and are ignored, as
// is the layer's largest component.
absl::StatusOr<LayeredPuppetOrphanReport> MeasureLayeredPuppetOrphans(
    const RgbaImage& static_layer, const RgbaImage& moving_layer);

// Counts transparent pixels fully enclosed by an image's own opaque pixels,
// four-connected. Measure this against each pose's own output; measuring a
// pose against the neutral silhouette instead counts whole-body translation as
// missing artwork.
absl::StatusOr<size_t> MeasureLayeredPuppetInteriorHoles(const RgbaImage& pose);

}  // namespace zebes
