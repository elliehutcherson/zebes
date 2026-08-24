#include "artwork/isolate_subject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

size_t PixelIndex(const RgbaImage& image, int x, int y) {
  return static_cast<size_t>(y) * image.width + x;
}

uint8_t BorderMedian(const RgbaImage& image, int channel) {
  std::array<int, 256> counts{};
  int samples = 0;
  const auto add = [&](int x, int y) {
    ++counts[image.pixels[PixelIndex(image, x, y) * 4 + channel]];
    ++samples;
  };
  for (int x = 0; x < image.width; ++x) {
    add(x, 0);
    if (image.height > 1) add(x, image.height - 1);
  }
  for (int y = 1; y + 1 < image.height; ++y) {
    add(0, y);
    if (image.width > 1) add(image.width - 1, y);
  }

  int accumulated = 0;
  for (int value = 0; value < static_cast<int>(counts.size()); ++value) {
    accumulated += counts[value];
    if (accumulated * 2 >= samples) return static_cast<uint8_t>(value);
  }
  return 0;
}

std::vector<int> ComponentAreas(const std::vector<uint8_t>& foreground, int width, int height) {
  std::vector<uint8_t> visited(foreground.size(), 0);
  std::vector<int> areas;
  std::queue<size_t> pending;
  for (size_t start = 0; start < foreground.size(); ++start) {
    if (foreground[start] == 0 || visited[start] != 0) continue;
    visited[start] = 1;
    pending.push(start);
    int area = 0;
    while (!pending.empty()) {
      const size_t pixel = pending.front();
      pending.pop();
      ++area;
      const int x = static_cast<int>(pixel % width);
      const int y = static_cast<int>(pixel / width);
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          const int neighbor_x = x + dx;
          const int neighbor_y = y + dy;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
            continue;
          }
          const size_t neighbor = static_cast<size_t>(neighbor_y) * width + neighbor_x;
          if (foreground[neighbor] == 0 || visited[neighbor] != 0) continue;
          visited[neighbor] = 1;
          pending.push(neighbor);
        }
      }
    }
    areas.push_back(area);
  }
  std::sort(areas.begin(), areas.end(), std::greater<>());
  return areas;
}

}  // namespace

