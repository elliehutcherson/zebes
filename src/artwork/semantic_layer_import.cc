#include "artwork/semantic_layer_import.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace zebes {
namespace {

size_t PixelIndex(int width, int x, int y) { return static_cast<size_t>(y) * width + x; }

struct ComponentIndices {
  std::vector<size_t> pixels;
  int minimum_x = 0;
  int minimum_y = 0;
  int maximum_x = 0;
  int maximum_y = 0;
};

}  // namespace

absl::StatusOr<RgbaImage> RestoreSemanticLayer(const RgbaImage& cropped,
                                               const SemanticLayerCrop& crop, int canvas_width,
                                               int canvas_height) {
  if (!cropped.IsValid()) {
    return absl::InvalidArgumentError("semantic layer crop image is invalid");
  }
  if (canvas_width <= 0 || canvas_height <= 0 || crop.x < 0 || crop.y < 0 ||
      crop.width != cropped.width || crop.height != cropped.height ||
      crop.x > canvas_width - crop.width || crop.y > canvas_height - crop.height) {
    return absl::InvalidArgumentError("semantic layer crop does not fit its declared canvas");
  }

  RgbaImage restored{
      .width = canvas_width,
      .height = canvas_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(canvas_width) * canvas_height * 4, 0),
  };
  for (int y = 0; y < cropped.height; ++y) {
    const size_t source_offset = static_cast<size_t>(y) * cropped.width * 4;
    const size_t target_offset = (static_cast<size_t>(crop.y + y) * canvas_width + crop.x) * 4;
    std::copy_n(cropped.pixels.begin() + static_cast<ptrdiff_t>(source_offset), cropped.width * 4,
                restored.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
  }
  return restored;
}

absl::StatusOr<RgbaImage> DownsampleSemanticLayer(const RgbaImage& source, int target_width,
                                                  int target_height, double coverage_threshold) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("semantic layer downsample source is invalid");
  }
  if (target_width <= 0 || target_height <= 0 || source.width % target_width != 0 ||
      source.height % target_height != 0 || !std::isfinite(coverage_threshold) ||
      coverage_threshold <= 0.0 || coverage_threshold > 1.0) {
    return absl::InvalidArgumentError(
        "semantic layer downsample requires exact positive integer scale and coverage");
  }
  const int scale_x = source.width / target_width;
  const int scale_y = source.height / target_height;
  const int64_t maximum_alpha = static_cast<int64_t>(scale_x) * scale_y * 255;
  RgbaImage output{
      .width = target_width,
      .height = target_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(target_width) * target_height * 4, 0),
  };

  for (int target_y = 0; target_y < target_height; ++target_y) {
    for (int target_x = 0; target_x < target_width; ++target_x) {
      std::array<int64_t, 3> premultiplied{};
      int64_t alpha_total = 0;
      for (int offset_y = 0; offset_y < scale_y; ++offset_y) {
        for (int offset_x = 0; offset_x < scale_x; ++offset_x) {
          const int source_x = target_x * scale_x + offset_x;
          const int source_y = target_y * scale_y + offset_y;
          const size_t source_offset = PixelIndex(source.width, source_x, source_y) * 4;
          const int alpha = source.pixels[source_offset + 3];
          alpha_total += alpha;
          for (int channel = 0; channel < 3; ++channel) {
            premultiplied[channel] += source.pixels[source_offset + channel] * alpha;
          }
        }
      }
      if (static_cast<double>(alpha_total) / maximum_alpha < coverage_threshold) continue;
      const size_t target_offset = PixelIndex(target_width, target_x, target_y) * 4;
      for (int channel = 0; channel < 3; ++channel) {
        output.pixels[target_offset + channel] =
            static_cast<uint8_t>(premultiplied[channel] / alpha_total);
      }
      output.pixels[target_offset + 3] = 255;
    }
  }
  return output;
}

absl::StatusOr<RgbaImage> PreserveSemanticVisiblePixels(const RgbaImage& candidate,
                                                        const RgbaImage& source,
                                                        const RgbaImage& visible_mask) {
  if (!candidate.IsValid() || !source.IsValid() || !visible_mask.IsValid() ||
      candidate.width != source.width || candidate.height != source.height ||
      visible_mask.width != source.width || visible_mask.height != source.height) {
    return absl::InvalidArgumentError(
        "semantic candidate, source, and visible mask must share valid dimensions");
  }
  RgbaImage result = candidate;
  for (size_t offset = 0; offset < source.pixels.size(); offset += 4) {
    if (visible_mask.pixels[offset + 3] == 0) continue;
    if (source.pixels[offset + 3] == 0) {
      return absl::InvalidArgumentError("semantic visible mask selects a transparent source pixel");
    }
    std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                result.pixels.begin() + static_cast<ptrdiff_t>(offset));
  }
  return result;
}

