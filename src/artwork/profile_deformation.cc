#include "artwork/profile_deformation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

size_t PixelIndex(int width, int x, int y) { return static_cast<size_t>(y) * width + x; }

bool IsFinite(ProfileControlPoint point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

std::optional<size_t> SharedJoint(const ProfileControlBone& first,
                                  const ProfileControlBone& second) {
  if (first.start_joint == second.start_joint || first.start_joint == second.end_joint) {
    return first.start_joint;
  }
  if (first.end_joint == second.start_joint || first.end_joint == second.end_joint) {
    return first.end_joint;
  }
  return std::nullopt;
}

absl::StatusOr<Point> InverseTransform(Point target, const ProfileControlBone& bone,
                                       absl::Span<const ProfileControlPoint> source_joints,
                                       absl::Span<const ProfileControlPoint> target_joints) {
  const ProfileControlPoint source_start = source_joints[bone.start_joint];
  const ProfileControlPoint source_end = source_joints[bone.end_joint];
  const ProfileControlPoint target_start = target_joints[bone.start_joint];
  const ProfileControlPoint target_end = target_joints[bone.end_joint];
  const double source_dx = source_end.x - source_start.x;
  const double source_dy = source_end.y - source_start.y;
  const double target_dx = target_end.x - target_start.x;
  const double target_dy = target_end.y - target_start.y;
  const double source_length = std::hypot(source_dx, source_dy);
  const double target_length = std::hypot(target_dx, target_dy);
  if (source_length <= 1e-6 || target_length <= 1e-6) {
    return absl::InvalidArgumentError("profile deformation bone has zero length");
  }

  const double target_unit_x = target_dx / target_length;
  const double target_unit_y = target_dy / target_length;
  const double relative_x = target.x - target_start.x;
  const double relative_y = target.y - target_start.y;
  const double along = relative_x * target_unit_x + relative_y * target_unit_y;
  const double away = relative_x * -target_unit_y + relative_y * target_unit_x;
  const double source_unit_x = source_dx / source_length;
  const double source_unit_y = source_dy / source_length;
  return Point{
      .x = source_start.x + source_unit_x * along - source_unit_y * away,
      .y = source_start.y + source_unit_y * along + source_unit_x * away,
  };
}

absl::StatusOr<Point> BlendedSourcePoint(Point target, size_t primary_bone,
                                         absl::Span<const ProfileControlPoint> source_joints,
                                         absl::Span<const ProfileControlPoint> target_joints,
                                         absl::Span<const ProfileControlBone> bones,
                                         double blend_radius) {
  ASSIGN_OR_RETURN(const Point primary,
                   InverseTransform(target, bones[primary_bone], source_joints, target_joints));
  Point weighted = primary;
  double total_weight = 1.0;
  for (size_t neighbor = 0; neighbor < bones.size(); ++neighbor) {
    if (neighbor == primary_bone) continue;
    const std::optional<size_t> shared = SharedJoint(bones[primary_bone], bones[neighbor]);
    if (!shared.has_value()) continue;
    const ProfileControlPoint joint = target_joints[*shared];
    const double distance = std::hypot(target.x - joint.x, target.y - joint.y);
    if (distance >= blend_radius) continue;
    const double normalized = 1.0 - distance / blend_radius;
    const double weight = normalized * normalized;
    ASSIGN_OR_RETURN(const Point neighbor_source,
                     InverseTransform(target, bones[neighbor], source_joints, target_joints));
    weighted.x += neighbor_source.x * weight;
    weighted.y += neighbor_source.y * weight;
    total_weight += weight;
  }
  weighted.x /= total_weight;
  weighted.y /= total_weight;
  return weighted;
}

std::optional<size_t> FindSourcePixel(const RgbaImage& source,
                                      absl::Span<const uint8_t> source_layers, uint8_t layer_id,
                                      Point source_point, int maximum_radius) {
  const int center_x = static_cast<int>(std::lround(source_point.x));
  const int center_y = static_cast<int>(std::lround(source_point.y));
  for (int radius = 0; radius <= maximum_radius; ++radius) {
    double best_distance = std::numeric_limits<double>::infinity();
    std::optional<size_t> best;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        if (radius > 0 && std::abs(offset_x) != radius && std::abs(offset_y) != radius) {
          continue;
        }
        const int x = center_x + offset_x;
        const int y = center_y + offset_y;
        if (x < 0 || y < 0 || x >= source.width || y >= source.height) {
          continue;
        }
        const size_t index = PixelIndex(source.width, x, y);
        if (source_layers[index] != layer_id || source.pixels[index * 4 + 3] == 0) {
          continue;
        }
        const double distance = std::hypot(x - source_point.x, y - source_point.y);
        if (distance < best_distance) {
          best_distance = distance;
          best = index;
        }
      }
    }
    if (best.has_value()) return best;
  }
  return std::nullopt;
}

}  // namespace

