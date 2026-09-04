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

// One vertex in a source-space deformation grid. For a two-bone part,
// first_bone_weight selects the first listed bone and the remainder selects the
// second. Neutral transforms therefore reproduce the original texture exactly.
struct LayeredPuppetMeshVertex {
  ProfileControlPoint source;
  double first_bone_weight = 1.0;
};

struct LayeredPuppetMeshTriangle {
  std::array<size_t, 3> vertices{};
};

struct LayeredPuppetMesh {
  std::vector<LayeredPuppetMeshVertex> vertices;
  std::vector<LayeredPuppetMeshTriangle> triangles;
};

// One independently composited animation layer. A one-bone part is rigid. A
// two-bone part must provide a mesh whose vertex weights blend the two bone
// transforms. Artwork uses the complete puppet canvas so joints and pixels
// share one coordinate system.
struct LayeredPuppetPart {
  std::string name;
  std::vector<size_t> bone_indices;
  RgbaImage artwork;
  // Original source pixels owned by this part. It may be empty for generated
  // underpaint that owns no visible source pixels.
  RgbaImage visible_artwork;
  LayeredPuppetMesh mesh;
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

// Builds a triangulated deformation grid over the artwork, keeping only cells
// that carry opaque pixels plus one cell of transparent collar around the
// contour. A joint-local blend band transitions weights between two connected
// bones while keeping each outer endpoint rigidly attached. The grid is
// source-only and reused across every pose.
//
// joint_blend_lateral_scale widens the blend band with distance from the bone.
// Zero reproduces a band of constant width, where a thick part folds over
// itself at the joint: rotating about the joint by an angle that changes at
// rate g inverts the map at lateral distance d once g*d exceeds one. Spreading
// the same angle change over a longer stretch away from the bone lowers g
// exactly where d is large, leaving the band on the bone axis unchanged.
absl::StatusOr<LayeredPuppetMesh> BuildLayeredPuppetMesh(
    const RgbaImage& artwork, absl::Span<const size_t> bone_indices,
    absl::Span<const ProfileControlBone> bones, absl::Span<const ProfileControlPoint> source_joints,
    int spacing, double joint_blend_radius, double joint_blend_lateral_scale = 0.0);

// How far from its bone chain a part may claim source pixels. Reach is
// interpolated along the chain from its outer start joint to its outer end.
//
// A limb is not equally attached along its length. At the shoulder a sleeve is
// part of the garment, so a wide reach is correct. At the hand the limb is free,
// so a tight reach keeps a neighbouring body part from being claimed by a
// candidate layer whose alpha happens to spill onto it.
struct LayeredPuppetOwnershipReach {
  double start = 0.0;
  double end = 0.0;
  // Pixels of dilation applied to the candidate alpha, so a soft painted edge
  // is claimed whole rather than leaving a fringe behind on the body.
  int grow = 1;
};

// Derives which source pixels a skinned part owns, from the part's own
// candidate artwork rather than an authored outline. A pixel is owned when the
// grown candidate paints it, the approved source paints it, and it lies within
// reach of the bone chain.
//
// The returned image is opaque white where owned and transparent elsewhere, on
// the source canvas. Use it both to cut the part's visible artwork out of the
// source and to subtract that region from whatever layer owned it before.
absl::StatusOr<RgbaImage> BuildLayeredPuppetOwnershipMask(
    const RgbaImage& candidate, const RgbaImage& source, absl::Span<const size_t> bone_indices,
    absl::Span<const ProfileControlBone> bones, absl::Span<const ProfileControlPoint> source_joints,
    const LayeredPuppetOwnershipReach& reach);

// Copies source pixels selected by mask onto a transparent canvas. This is the
// ownership-mask counterpart of BuildLayeredPuppetPartArtwork's polygon path.
absl::StatusOr<RgbaImage> BuildLayeredPuppetMaskedArtwork(const RgbaImage& source,
                                                          const RgbaImage& mask);

// Clears every pixel of artwork that mask selects.
absl::Status SubtractLayeredPuppetMask(RgbaImage& artwork, const RgbaImage& mask);

// Every part must use the common canvas and either one known bone or two known
// connected bones with a valid mesh. Every pose must provide corresponding
// joints and a permutation of all parts.
absl::Status ValidateLayeredPuppet(const LayeredPuppet& puppet);

// Returns the deformed mesh vertex positions one pose produces for a skinned
// part, in mesh vertex order and without rasterizing. This is the input to
// triangle-orientation diagnostics and to any future solver. part_index must
// name an existing part that carries a mesh.
absl::StatusOr<std::vector<ProfileControlPoint>> SolveLayeredPuppetMeshVertices(
    const LayeredPuppet& puppet, const LayeredPuppetPose& pose, size_t part_index);

// Renders one part alone with the same deformation path used by full
// composition. This is the evidence boundary for component, joint, and
// reconstruction diagnostics; part_index must name an existing part.
absl::StatusOr<RgbaImage> RenderLayeredPuppetPart(const LayeredPuppet& puppet,
                                                  const LayeredPuppetPose& pose, size_t part_index);

// Rigidly inverse-samples one-bone parts and triangle-rasterizes two-bone
// skinned parts, then composites in authored back-to-front order. Hidden
// underpaint and overlap come only from supplied artwork.
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
