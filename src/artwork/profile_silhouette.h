#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

}  // namespace zebes
