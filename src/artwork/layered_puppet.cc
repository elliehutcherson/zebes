#include "artwork/layered_puppet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Bounds {
  int minimum_x = 0;
  int minimum_y = 0;
  int maximum_x = 0;
  int maximum_y = 0;
};

size_t PixelIndex(int width, int x, int y) { return static_cast<size_t>(y) * width + x; }

bool IsFinite(ProfileControlPoint point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

bool HasOpaquePixel(const RgbaImage& image) {
  for (size_t offset = 3; offset < image.pixels.size(); offset += 4) {
    if (image.pixels[offset] != 0) return true;
  }
  return false;
}

absl::Status ValidatePolygon(const LayeredPuppetPolygon& polygon, int width, int height) {
  if (polygon.points.size() < 3) {
    return absl::InvalidArgumentError("layered puppet polygon needs at least three points");
  }
  for (const ProfileControlPoint point : polygon.points) {
    if (!IsFinite(point) || point.x < 0.0 || point.x > width || point.y < 0.0 || point.y > height) {
      return absl::InvalidArgumentError("layered puppet polygon extends outside its canvas");
    }
  }
  return absl::OkStatus();
}

bool IsInsidePolygon(double x, double y, const LayeredPuppetPolygon& polygon) {
  bool inside = false;
  ProfileControlPoint previous = polygon.points.back();
  for (const ProfileControlPoint current : polygon.points) {
    const bool crosses = (current.y > y) != (previous.y > y);
    if (crosses) {
      const double boundary_x =
          (previous.x - current.x) * (y - current.y) / (previous.y - current.y) + current.x;
      if (x < boundary_x) inside = !inside;
    }
    previous = current;
  }
  return inside;
}

Bounds PolygonBounds(const LayeredPuppetPolygon& polygon, int width, int height) {
  double minimum_x = polygon.points.front().x;
  double minimum_y = polygon.points.front().y;
  double maximum_x = minimum_x;
  double maximum_y = minimum_y;
  for (const ProfileControlPoint point : polygon.points) {
    minimum_x = std::min(minimum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    maximum_x = std::max(maximum_x, point.x);
    maximum_y = std::max(maximum_y, point.y);
  }
  return Bounds{
      .minimum_x = std::max(0, static_cast<int>(std::floor(minimum_x))),
      .minimum_y = std::max(0, static_cast<int>(std::floor(minimum_y))),
      .maximum_x = std::min(width, static_cast<int>(std::ceil(maximum_x))),
      .maximum_y = std::min(height, static_cast<int>(std::ceil(maximum_y))),
  };
}

absl::Status PaintPolygon(RgbaImage& destination, const LayeredPuppetPolygon& polygon,
                          const RgbaImage* source, const std::array<uint8_t, 4>* fill_color) {
  RETURN_IF_ERROR(ValidatePolygon(polygon, destination.width, destination.height));
  const Bounds bounds = PolygonBounds(polygon, destination.width, destination.height);
  for (int y = bounds.minimum_y; y < bounds.maximum_y; ++y) {
    for (int x = bounds.minimum_x; x < bounds.maximum_x; ++x) {
      if (!IsInsidePolygon(x + 0.5, y + 0.5, polygon)) continue;
      const size_t offset = PixelIndex(destination.width, x, y) * 4;
      if (source != nullptr) {
        if (source->pixels[offset + 3] == 0) continue;
        std::copy_n(source->pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                    destination.pixels.begin() + static_cast<ptrdiff_t>(offset));
        continue;
      }
      std::copy(fill_color->begin(), fill_color->end(),
                destination.pixels.begin() + static_cast<ptrdiff_t>(offset));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<Point> TransformPoint(Point point, const ProfileControlBone& bone,
                                     absl::Span<const ProfileControlPoint> source_joints,
                                     absl::Span<const ProfileControlPoint> target_joints,
                                     bool inverse) {
  const ProfileControlPoint source_start =
      inverse ? target_joints[bone.start_joint] : source_joints[bone.start_joint];
  const ProfileControlPoint source_end =
      inverse ? target_joints[bone.end_joint] : source_joints[bone.end_joint];
  const ProfileControlPoint target_start =
      inverse ? source_joints[bone.start_joint] : target_joints[bone.start_joint];
  const ProfileControlPoint target_end =
      inverse ? source_joints[bone.end_joint] : target_joints[bone.end_joint];
  const double source_dx = source_end.x - source_start.x;
  const double source_dy = source_end.y - source_start.y;
  const double target_dx = target_end.x - target_start.x;
  const double target_dy = target_end.y - target_start.y;
  const double source_length = std::hypot(source_dx, source_dy);
  const double target_length = std::hypot(target_dx, target_dy);
  if (source_length <= 1e-6 || target_length <= 1e-6) {
    return absl::InvalidArgumentError("layered puppet bone has zero length");
  }

  const double source_unit_x = source_dx / source_length;
  const double source_unit_y = source_dy / source_length;
  const double relative_x = point.x - source_start.x;
  const double relative_y = point.y - source_start.y;
  const double along = relative_x * source_unit_x + relative_y * source_unit_y;
  const double away = relative_x * -source_unit_y + relative_y * source_unit_x;
  const double target_unit_x = target_dx / target_length;
  const double target_unit_y = target_dy / target_length;
  return Point{
      .x = target_start.x + target_unit_x * along - target_unit_y * away,
      .y = target_start.y + target_unit_y * along + target_unit_x * away,
  };
}

absl::StatusOr<Bounds> TransformedOpaqueBounds(const LayeredPuppetPart& part,
                                               const ProfileControlBone& bone,
                                               absl::Span<const ProfileControlPoint> source_joints,
                                               absl::Span<const ProfileControlPoint> target_joints,
                                               int width, int height) {
  int source_minimum_x = width;
  int source_minimum_y = height;
  int source_maximum_x = -1;
  int source_maximum_y = -1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (part.artwork.pixels[PixelIndex(width, x, y) * 4 + 3] == 0) continue;
      source_minimum_x = std::min(source_minimum_x, x);
      source_minimum_y = std::min(source_minimum_y, y);
      source_maximum_x = std::max(source_maximum_x, x);
      source_maximum_y = std::max(source_maximum_y, y);
    }
  }
  if (source_maximum_x < source_minimum_x || source_maximum_y < source_minimum_y) {
    return absl::InvalidArgumentError("layered puppet part contains no opaque artwork");
  }

  const std::array<Point, 4> corners = {
      Point{.x = static_cast<double>(source_minimum_x), .y = static_cast<double>(source_minimum_y)},
      Point{.x = static_cast<double>(source_maximum_x), .y = static_cast<double>(source_minimum_y)},
      Point{.x = static_cast<double>(source_minimum_x), .y = static_cast<double>(source_maximum_y)},
      Point{.x = static_cast<double>(source_maximum_x), .y = static_cast<double>(source_maximum_y)},
  };
  double minimum_x = std::numeric_limits<double>::infinity();
  double minimum_y = std::numeric_limits<double>::infinity();
  double maximum_x = -std::numeric_limits<double>::infinity();
  double maximum_y = -std::numeric_limits<double>::infinity();
  for (const Point corner : corners) {
    ASSIGN_OR_RETURN(const Point transformed,
                     TransformPoint(corner, bone, source_joints, target_joints, false));
    minimum_x = std::min(minimum_x, transformed.x);
    minimum_y = std::min(minimum_y, transformed.y);
    maximum_x = std::max(maximum_x, transformed.x);
    maximum_y = std::max(maximum_y, transformed.y);
  }
  return Bounds{
      .minimum_x = std::max(0, static_cast<int>(std::floor(minimum_x)) - 1),
      .minimum_y = std::max(0, static_cast<int>(std::floor(minimum_y)) - 1),
      .maximum_x = std::min(width, static_cast<int>(std::ceil(maximum_x)) + 2),
      .maximum_y = std::min(height, static_cast<int>(std::ceil(maximum_y)) + 2),
  };
}

void CompositePixel(absl::Span<const uint8_t> source, absl::Span<uint8_t> destination) {
  const uint8_t source_alpha = source[3];
  if (source_alpha == 0) return;
  if (source_alpha == 255 || destination[3] == 0) {
    std::copy(source.begin(), source.end(), destination.begin());
    return;
  }
  const int destination_alpha = destination[3];
  const int combined_alpha = source_alpha + (destination_alpha * (255 - source_alpha) + 127) / 255;
  for (int channel = 0; channel < 3; ++channel) {
    const int numerator =
        source[channel] * source_alpha +
        (destination[channel] * destination_alpha * (255 - source_alpha) + 127) / 255;
    destination[channel] = static_cast<uint8_t>((numerator + combined_alpha / 2) / combined_alpha);
  }
  destination[3] = static_cast<uint8_t>(combined_alpha);
}

absl::Status RenderPart(const LayeredPuppet& puppet, const LayeredPuppetPose& pose,
                        const LayeredPuppetPart& part, RgbaImage& destination) {
  const ProfileControlBone bone = puppet.bones[part.bone_index];
  ASSIGN_OR_RETURN(const Bounds bounds,
                   TransformedOpaqueBounds(part, bone, puppet.source_joints, pose.joints,
                                           puppet.width, puppet.height));
  for (int y = bounds.minimum_y; y < bounds.maximum_y; ++y) {
    for (int x = bounds.minimum_x; x < bounds.maximum_x; ++x) {
      ASSIGN_OR_RETURN(
          const Point source_point,
          TransformPoint(Point{.x = static_cast<double>(x), .y = static_cast<double>(y)}, bone,
                         puppet.source_joints, pose.joints, true));
      const int source_x = static_cast<int>(std::lround(source_point.x));
      const int source_y = static_cast<int>(std::lround(source_point.y));
      if (source_x < 0 || source_y < 0 || source_x >= puppet.width || source_y >= puppet.height) {
        continue;
      }
      const size_t source_offset = PixelIndex(puppet.width, source_x, source_y) * 4;
      if (part.artwork.pixels[source_offset + 3] == 0) continue;
      const size_t target_offset = PixelIndex(puppet.width, x, y) * 4;
      CompositePixel(absl::MakeConstSpan(part.artwork.pixels).subspan(source_offset, 4),
                     absl::MakeSpan(destination.pixels).subspan(target_offset, 4));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<RgbaImage> BuildLayeredPuppetPartArtwork(
    const RgbaImage& source, absl::Span<const LayeredPuppetPolygon> source_polygons,
    absl::Span<const LayeredPuppetFill> fills) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("layered puppet source image is invalid");
  }
  if (source_polygons.empty() && fills.empty()) {
    return absl::InvalidArgumentError("layered puppet part contains no artwork regions");
  }
  RgbaImage result{
      .width = source.width,
      .height = source.height,
      .pixels = std::vector<uint8_t>(source.pixels.size(), 0),
  };
  for (const LayeredPuppetFill& fill : fills) {
    RETURN_IF_ERROR(PaintPolygon(result, fill.polygon, nullptr, &fill.color));
  }
  for (const LayeredPuppetPolygon& polygon : source_polygons) {
    RETURN_IF_ERROR(PaintPolygon(result, polygon, &source, nullptr));
  }
  if (!HasOpaquePixel(result)) {
    return absl::InvalidArgumentError("layered puppet part produced no opaque artwork");
  }
  return result;
}

absl::Status ValidateLayeredPuppet(const LayeredPuppet& puppet) {
  if (puppet.width <= 0 || puppet.height <= 0) {
    return absl::InvalidArgumentError("layered puppet dimensions must be positive");
  }
  if (puppet.source_joints.empty() || puppet.bones.empty() || puppet.parts.empty() ||
      puppet.poses.empty()) {
    return absl::InvalidArgumentError(
        "layered puppet skeleton, parts, and poses must be non-empty");
  }
  for (const ProfileControlPoint joint : puppet.source_joints) {
    if (!IsFinite(joint)) {
      return absl::InvalidArgumentError("layered puppet contains a non-finite source joint");
    }
  }
  for (const ProfileControlBone bone : puppet.bones) {
    if (bone.start_joint >= puppet.source_joints.size() ||
        bone.end_joint >= puppet.source_joints.size()) {
      return absl::InvalidArgumentError("layered puppet bone references an unknown joint");
    }
  }
  for (const LayeredPuppetPart& part : puppet.parts) {
    if (part.name.empty() || part.bone_index >= puppet.bones.size() || !part.artwork.IsValid() ||
        part.artwork.width != puppet.width || part.artwork.height != puppet.height) {
      return absl::InvalidArgumentError(
          "layered puppet part is incomplete or has wrong dimensions");
    }
  }
  for (size_t first = 0; first < puppet.parts.size(); ++first) {
    for (size_t second = first + 1; second < puppet.parts.size(); ++second) {
      if (puppet.parts[first].name == puppet.parts[second].name) {
        return absl::InvalidArgumentError("layered puppet part names must be unique");
      }
    }
  }
  for (const LayeredPuppetPose& pose : puppet.poses) {
    if (pose.name.empty() || pose.joints.size() != puppet.source_joints.size() ||
        pose.draw_order.size() != puppet.parts.size()) {
      return absl::InvalidArgumentError("layered puppet pose is incomplete");
    }
    std::vector<bool> seen(puppet.parts.size(), false);
    for (const ProfileControlPoint joint : pose.joints) {
      if (!IsFinite(joint)) {
        return absl::InvalidArgumentError("layered puppet pose contains a non-finite joint");
      }
    }
    for (const size_t part_index : pose.draw_order) {
      if (part_index >= puppet.parts.size() || seen[part_index]) {
        return absl::InvalidArgumentError("layered puppet pose must draw every part exactly once");
      }
      seen[part_index] = true;
    }
  }
  for (size_t first = 0; first < puppet.poses.size(); ++first) {
    for (size_t second = first + 1; second < puppet.poses.size(); ++second) {
      if (puppet.poses[first].name == puppet.poses[second].name) {
        return absl::InvalidArgumentError("layered puppet pose names must be unique");
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> RenderLayeredPuppetPose(const LayeredPuppet& puppet,
                                                  const LayeredPuppetPose& pose) {
  RETURN_IF_ERROR(ValidateLayeredPuppet(puppet));
  if (pose.joints.size() != puppet.source_joints.size() ||
      pose.draw_order.size() != puppet.parts.size()) {
    return absl::InvalidArgumentError("requested layered puppet pose is incomplete");
  }
  RgbaImage output{
      .width = puppet.width,
      .height = puppet.height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(puppet.width) * puppet.height * 4, 0),
  };
  for (const size_t part_index : pose.draw_order) {
    if (part_index >= puppet.parts.size()) {
      return absl::InvalidArgumentError("requested layered puppet pose references an unknown part");
    }
    RETURN_IF_ERROR(RenderPart(puppet, pose, puppet.parts[part_index], output));
  }
  return output;
}

absl::StatusOr<RgbaImage> DownsampleLayeredPuppetFrame(const RgbaImage& source, int frame_size,
                                                       double coverage_threshold) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("layered puppet downsample source is invalid");
  }
  if (frame_size <= 0 || !std::isfinite(coverage_threshold) || coverage_threshold <= 0.0 ||
      coverage_threshold > 1.0) {
    return absl::InvalidArgumentError("layered puppet downsample settings are invalid");
  }
  RgbaImage output{
      .width = frame_size,
      .height = frame_size,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(frame_size) * frame_size * 4, 0),
  };
  for (int target_y = 0; target_y < frame_size; ++target_y) {
    const int source_top = target_y * source.height / frame_size;
    const int source_bottom = static_cast<int>(
        std::ceil(static_cast<double>((target_y + 1) * source.height) / frame_size));
    for (int target_x = 0; target_x < frame_size; ++target_x) {
      const int source_left = target_x * source.width / frame_size;
      const int source_right = static_cast<int>(
          std::ceil(static_cast<double>((target_x + 1) * source.width) / frame_size));
      std::array<int64_t, 3> totals{};
      int opaque = 0;
      int samples = 0;
      for (int source_y = source_top; source_y < source_bottom; ++source_y) {
        for (int source_x = source_left; source_x < source_right; ++source_x) {
          ++samples;
          const size_t source_offset = PixelIndex(source.width, source_x, source_y) * 4;
          if (source.pixels[source_offset + 3] == 0) continue;
          ++opaque;
          for (int channel = 0; channel < 3; ++channel) {
            totals[channel] += source.pixels[source_offset + channel];
          }
        }
      }
      if (opaque == 0 || static_cast<double>(opaque) / samples < coverage_threshold) continue;
      const size_t target_offset = PixelIndex(frame_size, target_x, target_y) * 4;
      for (int channel = 0; channel < 3; ++channel) {
        output.pixels[target_offset + channel] = static_cast<uint8_t>(totals[channel] / opaque);
      }
      output.pixels[target_offset + 3] = 255;
    }
  }
  return output;
}

absl::StatusOr<RgbaImage> PackLayeredPuppetFrames(absl::Span<const RgbaImage> frames) {
  if (frames.empty() || !frames.front().IsValid()) {
    return absl::InvalidArgumentError("layered puppet frame set must be non-empty and valid");
  }
  const int frame_width = frames.front().width;
  const int frame_height = frames.front().height;
  if (frames.size() > static_cast<size_t>(std::numeric_limits<int>::max() / frame_width)) {
    return absl::ResourceExhaustedError("layered puppet frame strip is too wide");
  }
  const int output_width = frame_width * static_cast<int>(frames.size());
  RgbaImage output{
      .width = output_width,
      .height = frame_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(output_width) * frame_height * 4, 0),
  };
  for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const RgbaImage& frame = frames[frame_index];
    if (!frame.IsValid() || frame.width != frame_width || frame.height != frame_height) {
      return absl::InvalidArgumentError("layered puppet frames must have equal dimensions");
    }
    for (int y = 0; y < frame_height; ++y) {
      const size_t source_offset = static_cast<size_t>(y) * frame_width * 4;
      const size_t target_offset =
          (static_cast<size_t>(y) * output_width + frame_index * frame_width) * 4;
      std::copy_n(frame.pixels.begin() + static_cast<ptrdiff_t>(source_offset), frame_width * 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
    }
  }
  return output;
}

absl::StatusOr<RgbaImage> ZoomLayeredPuppetEvidence(const RgbaImage& source, int scale) {
  if (!source.IsValid() || scale <= 0 || source.width > std::numeric_limits<int>::max() / scale ||
      source.height > std::numeric_limits<int>::max() / scale) {
    return absl::InvalidArgumentError("layered puppet evidence zoom is invalid");
  }
  const int width = source.width * scale;
  const int height = source.height * scale;
  RgbaImage output{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0),
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t source_offset = PixelIndex(source.width, x / scale, y / scale) * 4;
      const size_t target_offset = PixelIndex(width, x, y) * 4;
      std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset), 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
    }
  }
  return output;
}

}  // namespace zebes
