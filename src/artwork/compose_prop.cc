#include "artwork/compose_prop.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

absl::Status ValidatePropCompositionConfig(const PropCompositionConfig& config) {
  if (config.canvas_tiles_wide <= 0 || config.canvas_tiles_high <= 0) {
    return absl::InvalidArgumentError("prop composition canvas dimensions must be positive");
  }
  if (!std::isfinite(config.padding_fraction) || config.padding_fraction < 0.0f ||
      config.padding_fraction >= 0.45f) {
    return absl::InvalidArgumentError("prop composition padding must be in [0, 0.45)");
  }
  return absl::OkStatus();
}

absl::Status ValidatePropAttachment(const PropAttachmentConfig& attachment, int output_width,
                                    int output_height) {
  if (output_width <= 0 || output_height <= 0) {
    return absl::InvalidArgumentError("prop attachment requires positive output dimensions");
  }
  switch (attachment.mode) {
    case PropAttachmentMode::kGrounded:
    case PropAttachmentMode::kCeiling:
      if (attachment.free_anchor.has_value()) {
        return absl::InvalidArgumentError(
            "grounded and ceiling attachments cannot contain a free anchor");
      }
      return absl::OkStatus();
    case PropAttachmentMode::kFree:
      if (!attachment.free_anchor.has_value()) {
        return absl::InvalidArgumentError("free attachment requires an explicit anchor");
      }
      if (attachment.free_anchor->x < 0 || attachment.free_anchor->y < 0 ||
          attachment.free_anchor->x >= output_width || attachment.free_anchor->y >= output_height) {
        return absl::InvalidArgumentError("free attachment anchor must be inside the prop canvas");
      }
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("prop attachment mode is invalid");
}

absl::StatusOr<PropArtwork> ComposeProp(const RgbaImage& isolated,
                                        const PropCompositionConfig& config, int final_output_width,
                                        int final_output_height) {
  if (!isolated.IsValid()) return absl::InvalidArgumentError("isolated image is invalid");
  RETURN_IF_ERROR(ValidatePropCompositionConfig(config));
  RETURN_IF_ERROR(
      ValidatePropAttachment(config.attachment, final_output_width, final_output_height));

  int left = isolated.width;
  int top = isolated.height;
  int right = 0;
  int bottom = 0;
  for (int y = 0; y < isolated.height; ++y) {
    for (int x = 0; x < isolated.width; ++x) {
      const size_t pixel = (static_cast<size_t>(y) * isolated.width + x) * 4;
      if (isolated.pixels[pixel + 3] == 0) continue;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x + 1);
      bottom = std::max(bottom, y + 1);
    }
  }
  if (left >= right || top >= bottom) {
    return absl::FailedPreconditionError("isolated image has no subject to compose");
  }

  const double usable = 1.0 - 2.0 * config.padding_fraction;
  double canvas_width = static_cast<double>(right - left) / usable;
  double canvas_height = static_cast<double>(bottom - top) / usable;
  const double target_aspect =
      static_cast<double>(config.canvas_tiles_wide) / config.canvas_tiles_high;
  if (canvas_width / canvas_height < target_aspect) {
    canvas_width = canvas_height * target_aspect;
  } else {
    canvas_height = canvas_width / target_aspect;
  }
  if (canvas_width > 16384.0 || canvas_height > 16384.0) {
    return absl::ResourceExhaustedError("composed prop canvas exceeds 16384 pixels");
  }

  const int output_width = static_cast<int>(std::ceil(canvas_width));
  const int output_height = static_cast<int>(std::ceil(canvas_height));
  const double subject_center_x = (static_cast<double>(left) + right) * 0.5;
  const int source_left =
      static_cast<int>(std::floor(subject_center_x - static_cast<double>(output_width) * 0.5));
  int source_top = 0;
  switch (config.attachment.mode) {
    case PropAttachmentMode::kGrounded: {
      const int source_bottom =
          static_cast<int>(std::ceil(bottom + config.padding_fraction * output_height));
      source_top = source_bottom - output_height;
      break;
    }
    case PropAttachmentMode::kCeiling:
      source_top = static_cast<int>(std::floor(top - config.padding_fraction * output_height));
      break;
    case PropAttachmentMode::kFree: {
      const double subject_center_y = (static_cast<double>(top) + bottom) * 0.5;
      source_top =
          static_cast<int>(std::floor(subject_center_y - static_cast<double>(output_height) * 0.5));
      break;
    }
  }

  RgbaImage composed;
  composed.width = output_width;
  composed.height = output_height;
  composed.pixels.assign(static_cast<size_t>(output_width) * output_height * 4, 0);
  for (int output_y = 0; output_y < output_height; ++output_y) {
    const int source_y = source_top + output_y;
    if (source_y < 0 || source_y >= isolated.height) continue;
    for (int output_x = 0; output_x < output_width; ++output_x) {
      const int source_x = source_left + output_x;
      if (source_x < 0 || source_x >= isolated.width) continue;
      const size_t source_pixel = (static_cast<size_t>(source_y) * isolated.width + source_x) * 4;
      const size_t output_pixel = (static_cast<size_t>(output_y) * output_width + output_x) * 4;
      std::copy_n(isolated.pixels.begin() + static_cast<ptrdiff_t>(source_pixel), 4,
                  composed.pixels.begin() + static_cast<ptrdiff_t>(output_pixel));
    }
  }

  int anchor_y = 0;
  if (config.attachment.mode == PropAttachmentMode::kGrounded) {
    anchor_y = bottom - 1 - source_top;
  } else if (config.attachment.mode == PropAttachmentMode::kCeiling) {
    anchor_y = top - source_top;
  } else {
    anchor_y = static_cast<int>(std::lround(static_cast<double>(config.attachment.free_anchor->y) *
                                            composed.height / final_output_height));
  }
  const int anchor_x =
      config.attachment.mode == PropAttachmentMode::kFree
          ? static_cast<int>(std::lround(static_cast<double>(config.attachment.free_anchor->x) *
                                         composed.width / final_output_width))
          : static_cast<int>(std::lround(subject_center_x - source_left));
  const int composed_width = composed.width;
  const int composed_height = composed.height;
  PropArtwork result{
      .image = std::move(composed),
      .anchor_x = std::clamp(anchor_x, 0, composed_width - 1),
      .anchor_y = std::clamp(anchor_y, 0, composed_height - 1),
  };
  if (!result.IsValid()) {
    return absl::InternalError("prop composition produced an invalid anchor");
  }
  return result;
}

}  // namespace zebes
