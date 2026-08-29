#include "editor/prop_artwork_editor/prop_artwork_context.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "terrain/terrain_generator.h"

namespace zebes {
namespace {

constexpr int kCheckerSize = 8;
constexpr int kMaximumPreviewDimension = 16384;

struct ContextAnchor {
  int x = 0;
  int y = 0;
};

int AlignDown(int value, int step) {
  if (value >= 0) return (value / step) * step;
  return -(((-value + step - 1) / step) * step);
}

int AlignUp(int value, int step) { return ((value + step - 1) / step) * step; }

void BlendPixel(const uint8_t* source, uint8_t* target) {
  const int source_alpha = source[3];
  if (source_alpha == 0) return;
  if (source_alpha == 255) {
    std::copy_n(source, 4, target);
    return;
  }

  const int inverse = 255 - source_alpha;
  for (int channel = 0; channel < 3; ++channel) {
    target[channel] = static_cast<uint8_t>(
        (source[channel] * source_alpha + target[channel] * inverse + 127) / 255);
  }
  target[3] = 255;
}

void Composite(const RgbaImage& source, int target_x, int target_y, RgbaImage& target) {
  for (int y = 0; y < source.height; ++y) {
    const int output_y = target_y + y;
    if (output_y < 0 || output_y >= target.height) continue;
    for (int x = 0; x < source.width; ++x) {
      const int output_x = target_x + x;
      if (output_x < 0 || output_x >= target.width) continue;
      const size_t source_offset = (static_cast<size_t>(y) * source.width + x) * 4;
      const size_t target_offset = (static_cast<size_t>(output_y) * target.width + output_x) * 4;
      BlendPixel(source.pixels.data() + source_offset, target.pixels.data() + target_offset);
    }
  }
}

RgbaImage Checkerboard(int width, int height) {
  RgbaImage image{.width = width, .height = height};
  image.pixels.resize(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t shade = ((x / kCheckerSize) + (y / kCheckerSize)) % 2 == 0 ? 42 : 52;
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      image.pixels[offset + 0] = shade;
      image.pixels[offset + 1] = shade;
      image.pixels[offset + 2] = shade;
      image.pixels[offset + 3] = 255;
    }
  }
  return image;
}

RgbaImage FlipVertical(const RgbaImage& source) {
  RgbaImage flipped{.width = source.width, .height = source.height};
  flipped.pixels.resize(source.pixels.size());
  for (int y = 0; y < source.height; ++y) {
    const size_t source_offset = static_cast<size_t>(y) * source.width * 4;
    const size_t target_offset = static_cast<size_t>(source.height - 1 - y) * source.width * 4;
    std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset), source.width * 4,
                flipped.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
  }
  return flipped;
}

std::optional<int> SurfaceY(const PropArtworkContextPreview& preview, int preview_x) {
  const int terrain_x = preview_x - preview.terrain_left;
  if (terrain_x < 0 || terrain_x >= preview.terrain.width) return std::nullopt;

  if (preview.attachment_mode == PropAttachmentMode::kGrounded) {
    for (int y = 0; y < preview.terrain.height; ++y) {
      const size_t offset = (static_cast<size_t>(y) * preview.terrain.width + terrain_x) * 4;
      if (preview.terrain.pixels[offset + 3] != 0) return preview.terrain_top + y;
    }
    return std::nullopt;
  }
  if (preview.attachment_mode == PropAttachmentMode::kCeiling) {
    for (int y = preview.terrain.height - 1; y >= 0; --y) {
      const size_t offset = (static_cast<size_t>(y) * preview.terrain.width + terrain_x) * 4;
      if (preview.terrain.pixels[offset + 3] != 0) return preview.terrain_top + y;
    }
  }
  return std::nullopt;
}

std::optional<ContextAnchor> NearestSurfaceAnchor(const PropArtwork& prop, int requested_x,
                                                  const PropArtworkContextPreview& preview) {
  const int minimum_x = prop.anchor_x;
  const int maximum_x = preview.image.width - prop.image.width + prop.anchor_x;
  const int minimum_y = prop.anchor_y;
  const int maximum_y = preview.image.height - prop.image.height + prop.anchor_y;
  const int clamped_x = std::clamp(requested_x, minimum_x, maximum_x);

  for (int distance = 0; distance < preview.image.width; ++distance) {
    const std::array<int, 2> candidates = {clamped_x - distance, clamped_x + distance};
    for (size_t index = 0; index < candidates.size(); ++index) {
      if (distance == 0 && index == 1) continue;
      const int candidate_x = candidates[index];
      if (candidate_x < minimum_x || candidate_x > maximum_x) continue;
      const std::optional<int> candidate_y = SurfaceY(preview, candidate_x);
      if (!candidate_y.has_value() || *candidate_y < minimum_y || *candidate_y > maximum_y) {
        continue;
      }
      return ContextAnchor{.x = candidate_x, .y = *candidate_y};
    }
  }
  return std::nullopt;
}

}  // namespace

