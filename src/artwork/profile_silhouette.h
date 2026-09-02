#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/image_io.h"

namespace zebes {

// Configuration for deterministic target-view silhouette analysis. Generation
// and identity selection stay outside this boundary; the input is already an
// isolated RGBA image.
struct ProfileSilhouetteConfig {
  int working_size = 256;
  int alpha_threshold = 16;
  int minimum_branch_length = 5;
};

// A reduced authoritative silhouette and its one-pixel medial axis. Counts are
// review diagnostics, not semantic-joint claims: semantic anatomy remains a
// later layer built on this deterministic topology.
struct ProfileSilhouette {
  int width = 0;
  int height = 0;
  int source_scale = 0;
  std::vector<uint8_t> silhouette;
  std::vector<uint8_t> medial_axis;
  int silhouette_pixels = 0;
  int medial_axis_pixels = 0;
  int component_count = 0;
  int endpoint_count = 0;
  int branch_pixel_count = 0;

  bool IsValid() const;
};

struct ProfileControlPoint {
  double x = 0.0;
  double y = 0.0;
};

struct ProfileControlBone {
  size_t start_joint = 0;
  size_t end_joint = 0;
};

absl::Status ValidateProfileSilhouetteConfig(const ProfileSilhouetteConfig& config);

// Reduces the isolated alpha mask, extracts a topology-preserving Zhang-Suen
// medial axis, and removes terminal branches shorter than the configured limit.
// The source must be square and an integer multiple of working_size so one run
// never silently changes aspect or registration.
absl::StatusOr<ProfileSilhouette> ExtractProfileSilhouette(const RgbaImage& isolated,
                                                           const ProfileSilhouetteConfig& config);

// Gray is the exact reduced silhouette; red is the extracted medial axis.
absl::StatusOr<RgbaImage> RenderProfileSilhouetteEvidence(const ProfileSilhouette& profile);

// Binary Canny input for the neutral reference: the exact outer contour and
// medial axis are white on opaque black. Posed semantic bones use the same
// contract after joint inference.
absl::StatusOr<RgbaImage> RenderProfileSilhouetteControl(const ProfileSilhouette& profile);

// Binary posed Canny input from an approved silhouette mask and semantic
// skeleton. This renderer is stable C++; joint inference and pose selection are
// still experiment policy and remain outside this function.
absl::StatusOr<RgbaImage> RenderProfilePoseControl(absl::Span<const uint8_t> silhouette, int width,
                                                   int height,
                                                   absl::Span<const ProfileControlPoint> joints,
                                                   absl::Span<const ProfileControlBone> bones);

// Converts experiment-owned ordinal layer IDs into a grayscale depth guide.
// Layer ID zero is background; IDs 1..N index depth_by_layer at 0..N-1.
// Which limb is front remains experiment policy, while validation and pixel
// encoding are reusable deterministic C++.
absl::StatusOr<RgbaImage> RenderProfileOrdinalDepth(absl::Span<const uint8_t> layer_ids, int width,
                                                    int height,
                                                    absl::Span<const uint8_t> depth_by_layer);

}  // namespace zebes