absl::StatusOr<RgbaImage> ClipSemanticLayerToMask(const RgbaImage& candidate,
                                                  const RgbaImage& allowed_mask) {
  if (!candidate.IsValid() || !allowed_mask.IsValid() || candidate.width != allowed_mask.width ||
      candidate.height != allowed_mask.height) {
    return absl::InvalidArgumentError(
        "semantic candidate and allowed mask must share valid dimensions");
  }
  RgbaImage result = candidate;
  for (size_t offset = 0; offset < result.pixels.size(); offset += 4) {
    if (allowed_mask.pixels[offset + 3] != 0) continue;
    std::fill_n(result.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
  }
  return result;
}

absl::StatusOr<SemanticVisibleOwnership> MeasureSemanticVisibleOwnership(
    const RgbaImage& source, absl::Span<const RgbaImage> visible_layers) {
  if (!source.IsValid() || visible_layers.empty()) {
    return absl::InvalidArgumentError(
        "semantic ownership requires a valid source and visible layers");
  }
  for (const RgbaImage& layer : visible_layers) {
    if (!layer.IsValid() || layer.width != source.width || layer.height != source.height) {
      return absl::InvalidArgumentError("semantic visible ownership layer dimensions differ");
    }
  }
  SemanticVisibleOwnership result;
  for (size_t offset = 0; offset < source.pixels.size(); offset += 4) {
    size_t owners = 0;
    for (const RgbaImage& layer : visible_layers) {
      if (layer.pixels[offset + 3] != 0) ++owners;
    }
    if (source.pixels[offset + 3] == 0) {
      if (owners != 0) ++result.ownership_outside_source_pixels;
      continue;
    }
    ++result.source_pixels;
    if (owners == 0) {
      ++result.unowned_pixels;
    } else if (owners == 1) {
      ++result.singly_owned_pixels;
    } else {
      ++result.multiply_owned_pixels;
    }
  }
  return result;
}

absl::StatusOr<SemanticLayerMutation> MeasureSemanticLayerMutation(const RgbaImage& source,
                                                                   const RgbaImage& processed) {
  if (!source.IsValid() || !processed.IsValid() || source.width != processed.width ||
      source.height != processed.height) {
    return absl::InvalidArgumentError("semantic mutation images must share valid dimensions");
  }
  SemanticLayerMutation result;
  for (size_t offset = 0; offset < source.pixels.size(); offset += 4) {
    if (!std::equal(source.pixels.begin() + static_cast<ptrdiff_t>(offset),
                    source.pixels.begin() + static_cast<ptrdiff_t>(offset + 4),
                    processed.pixels.begin() + static_cast<ptrdiff_t>(offset))) {
      ++result.changed_pixels;
    }
    const bool before = source.pixels[offset + 3] != 0;
    const bool after = processed.pixels[offset + 3] != 0;
    if (!before && after) ++result.alpha_added_pixels;
    if (before && !after) ++result.alpha_removed_pixels;
  }
  return result;
}

absl::StatusOr<std::vector<SemanticLayerComponent>> SplitSemanticLayerComponents(
    const RgbaImage& source, size_t minimum_pixels) {
  if (!source.IsValid() || minimum_pixels == 0) {
    return absl::InvalidArgumentError(
        "semantic component split requires a valid image and positive minimum size");
  }
  const size_t pixel_count = static_cast<size_t>(source.width) * source.height;
  std::vector<bool> visited(pixel_count, false);
  std::vector<ComponentIndices> components;
  for (size_t start = 0; start < pixel_count; ++start) {
    if (visited[start] || source.pixels[start * 4 + 3] == 0) continue;
    ComponentIndices component{
        .minimum_x = source.width,
        .minimum_y = source.height,
        .maximum_x = 0,
        .maximum_y = 0,
    };
    std::deque<size_t> pending;
    pending.push_back(start);
    visited[start] = true;
    while (!pending.empty()) {
      const size_t index = pending.front();
      pending.pop_front();
      component.pixels.push_back(index);
      const int x = static_cast<int>(index % source.width);
      const int y = static_cast<int>(index / source.width);
      component.minimum_x = std::min(component.minimum_x, x);
      component.minimum_y = std::min(component.minimum_y, y);
      component.maximum_x = std::max(component.maximum_x, x + 1);
      component.maximum_y = std::max(component.maximum_y, y + 1);
      const std::array<std::pair<int, int>, 4> neighbors = {
          std::pair{x - 1, y}, std::pair{x + 1, y}, std::pair{x, y - 1}, std::pair{x, y + 1}};
      for (const auto [neighbor_x, neighbor_y] : neighbors) {
        if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= source.width ||
            neighbor_y >= source.height) {
          continue;
        }
        const size_t neighbor = PixelIndex(source.width, neighbor_x, neighbor_y);
        if (visited[neighbor] || source.pixels[neighbor * 4 + 3] == 0) continue;
        visited[neighbor] = true;
        pending.push_back(neighbor);
      }
    }
    if (component.pixels.size() >= minimum_pixels) {
      components.push_back(std::move(component));
    }
  }

  std::sort(components.begin(), components.end(),
            [](const ComponentIndices& first, const ComponentIndices& second) {
              if (first.minimum_x != second.minimum_x) return first.minimum_x < second.minimum_x;
              return first.minimum_y < second.minimum_y;
            });
  std::vector<SemanticLayerComponent> result;
  result.reserve(components.size());
  for (const ComponentIndices& component : components) {
    RgbaImage artwork{
        .width = source.width,
        .height = source.height,
        .pixels = std::vector<uint8_t>(source.pixels.size(), 0),
    };
    for (const size_t index : component.pixels) {
      const size_t offset = index * 4;
      std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(offset), 4,
                  artwork.pixels.begin() + static_cast<ptrdiff_t>(offset));
    }
    result.push_back({
        .artwork = std::move(artwork),
        .minimum_x = component.minimum_x,
        .minimum_y = component.minimum_y,
        .maximum_x = component.maximum_x,
        .maximum_y = component.maximum_y,
        .pixel_count = component.pixels.size(),
    });
  }
  return result;
}

}  // namespace zebes