absl::Status MovePropArtworkContextPreview(const PropArtwork& prop, int requested_anchor_x,
                                           int requested_anchor_y,
                                           PropArtworkContextPreview* preview) {
  if (preview == nullptr) {
    return absl::InvalidArgumentError("moving a context preview requires an output preview");
  }
  if (!prop.IsValid() || !preview->image.IsValid() || !preview->base_image.IsValid() ||
      !preview->terrain.IsValid() || preview->image.width != preview->base_image.width ||
      preview->image.height != preview->base_image.height ||
      prop.image.width > preview->image.width || prop.image.height > preview->image.height) {
    return absl::InvalidArgumentError("moving a context preview requires valid matching layers");
  }

  ContextAnchor anchor;
  if (preview->attachment_mode == PropAttachmentMode::kFree) {
    anchor = {
        .x = std::clamp(requested_anchor_x, prop.anchor_x,
                        preview->image.width - prop.image.width + prop.anchor_x),
        .y = std::clamp(requested_anchor_y, prop.anchor_y,
                        preview->image.height - prop.image.height + prop.anchor_y),
    };
  } else if (preview->attachment_mode == PropAttachmentMode::kGrounded ||
             preview->attachment_mode == PropAttachmentMode::kCeiling) {
    const std::optional<ContextAnchor> surface =
        NearestSurfaceAnchor(prop, requested_anchor_x, *preview);
    if (!surface.has_value()) {
      return absl::FailedPreconditionError(
          "context preview has no terrain surface that can hold the complete prop");
    }
    anchor = *surface;
  } else {
    return absl::InvalidArgumentError("context preview attachment mode is invalid");
  }

  preview->anchor_x = anchor.x;
  preview->anchor_y = anchor.y;
  preview->prop_left = anchor.x - prop.anchor_x;
  preview->prop_top = anchor.y - prop.anchor_y;
  preview->image = preview->base_image;
  Composite(prop.image, preview->prop_left, preview->prop_top, preview->image);
  return absl::OkStatus();
}

absl::StatusOr<PropArtworkContextPreview> BuildPropArtworkContextPreview(
    const PropArtwork& prop, const TerrainGenConfig& terrain_config,
    PropAttachmentMode attachment_mode) {
  if (!prop.IsValid()) {
    return absl::InvalidArgumentError("context preview requires valid prop artwork");
  }

  TerrainGenConfig preview_config = terrain_config;
  preview_config.supersample = 1;
  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(preview_config));
  ASSIGN_OR_RETURN(RgbaImage terrain, RenderTerrainPreviewScene(renderer));

  const int anchor_x = terrain.width / 2;
  int anchor_y = terrain.height / 2;
  if (attachment_mode == PropAttachmentMode::kGrounded) {
    anchor_y = -1;
    for (int y = 0; y < terrain.height; ++y) {
      const size_t offset = (static_cast<size_t>(y) * terrain.width + anchor_x) * 4;
      if (terrain.pixels[offset + 3] == 0) continue;
      anchor_y = y;
      break;
    }
  } else if (attachment_mode == PropAttachmentMode::kCeiling) {
    terrain = FlipVertical(terrain);
    anchor_y = -1;
    for (int y = terrain.height - 1; y >= 0; --y) {
      const size_t offset = (static_cast<size_t>(y) * terrain.width + anchor_x) * 4;
      if (terrain.pixels[offset + 3] == 0) continue;
      anchor_y = y;
      break;
    }
  } else if (attachment_mode != PropAttachmentMode::kFree) {
    return absl::InvalidArgumentError("context preview attachment mode is invalid");
  }
  if (anchor_y < 0) {
    return absl::FailedPreconditionError(
        "selected terrain produced no attachment surface under the context-preview anchor");
  }

  const int prop_left = anchor_x - prop.anchor_x;
  const int prop_top = anchor_y - prop.anchor_y;
  const int margin = preview_config.tile_size;
  const int content_left = AlignDown(std::min(0, prop_left), margin);
  const int content_top = AlignDown(std::min(0, prop_top), margin);
  const int content_right = AlignUp(std::max(terrain.width, prop_left + prop.image.width), margin);
  const int content_bottom =
      AlignUp(std::max(terrain.height, prop_top + prop.image.height), margin);

  const int64_t preview_width = static_cast<int64_t>(content_right) - content_left + 2LL * margin;
  const int64_t preview_height = static_cast<int64_t>(content_bottom) - content_top + 2LL * margin;
  if (preview_width <= 0 || preview_height <= 0 || preview_width > kMaximumPreviewDimension ||
      preview_height > kMaximumPreviewDimension) {
    return absl::ResourceExhaustedError("context preview dimensions exceed safe limits");
  }

  const int terrain_left = margin - content_left;
  const int terrain_top = margin - content_top;
  RgbaImage base_image =
      Checkerboard(static_cast<int>(preview_width), static_cast<int>(preview_height));
  Composite(terrain, terrain_left, terrain_top, base_image);
  PropArtworkContextPreview preview{
      .image = base_image,
      .base_image = std::move(base_image),
      .terrain = std::move(terrain),
      .terrain_left = terrain_left,
      .terrain_top = terrain_top,
      .attachment_mode = attachment_mode,
  };
  RETURN_IF_ERROR(MovePropArtworkContextPreview(prop, terrain_left + anchor_x,
                                                terrain_top + anchor_y, &preview));
  return preview;
}

}  // namespace zebes
