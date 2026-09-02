#include "artwork/profile_silhouette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

size_t PixelIndex(int width, int x, int y) { return static_cast<size_t>(y) * width + x; }

int NeighborIndices(const std::vector<uint8_t>& mask, int width, int height, size_t index,
                    std::array<size_t, 8>* neighbors) {
  const int x = static_cast<int>(index % width);
  const int y = static_cast<int>(index / width);
  int count = 0;
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) continue;
      const int neighbor_x = x + offset_x;
      const int neighbor_y = y + offset_y;
      if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
        continue;
      }
      const size_t neighbor = PixelIndex(width, neighbor_x, neighbor_y);
      if (mask[neighbor] == 0) continue;
      (*neighbors)[count++] = neighbor;
    }
  }
  return count;
}

int NeighborCount(const std::vector<uint8_t>& mask, int width, int height, size_t index) {
  std::array<size_t, 8> neighbors{};
  return NeighborIndices(mask, width, height, index, &neighbors);
}

absl::StatusOr<std::vector<uint8_t>> ReduceAlphaMask(const RgbaImage& isolated,
                                                     const ProfileSilhouetteConfig& config) {
  const int scale = isolated.width / config.working_size;
  const int block_area = scale * scale;
  const int required_coverage = std::max(1, block_area / 3);
  const size_t output_pixels = static_cast<size_t>(config.working_size) * config.working_size;
  std::vector<uint8_t> output(output_pixels, 0);

  int foreground_pixels = 0;
  for (int output_y = 0; output_y < config.working_size; ++output_y) {
    for (int output_x = 0; output_x < config.working_size; ++output_x) {
      int covered = 0;
      for (int source_y = output_y * scale; source_y < (output_y + 1) * scale; ++source_y) {
        for (int source_x = output_x * scale; source_x < (output_x + 1) * scale; ++source_x) {
          const size_t source = PixelIndex(isolated.width, source_x, source_y);
          if (isolated.pixels[source * 4 + 3] > config.alpha_threshold) {
            ++covered;
          }
        }
      }
      if (covered < required_coverage) continue;
      output[PixelIndex(config.working_size, output_x, output_y)] = 1;
      ++foreground_pixels;
    }
  }
  if (foreground_pixels == 0) {
    return absl::FailedPreconditionError("profile silhouette has no foreground after reduction");
  }
  return output;
}

absl::StatusOr<std::vector<uint8_t>> ThinMask(const std::vector<uint8_t>& silhouette, int width,
                                              int height) {
  std::vector<uint8_t> work = silhouette;
  bool changed = true;
  int iterations = 0;
  std::vector<size_t> remove;
  remove.reserve(work.size() / 8);

  while (changed) {
    changed = false;
    ++iterations;
    for (int step = 0; step < 2; ++step) {
      remove.clear();
      for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
          const size_t index = PixelIndex(width, x, y);
          if (work[index] == 0) continue;
          const std::array<uint8_t, 8> neighbors = {
              work[index - width],     work[index - width + 1], work[index + 1],
              work[index + width + 1], work[index + width],     work[index + width - 1],
              work[index - 1],         work[index - width - 1],
          };
          const int neighbor_count = std::count(neighbors.begin(), neighbors.end(), uint8_t{1});
          if (neighbor_count < 2 || neighbor_count > 6) continue;

          int transitions = 0;
          for (size_t neighbor = 0; neighbor < neighbors.size(); ++neighbor) {
            if (neighbors[neighbor] == 0 && neighbors[(neighbor + 1) % neighbors.size()] != 0) {
              ++transitions;
            }
          }
          if (transitions != 1) continue;

          const uint8_t p2 = neighbors[0];
          const uint8_t p4 = neighbors[2];
          const uint8_t p6 = neighbors[4];
          const uint8_t p8 = neighbors[6];
          if (step == 0 && ((p2 != 0 && p4 != 0 && p6 != 0) || (p4 != 0 && p6 != 0 && p8 != 0))) {
            continue;
          }
          if (step == 1 && ((p2 != 0 && p4 != 0 && p8 != 0) || (p2 != 0 && p6 != 0 && p8 != 0))) {
            continue;
          }
          remove.push_back(index);
        }
      }
      if (remove.empty()) continue;
      changed = true;
      for (const size_t index : remove) work[index] = 0;
    }
    if (iterations > std::max(width, height)) {
      return absl::InternalError("profile medial-axis thinning did not converge");
    }
  }
  return work;
}