absl::Status ValidateSubjectIsolationConfig(const SubjectIsolationConfig& config) {
  if (config.alpha_threshold < 0 || config.alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "subject isolation alpha threshold must be between 0 and 255");
  }
  if (!std::isfinite(config.background_distance) || config.background_distance < 0.0f) {
    return absl::InvalidArgumentError(
        "subject isolation background distance must be finite and non-negative");
  }
  if (!std::isfinite(config.enclosed_background_distance) ||
      config.enclosed_background_distance < 0.0f ||
      config.enclosed_background_distance > config.background_distance) {
    return absl::InvalidArgumentError(
        "subject isolation enclosed distance must be finite and no greater than background "
        "distance");
  }
  if (config.minimum_subject_area <= 0) {
    return absl::InvalidArgumentError("subject isolation minimum area must be positive");
  }
  if (!std::isfinite(config.competing_subject_ratio) || config.competing_subject_ratio < 0.0f ||
      config.competing_subject_ratio > 1.0f) {
    return absl::InvalidArgumentError(
        "subject isolation competing-subject ratio must be between 0 and 1");
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> IsolateSubject(const RgbaImage& source,
                                         const SubjectIsolationConfig& config) {
  if (!source.IsValid()) return absl::InvalidArgumentError("source image is invalid");
  RETURN_IF_ERROR(ValidateSubjectIsolationConfig(config));

  const size_t pixel_count = static_cast<size_t>(source.width) * source.height;
  size_t transparent = 0;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    if (source.pixels[pixel * 4 + 3] <= config.alpha_threshold) ++transparent;
  }
  const bool has_meaningful_alpha = transparent * 100 >= pixel_count;

  std::vector<uint8_t> foreground(pixel_count, 0);
  if (has_meaningful_alpha) {
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
      foreground[pixel] = source.pixels[pixel * 4 + 3] > config.alpha_threshold ? 1 : 0;
    }
  } else {
    const std::array<uint8_t, 3> background = {BorderMedian(source, 0), BorderMedian(source, 1),
                                               BorderMedian(source, 2)};
    const float threshold_squared = config.background_distance * config.background_distance;
    const float enclosed_threshold_squared =
        config.enclosed_background_distance * config.enclosed_background_distance;
    std::vector<uint8_t> background_candidate(pixel_count, 0);
    std::vector<float> background_distances(pixel_count, 0.0f);
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
      float distance_squared = 0.0f;
      for (int channel = 0; channel < 3; ++channel) {
        const float difference = static_cast<float>(source.pixels[pixel * 4 + channel]) -
                                 static_cast<float>(background[channel]);
        distance_squared += difference * difference;
      }
      background_distances[pixel] = distance_squared;
      background_candidate[pixel] = distance_squared <= threshold_squared ? 1 : 0;
    }

    std::vector<uint8_t> exterior(pixel_count, 0);
    std::queue<size_t> pending;
    const auto enqueue = [&](int x, int y) {
      const size_t pixel = PixelIndex(source, x, y);
      if (background_candidate[pixel] == 0 || exterior[pixel] != 0) return;
      exterior[pixel] = 1;
      pending.push(pixel);
    };
    for (int x = 0; x < source.width; ++x) {
      enqueue(x, 0);
      enqueue(x, source.height - 1);
    }
    for (int y = 0; y < source.height; ++y) {
      enqueue(0, y);
      enqueue(source.width - 1, y);
    }

    constexpr std::array<int, 4> kDx = {-1, 1, 0, 0};
    constexpr std::array<int, 4> kDy = {0, 0, -1, 1};
    while (!pending.empty()) {
      const size_t pixel = pending.front();
      pending.pop();
      const int x = static_cast<int>(pixel % source.width);
      const int y = static_cast<int>(pixel / source.width);
      for (size_t direction = 0; direction < kDx.size(); ++direction) {
        const int neighbor_x = x + kDx[direction];
        const int neighbor_y = y + kDy[direction];
        if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= source.width ||
            neighbor_y >= source.height) {
          continue;
        }
        enqueue(neighbor_x, neighbor_y);
      }
    }
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
      const bool enclosed_background = background_distances[pixel] <= enclosed_threshold_squared;
      foreground[pixel] = exterior[pixel] == 0 && !enclosed_background ? 1 : 0;
    }
  }

  const std::vector<int> areas = ComponentAreas(foreground, source.width, source.height);
  if (areas.empty() || areas.front() < config.minimum_subject_area) {
    return absl::FailedPreconditionError("subject isolation found no usable foreground");
  }
  if (areas.size() > 1 && static_cast<float>(areas[1]) >=
                              static_cast<float>(areas[0]) * config.competing_subject_ratio) {
    return absl::FailedPreconditionError(absl::StrCat(
        "subject isolation found competing components of area ", areas[0], " and ", areas[1]));
  }

  for (int x = 0; x < source.width; ++x) {
    if (foreground[PixelIndex(source, x, 0)] != 0 ||
        foreground[PixelIndex(source, x, source.height - 1)] != 0) {
      return absl::FailedPreconditionError("isolated subject touches the image border");
    }
  }
  for (int y = 0; y < source.height; ++y) {
    if (foreground[PixelIndex(source, 0, y)] != 0 ||
        foreground[PixelIndex(source, source.width - 1, y)] != 0) {
      return absl::FailedPreconditionError("isolated subject touches the image border");
    }
  }

  RgbaImage isolated = source;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    if (foreground[pixel] != 0) {
      if (!has_meaningful_alpha) isolated.pixels[pixel * 4 + 3] = 255;
      continue;
    }
    std::fill_n(isolated.pixels.begin() + static_cast<ptrdiff_t>(pixel * 4), 4, 0);
  }
  return isolated;
}

}  // namespace zebes
