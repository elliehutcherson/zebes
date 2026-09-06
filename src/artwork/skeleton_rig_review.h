#ifndef ZEBES_ARTWORK_SKELETON_RIG_REVIEW_H_
#define ZEBES_ARTWORK_SKELETON_RIG_REVIEW_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

struct SkeletonRigJoint {
  double x = 0.0;
  double y = 0.0;
};

struct SkeletonRigPoint {
  std::string name;
  std::string chain;
};

struct SkeletonRigBone {
  std::string start;
  std::string end;
};

struct SkeletonRigFrame {
  std::string label;
  std::string underlay;
  absl::flat_hash_map<std::string, SkeletonRigJoint> pose;
};

struct SkeletonRigClip {
  std::string id;
  std::string name;
  int fps = 0;
  std::vector<SkeletonRigFrame> frames;
};

struct SkeletonRig {
  int version = 0;
  double floor_y = 0.0;
  std::vector<SkeletonRigPoint> points;
  std::vector<SkeletonRigBone> bones;
  std::vector<SkeletonRigClip> clips;
};

struct SkeletonRigClipMetrics {
  double hip_oscillation = 0.0;
  double maximum_bone_length_drift = 0.0;
  bool left_foot_leads = false;
  bool right_foot_leads = false;
};

// Parses Rig Bench schema version 2 and rejects unknown fields, malformed
// topology, incomplete poses, duplicate names, and cyclic bone graphs.
absl::StatusOr<SkeletonRig> ParseSkeletonRig(std::string_view encoded);

// Reads and parses one Rig Bench JSON document.
absl::StatusOr<SkeletonRig> LoadSkeletonRig(const std::filesystem::path& path);

// Finds a named clip or returns NotFound when the rig does not own it.
absl::StatusOr<const SkeletonRigClip*> FindSkeletonRigClip(const SkeletonRig& rig,
                                                           std::string_view clip_id);

// Measures review-relevant invariants without changing authored coordinates.
// Bone drift is the largest max-minus-min segment length over the clip.
absl::StatusOr<SkeletonRigClipMetrics> MeasureSkeletonRigClip(const SkeletonRig& rig,
                                                              const SkeletonRigClip& clip);

// Renders one pose to an RGBA guide image. Coordinates and line widths are
// multiplied by render_scale so the 256-space rig can reproduce the 512px
// matched-pair skeleton references without changing joint locations.
absl::StatusOr<RgbaImage> RenderSkeletonRigFrameImage(const SkeletonRig& rig,
                                                      const SkeletonRigFrame& frame,
                                                      int canvas_width, int canvas_height,
                                                      int render_scale);

// Renders every clip frame in order to a tightly packed sheet. Rows are derived
// from the positive column count; unused cells in the final row remain white.
absl::StatusOr<RgbaImage> RenderSkeletonRigSheetImage(const SkeletonRig& rig,
                                                      const SkeletonRigClip& clip, int canvas_width,
                                                      int canvas_height, int render_scale,
                                                      int columns);

// Produces a standalone review page with an animated stage, selectable frame
// strip, chain legend, floor, and measured cycle diagnostics. The document has
// no external runtime dependencies and may be served by any static file server.
absl::StatusOr<std::string> RenderSkeletonRigReviewHtml(const SkeletonRig& rig,
                                                        const SkeletonRigClip& clip,
                                                        int canvas_width, int canvas_height);

}  // namespace zebes

#endif  // ZEBES_ARTWORK_SKELETON_RIG_REVIEW_H_