void PruneShortBranches(std::vector<uint8_t>* medial_axis, int width, int height,
                        int minimum_length) {
  if (minimum_length <= 0) return;
  std::vector<size_t> endpoints;
  std::vector<size_t> path;
  path.reserve(static_cast<size_t>(minimum_length) + 1);

  for (int pass = 0; pass < 4; ++pass) {
    endpoints.clear();
    for (size_t index = 0; index < medial_axis->size(); ++index) {
      if ((*medial_axis)[index] != 0 && NeighborCount(*medial_axis, width, height, index) == 1) {
        endpoints.push_back(index);
      }
    }

    bool changed = false;
    for (const size_t endpoint : endpoints) {
      if ((*medial_axis)[endpoint] == 0) continue;
      path.clear();
      path.push_back(endpoint);
      size_t previous = medial_axis->size();
      size_t current = endpoint;
      while (static_cast<int>(path.size()) <= minimum_length) {
        std::array<size_t, 8> neighbors{};
        const int neighbor_count =
            NeighborIndices(*medial_axis, width, height, current, &neighbors);
        size_t next = medial_axis->size();
        int following = 0;
        for (int neighbor = 0; neighbor < neighbor_count; ++neighbor) {
          if (neighbors[neighbor] == previous) continue;
          next = neighbors[neighbor];
          ++following;
        }
        if (following != 1) break;
        previous = current;
        current = next;
        path.push_back(current);
      }
      if (static_cast<int>(path.size()) > minimum_length ||
          NeighborCount(*medial_axis, width, height, current) < 3) {
        continue;
      }
      for (size_t path_index = 0; path_index + 1 < path.size(); ++path_index) {
        (*medial_axis)[path[path_index]] = 0;
        changed = true;
      }
    }
    if (!changed) return;
  }
}

int CountComponents(const std::vector<uint8_t>& mask, int width, int height) {
  std::vector<uint8_t> visited(mask.size(), 0);
  std::queue<size_t> pending;
  int components = 0;
  for (size_t start = 0; start < mask.size(); ++start) {
    if (mask[start] == 0 || visited[start] != 0) continue;
    ++components;
    visited[start] = 1;
    pending.push(start);
    while (!pending.empty()) {
      const size_t current = pending.front();
      pending.pop();
      std::array<size_t, 8> neighbors{};
      const int neighbor_count = NeighborIndices(mask, width, height, current, &neighbors);
      for (int neighbor = 0; neighbor < neighbor_count; ++neighbor) {
        const size_t index = neighbors[neighbor];
        if (visited[index] != 0) continue;
        visited[index] = 1;
        pending.push(index);
      }
    }
  }
  return components;
}

bool IsMaskEdge(absl::Span<const uint8_t> silhouette, int width, int height, size_t index) {
  if (silhouette[index] == 0) return false;
  const int x = static_cast<int>(index % width);
  const int y = static_cast<int>(index / width);
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
      const int neighbor_x = x + offset_x;
      const int neighbor_y = y + offset_y;
      if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
        return true;
      }
      if (silhouette[PixelIndex(width, neighbor_x, neighbor_y)] == 0) {
        return true;
      }
    }
  }
  return false;
}

void PaintWhite(RgbaImage* image, int x, int y) {
  if (x < 0 || y < 0 || x >= image->width || y >= image->height) return;
  const size_t pixel = PixelIndex(image->width, x, y) * 4;
  image->pixels[pixel] = 255;
  image->pixels[pixel + 1] = 255;
  image->pixels[pixel + 2] = 255;
  image->pixels[pixel + 3] = 255;
}

