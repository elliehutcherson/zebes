#include "artwork/cleanup_prop.h"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

struct Component {
  std::vector<size_t> pixels;
};

std::vector<Component> FindComponents(const RgbaImage& image) {
  const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
  std::vector<uint8_t> visited(pixel_count, 0);
  std::vector<Component> components;
  std::queue<size_t> pending;
  for (size_t start = 0; start < pixel_count; ++start) {
    if (visited[start] != 0 || image.pixels[start * 4 + 3] == 0) continue;
    visited[start] = 1;
    pending.push(start);
    Component component;
    while (!pending.empty()) {
      const size_t pixel = pending.front();
      pending.pop();
      component.pixels.push_back(pixel);
      const int x = static_cast<int>(pixel % image.width);
      const int y = static_cast<int>(pixel / image.width);
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          const int neighbor_x = x + dx;
          const int neighbor_y = y + dy;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= image.width ||
              neighbor_y >= image.height) {
            continue;
          }
          const size_t neighbor = static_cast<size_t>(neighbor_y) * image.width + neighbor_x;
          if (visited[neighbor] != 0 || image.pixels[neighbor * 4 + 3] == 0) continue;
          visited[neighbor] = 1;
          pending.push(neighbor);
        }
      }
    }
    components.push_back(std::move(component));
  }
  std::sort(components.begin(), components.end(),
            [](const Component& left, const Component& right) {
              return left.pixels.size() > right.pixels.size();
            });
  return components;
}

bool PaletteContains(absl::Span<const RgbaColor> palette, const RgbaColor& candidate) {
  for (const RgbaColor& color : palette) {
    if (color == candidate) return true;
  }
  return false;
}

}  // namespace

absl::StatusOr<PropArtwork> CleanupAndValidateProp(const PropArtwork& artwork,
                                                   absl::Span<const RgbaColor> palette,
                                                   int tile_size, const PropCleanupConfig& config) {
  if (!artwork.IsValid()) return absl::InvalidArgumentError("prop artwork is invalid");
  if (palette.empty() || config.alpha_threshold < 0 || config.alpha_threshold > 255 ||
      config.minimum_component_area <= 0 || tile_size <= 0 || config.grounded_tolerance < 0) {
    return absl::InvalidArgumentError("prop cleanup settings are invalid");
  }
  if (artwork.image.width % tile_size != 0 || artwork.image.height % tile_size != 0) {
    return absl::FailedPreconditionError("prop canvas is not a whole number of tiles");
  }

  PropArtwork cleaned = artwork;
  const size_t pixel_count = static_cast<size_t>(cleaned.image.width) * cleaned.image.height;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * 4;
    if (cleaned.image.pixels[offset + 3] >= config.alpha_threshold) {
      cleaned.image.pixels[offset + 3] = 255;
      continue;
    }
    std::fill_n(cleaned.image.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
  }

  std::vector<Component> components = FindComponents(cleaned.image);
  if (components.empty()) return absl::FailedPreconditionError("cleaned prop has no subject");
  if (components.front().pixels.size() < static_cast<size_t>(config.minimum_component_area)) {
    return absl::FailedPreconditionError("cleaned prop has no component meeting the minimum area");
  }
  for (size_t index = 0; index < components.size(); ++index) {
    const Component& component = components[index];
    if (component.pixels.size() >= static_cast<size_t>(config.minimum_component_area)) {
      if (index > 0) {
        return absl::FailedPreconditionError(
            absl::StrCat("cleaned prop has a second substantial component of ",
                         component.pixels.size(), " pixels"));
      }
      continue;
    }
    for (const size_t pixel : component.pixels) {
      std::fill_n(cleaned.image.pixels.begin() + static_cast<ptrdiff_t>(pixel * 4), 4, 0);
    }
  }

  bool grounded = false;
  for (int dy = -config.grounded_tolerance; dy <= config.grounded_tolerance; ++dy) {
    for (int dx = -config.grounded_tolerance; dx <= config.grounded_tolerance; ++dx) {
      const int x = cleaned.anchor_x + dx;
      const int y = cleaned.anchor_y + dy;
      if (x < 0 || y < 0 || x >= cleaned.image.width || y >= cleaned.image.height) continue;
      const size_t pixel = (static_cast<size_t>(y) * cleaned.image.width + x) * 4;
      if (cleaned.image.pixels[pixel + 3] == 255) grounded = true;
    }
  }
  if (!grounded) {
    return absl::FailedPreconditionError("prop anchor is not grounded on an opaque pixel");
  }

  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * 4;
    const uint8_t alpha = cleaned.image.pixels[offset + 3];
    if (alpha == 0) {
      if (cleaned.image.pixels[offset + 0] != 0 || cleaned.image.pixels[offset + 1] != 0 ||
          cleaned.image.pixels[offset + 2] != 0) {
        return absl::InternalError("transparent prop pixel retained RGB data");
      }
      continue;
    }
    if (alpha != 255) return absl::InternalError("cleaned prop retained partial alpha");
    const RgbaColor color{
        cleaned.image.pixels[offset + 0],
        cleaned.image.pixels[offset + 1],
        cleaned.image.pixels[offset + 2],
        255,
    };
    if (!PaletteContains(palette, color)) {
      return absl::InternalError("cleaned prop contains a colour outside its palette");
    }
  }
  return cleaned;
}

}  // namespace zebes
