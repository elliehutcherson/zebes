#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"

namespace zebes {

struct ProfileDeformationConfig {
  // Pixels within this distance of a shared joint blend the inverse transforms
  // of both incident bones rather than behaving as rigid cutouts.
  double joint_blend_radius = 12.0;
  // If inverse mapping lands outside the source pixels owned by the same bone,
  // search this far for retained artwork before reporting an unmapped pixel.
  int maximum_source_search_radius = 16;
};

struct ProfileDeformationResult {
  RgbaImage image;
  int mapped_pixels = 0;
  int unmapped_pixels = 0;
};

absl::Status ValidateProfileDeformationConfig(const ProfileDeformationConfig& config);

// Inverse-maps each target layer pixel into the original isolated artwork.
// Layer IDs are 1-based bone indices; zero is transparent background. Target
// layer ownership supplies deterministic front/back composition. Joint blending
// smooths articulation without allowing one body layer to sample another.
absl::StatusOr<ProfileDeformationResult> DeformProfileArtwork(
    const RgbaImage& source, absl::Span<const uint8_t> source_layers,
    absl::Span<const uint8_t> target_layers, absl::Span<const ProfileControlPoint> source_joints,
    absl::Span<const ProfileControlPoint> target_joints, absl::Span<const ProfileControlBone> bones,
    const ProfileDeformationConfig& config);

}  // namespace zebes
