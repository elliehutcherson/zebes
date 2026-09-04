#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"

namespace zebes {

// One closed polygon in the puppet's working-resolution coordinate system.
// Pixel centers inside the polygon are selected; points may lie on the canvas
// boundary so an authored region can include the final row or column.
struct LayeredPuppetPolygon {
  std::vector<ProfileControlPoint> points;
};

// Authored hidden-surface artwork. The solid color normally comes from a source
// artwork sample chosen by the adapter; generated RGBA layers can instead be
// supplied directly as LayeredPuppetPart::artwork.
struct LayeredPuppetFill {
  LayeredPuppetPolygon polygon;
  std::array<uint8_t, 4> color{};
};

// One independently composited animation layer bound to one skeleton bone.
// Artwork uses the complete puppet canvas so source joints and pixels share one
// coordinate system. Overlap between parts is intentional and hides joint gaps.
struct LayeredPuppetPart {
  std::string name;
  size_t bone_index = 0;
  RgbaImage artwork;
};

// One target pose. draw_order contains every part index exactly once, from back
// to front, so rendering performs no semantic or string lookup.
struct LayeredPuppetPose {
  std::string name;
  std::vector<ProfileControlPoint> joints;
  std::vector<size_t> draw_order;
};

// Complete deterministic input for a separated 2D puppet. Inference adapters
// must resolve semantics, hidden pixels, joints, and draw order before creating
// this value; the renderer never guesses missing ownership.
struct LayeredPuppet {
  int width = 0;
  int height = 0;
  std::vector<ProfileControlPoint> source_joints;
  std::vector<ProfileControlBone> bones;
  std::vector<LayeredPuppetPart> parts;
  std::vector<LayeredPuppetPose> poses;
};

// Extracts original visible pixels inside source_polygons and applies authored
// hidden-surface fills underneath them. The returned image retains the source
// canvas dimensions and may overlap artwork owned by other parts.
absl::StatusOr<RgbaImage> BuildLayeredPuppetPartArtwork(
    const RgbaImage& source, absl::Span<const LayeredPuppetPolygon> source_polygons,
    absl::Span<const LayeredPuppetFill> fills);

// Rejects incomplete or ambiguous puppet state before any evidence is written.
// Every part must use the common canvas and a known bone; every pose must provide
// corresponding joints and a permutation of all parts.
absl::Status ValidateLayeredPuppet(const LayeredPuppet& puppet);

// Rigidly inverse-samples each separated part through its bone, then composites
// parts in authored back-to-front order. Hidden underpaint and overlap come only
// from the supplied artwork; rendering does not synthesize pixels.
absl::StatusOr<RgbaImage> RenderLayeredPuppetPose(const LayeredPuppet& puppet,
                                                  const LayeredPuppetPose& pose);

// Reduces a working-resolution pose to a native square sprite. RGB averages only
// covered source pixels; alpha remains binary and requires the requested source
// coverage so isolated edge noise cannot create a sprite pixel.
absl::StatusOr<RgbaImage> DownsampleLayeredPuppetFrame(const RgbaImage& source, int frame_size,
                                                       double coverage_threshold = 0.2);

// Packs equal-sized frames left to right without resampling.
absl::StatusOr<RgbaImage> PackLayeredPuppetFrames(absl::Span<const RgbaImage> frames);

// Enlarges one image by integer nearest-neighbor replication for visual review.
absl::StatusOr<RgbaImage> ZoomLayeredPuppetEvidence(const RgbaImage& source, int scale);

}  // namespace zebes