absl::Status ValidateProfileDeformationConfig(const ProfileDeformationConfig& config) {
  if (!std::isfinite(config.joint_blend_radius) || config.joint_blend_radius <= 0.0) {
    return absl::InvalidArgumentError(
        "profile deformation joint blend radius must be finite and positive");
  }
  if (config.maximum_source_search_radius < 0) {
    return absl::InvalidArgumentError(
        "profile deformation source search radius must be non-negative");
  }
  return absl::OkStatus();
}

absl::StatusOr<ProfileDeformationResult> DeformProfileArtwork(
    const RgbaImage& source, absl::Span<const uint8_t> source_layers,
    absl::Span<const uint8_t> target_layers, absl::Span<const ProfileControlPoint> source_joints,
    absl::Span<const ProfileControlPoint> target_joints, absl::Span<const ProfileControlBone> bones,
    const ProfileDeformationConfig& config) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("profile deformation source image is invalid");
  }
  RETURN_IF_ERROR(ValidateProfileDeformationConfig(config));
  const size_t pixel_count = static_cast<size_t>(source.width) * source.height;
  if (source_layers.size() != pixel_count || target_layers.size() != pixel_count) {
    return absl::InvalidArgumentError(
        "profile deformation layer dimensions do not match source image");
  }
  if (source_joints.size() != target_joints.size() || source_joints.empty()) {
    return absl::InvalidArgumentError(
        "profile deformation source and target joints must correspond");
  }
  if (bones.empty() || bones.size() > 255) {
    return absl::InvalidArgumentError("profile deformation requires between 1 and 255 bones");
  }
  for (const ProfileControlPoint joint : source_joints) {
    if (!IsFinite(joint)) {
      return absl::InvalidArgumentError("profile deformation contains a non-finite source joint");
    }
  }
  for (const ProfileControlPoint joint : target_joints) {
    if (!IsFinite(joint)) {
      return absl::InvalidArgumentError("profile deformation contains a non-finite target joint");
    }
  }
  for (const ProfileControlBone bone : bones) {
    if (bone.start_joint >= source_joints.size() || bone.end_joint >= source_joints.size()) {
      return absl::InvalidArgumentError("profile deformation bone references an unknown joint");
    }
  }

  ProfileDeformationResult result{
      .image =
          RgbaImage{
              .width = source.width,
              .height = source.height,
              .pixels = std::vector<uint8_t>(pixel_count * 4, 0),
          },
  };
  for (size_t target_index = 0; target_index < pixel_count; ++target_index) {
    const uint8_t layer_id = target_layers[target_index];
    if (layer_id == 0) continue;
    const size_t bone_index = static_cast<size_t>(layer_id - 1);
    if (bone_index >= bones.size()) {
      return absl::InvalidArgumentError(
          "profile deformation target references an unknown bone layer");
    }
    const Point target{
        .x = static_cast<double>(target_index % source.width),
        .y = static_cast<double>(target_index / source.width),
    };
    ASSIGN_OR_RETURN(const Point source_point,
                     BlendedSourcePoint(target, bone_index, source_joints, target_joints, bones,
                                        config.joint_blend_radius));
    const std::optional<size_t> source_index = FindSourcePixel(
        source, source_layers, layer_id, source_point, config.maximum_source_search_radius);
    if (!source_index.has_value()) {
      ++result.unmapped_pixels;
      continue;
    }
    const size_t source_offset = *source_index * 4;
    const size_t target_offset = target_index * 4;
    std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset), 4,
                result.image.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
    ++result.mapped_pixels;
  }
  return result;
}

}  // namespace zebes