void DrawWhiteLine(RgbaImage* image, ProfileControlPoint start, ProfileControlPoint end) {
  int x0 = static_cast<int>(std::lround(start.x));
  int y0 = static_cast<int>(std::lround(start.y));
  const int x1 = static_cast<int>(std::lround(end.x));
  const int y1 = static_cast<int>(std::lround(end.y));
  const int delta_x = std::abs(x1 - x0);
  const int delta_y = -std::abs(y1 - y0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int step_y = y0 < y1 ? 1 : -1;
  int error = delta_x + delta_y;
  while (true) {
    PaintWhite(image, x0, y0);
    if (x0 == x1 && y0 == y1) return;
    const int doubled_error = 2 * error;
    if (doubled_error >= delta_y) {
      error += delta_y;
      x0 += step_x;
    }
    if (doubled_error <= delta_x) {
      error += delta_x;
      y0 += step_y;
    }
  }
}

}  // namespace

bool ProfileSilhouette::IsValid() const {
  if (width <= 0 || height <= 0 || source_scale <= 0) return false;
  const size_t pixel_count = static_cast<size_t>(width) * height;
  return silhouette.size() == pixel_count && medial_axis.size() == pixel_count;
}

absl::Status ValidateProfileSilhouetteConfig(const ProfileSilhouetteConfig& config) {
  if (config.working_size <= 0) {
    return absl::InvalidArgumentError("profile silhouette working size must be positive");
  }
  if (config.alpha_threshold < 0 || config.alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "profile silhouette alpha threshold must be between 0 and 255");
  }
  if (config.minimum_branch_length < 0) {
    return absl::InvalidArgumentError(
        "profile silhouette minimum branch length must be non-negative");
  }
  return absl::OkStatus();
}

absl::StatusOr<ProfileSilhouette> ExtractProfileSilhouette(const RgbaImage& isolated,
                                                           const ProfileSilhouetteConfig& config) {
  if (!isolated.IsValid()) {
    return absl::InvalidArgumentError("profile silhouette source image is invalid");
  }
  RETURN_IF_ERROR(ValidateProfileSilhouetteConfig(config));
  if (isolated.width != isolated.height) {
    return absl::InvalidArgumentError("profile silhouette source image must be square");
  }
  if (isolated.width < config.working_size) {
    return absl::InvalidArgumentError(absl::StrCat("profile silhouette source width ",
                                                   isolated.width, " is smaller than working size ",
                                                   config.working_size));
  }
  if (isolated.width % config.working_size != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("profile silhouette source width ", isolated.width,
                     " is not an integer multiple of working size ", config.working_size));
  }

  ASSIGN_OR_RETURN(std::vector<uint8_t> silhouette, ReduceAlphaMask(isolated, config));
  ASSIGN_OR_RETURN(std::vector<uint8_t> medial_axis,
                   ThinMask(silhouette, config.working_size, config.working_size));
  PruneShortBranches(&medial_axis, config.working_size, config.working_size,
                     config.minimum_branch_length);

  ProfileSilhouette profile{
      .width = config.working_size,
      .height = config.working_size,
      .source_scale = isolated.width / config.working_size,
      .silhouette = std::move(silhouette),
      .medial_axis = std::move(medial_axis),
  };
  profile.silhouette_pixels = static_cast<int>(
      std::count(profile.silhouette.begin(), profile.silhouette.end(), uint8_t{1}));
  profile.medial_axis_pixels = static_cast<int>(
      std::count(profile.medial_axis.begin(), profile.medial_axis.end(), uint8_t{1}));
  profile.component_count = CountComponents(profile.medial_axis, profile.width, profile.height);
  for (size_t index = 0; index < profile.medial_axis.size(); ++index) {
    if (profile.medial_axis[index] == 0) continue;
    const int neighbors = NeighborCount(profile.medial_axis, profile.width, profile.height, index);
    profile.endpoint_count += neighbors == 1 ? 1 : 0;
    profile.branch_pixel_count += neighbors >= 3 ? 1 : 0;
  }
  if (profile.medial_axis_pixels == 0) {
    return absl::FailedPreconditionError("profile silhouette produced an empty medial axis");
  }
  return profile;
}

absl::StatusOr<RgbaImage> RenderProfileSilhouetteEvidence(const ProfileSilhouette& profile) {
  if (!profile.IsValid()) {
    return absl::InvalidArgumentError("profile silhouette evidence is invalid");
  }
  RgbaImage evidence{
      .width = profile.width,
      .height = profile.height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(profile.width) * profile.height * 4, 0),
  };
  for (size_t index = 0; index < profile.silhouette.size(); ++index) {
    const size_t pixel = index * 4;
    evidence.pixels[pixel + 3] = 255;
    if (profile.silhouette[index] != 0) {
      evidence.pixels[pixel] = 208;
      evidence.pixels[pixel + 1] = 208;
      evidence.pixels[pixel + 2] = 208;
    }
    if (profile.medial_axis[index] == 0) continue;
    evidence.pixels[pixel] = 255;
    evidence.pixels[pixel + 1] = 70;
    evidence.pixels[pixel + 2] = 70;
  }
  return evidence;
}

absl::StatusOr<RgbaImage> RenderProfileSilhouetteControl(const ProfileSilhouette& profile) {
  if (!profile.IsValid()) {
    return absl::InvalidArgumentError("profile silhouette control is invalid");
  }
  RgbaImage control{
      .width = profile.width,
      .height = profile.height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(profile.width) * profile.height * 4, 0),
  };
  for (size_t index = 0; index < profile.silhouette.size(); ++index) {
    const size_t pixel = index * 4;
    control.pixels[pixel + 3] = 255;
    if (!IsMaskEdge(profile.silhouette, profile.width, profile.height, index) &&
        profile.medial_axis[index] == 0) {
      continue;
    }
    control.pixels[pixel] = 255;
    control.pixels[pixel + 1] = 255;
    control.pixels[pixel + 2] = 255;
  }
  return control;
}

absl::StatusOr<RgbaImage> RenderProfilePoseControl(absl::Span<const uint8_t> silhouette, int width,
                                                   int height,
                                                   absl::Span<const ProfileControlPoint> joints,
                                                   absl::Span<const ProfileControlBone> bones) {
  if (width <= 0 || height <= 0 || silhouette.size() != static_cast<size_t>(width) * height) {
    return absl::InvalidArgumentError("profile pose control silhouette dimensions are invalid");
  }
  for (const ProfileControlPoint& joint : joints) {
    if (!std::isfinite(joint.x) || !std::isfinite(joint.y)) {
      return absl::InvalidArgumentError("profile pose control contains a non-finite joint");
    }
  }
  for (const ProfileControlBone& bone : bones) {
    if (bone.start_joint >= joints.size() || bone.end_joint >= joints.size()) {
      return absl::InvalidArgumentError("profile pose control bone references an unknown joint");
    }
  }

  RgbaImage control{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0),
  };
  for (size_t index = 0; index < silhouette.size(); ++index) {
    control.pixels[index * 4 + 3] = 255;
    if (IsMaskEdge(silhouette, width, height, index)) {
      PaintWhite(&control, static_cast<int>(index % width), static_cast<int>(index / width));
    }
  }
  for (const ProfileControlBone& bone : bones) {
    const ProfileControlPoint start = joints[bone.start_joint];
    const ProfileControlPoint end = joints[bone.end_joint];
    for (const std::array<int, 2>& offset :
         {std::array<int, 2>{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
      DrawWhiteLine(&control, {start.x + offset[0], start.y + offset[1]},
                    {end.x + offset[0], end.y + offset[1]});
    }
  }
  for (const ProfileControlPoint& joint : joints) {
    const int center_x = static_cast<int>(std::lround(joint.x));
    const int center_y = static_cast<int>(std::lround(joint.y));
    for (int offset_y = -2; offset_y <= 2; ++offset_y) {
      for (int offset_x = -2; offset_x <= 2; ++offset_x) {
        if (offset_x * offset_x + offset_y * offset_y > 4) continue;
        PaintWhite(&control, center_x + offset_x, center_y + offset_y);
      }
    }
  }
  return control;
}

}  // namespace zebes
