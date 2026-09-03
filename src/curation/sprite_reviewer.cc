#include "curation/sprite_reviewer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

constexpr size_t kMaximumFrames = 256;
constexpr int64_t kMaximumNativeFramePixels = 128LL * 1024 * 1024;
constexpr RgbaColor8 kTransparent{.red = 0, .green = 0, .blue = 0, .alpha = 0};
constexpr RgbaColor8 kCheckerLight{.red = 55, .green = 55, .blue = 65, .alpha = 255};
constexpr RgbaColor8 kCheckerDark{.red = 35, .green = 35, .blue = 45, .alpha = 255};

absl::Status ValidateFrame(const SpriteFrame& frame, const RgbaImage& texture) {
  const int64_t right = static_cast<int64_t>(frame.texture_x) + frame.texture_w;
  const int64_t bottom = static_cast<int64_t>(frame.texture_y) + frame.texture_h;
  if (frame.index < 0 || frame.texture_x < 0 || frame.texture_y < 0 || frame.texture_w <= 0 ||
      frame.texture_h <= 0 || frame.render_w <= 0 || frame.render_h <= 0 ||
      frame.frames_per_cycle < 0 || right > texture.width || bottom > texture.height) {
    return absl::FailedPreconditionError(
        absl::StrCat("sprite frame ", frame.index, " has invalid texture or render geometry"));
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> CropFrame(const SpriteFrame& frame, const RgbaImage& texture) {
  RETURN_IF_ERROR(ValidateFrame(frame, texture));
  ASSIGN_OR_RETURN(RgbaImage cropped,
                   CreateSolidRgbaImage(frame.texture_w, frame.texture_h, kTransparent));
  RETURN_IF_ERROR(CompositeRgbaNearest(cropped, texture,
                                       {.x = frame.texture_x,
                                        .y = frame.texture_y,
                                        .width = frame.texture_w,
                                        .height = frame.texture_h},
                                       {.x = 0.0,
                                        .y = 0.0,
                                        .width = static_cast<double>(frame.texture_w),
                                        .height = static_cast<double>(frame.texture_h)}));
  return cropped;
}

absl::StatusOr<RgbaImage> RenderEnlargedFrame(const SpriteFrame& frame, const RgbaImage& texture) {
  constexpr int kCanvas = 256;
  ASSIGN_OR_RETURN(RgbaImage enlarged,
                   CreateCheckerboardRgbaImage(kCanvas, kCanvas, 16, kCheckerLight, kCheckerDark));
  const double scale = std::min(8.0, std::min(224.0 / frame.render_w, 224.0 / frame.render_h));
  const double width = frame.render_w * scale;
  const double height = frame.render_h * scale;
  RETURN_IF_ERROR(CompositeRgbaNearest(enlarged, texture,
                                       {.x = frame.texture_x,
                                        .y = frame.texture_y,
                                        .width = frame.texture_w,
                                        .height = frame.texture_h},
                                       {.x = (kCanvas - width) / 2.0,
                                        .y = (kCanvas - height) / 2.0,
                                        .width = width,
                                        .height = height}));
  return enlarged;
}

absl::StatusOr<RgbaImage> RenderAnimationStrip(const Sprite& sprite, const RgbaImage& texture) {
  constexpr int kMaximumCellContent = 128;
  constexpr int kGutter = 8;
  std::vector<int> widths;
  widths.reserve(sprite.frames.size());
  int strip_width = kGutter;
  int strip_height = 1;
  for (const SpriteFrame& frame : sprite.frames) {
    RETURN_IF_ERROR(ValidateFrame(frame, texture));
    const double scale = std::min(
        1.0, static_cast<double>(kMaximumCellContent) / std::max(frame.render_w, frame.render_h));
    const int width = std::max(1, static_cast<int>(frame.render_w * scale));
    const int height = std::max(1, static_cast<int>(frame.render_h * scale));
    widths.push_back(width);
    strip_width += width + kGutter;
    strip_height = std::max(strip_height, height + 2 * kGutter);
  }
  ASSIGN_OR_RETURN(RgbaImage strip, CreateCheckerboardRgbaImage(strip_width, strip_height, 8,
                                                                kCheckerLight, kCheckerDark));
  int x = kGutter;
  for (size_t index = 0; index < sprite.frames.size(); ++index) {
    const SpriteFrame& frame = sprite.frames[index];
    const int width = widths[index];
    const double scale = static_cast<double>(width) / frame.render_w;
    const double height = frame.render_h * scale;
    RETURN_IF_ERROR(CompositeRgbaNearest(strip, texture,
                                         {.x = frame.texture_x,
                                          .y = frame.texture_y,
                                          .width = frame.texture_w,
                                          .height = frame.texture_h},
                                         {.x = static_cast<double>(x),
                                          .y = (strip_height - height) / 2.0,
                                          .width = static_cast<double>(width),
                                          .height = height}));
    x += width + kGutter;
  }
  return strip;
}

struct PixelBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

absl::StatusOr<PixelBounds> ForegroundBounds(const SpriteFrame& frame, const RgbaImage& texture) {
  RETURN_IF_ERROR(ValidateFrame(frame, texture));
  PixelBounds bounds{
      .left = frame.texture_w,
      .top = frame.texture_h,
  };
  for (int y = 0; y < frame.texture_h; ++y) {
    for (int x = 0; x < frame.texture_w; ++x) {
      const size_t offset =
          (static_cast<size_t>(frame.texture_y + y) * texture.width + frame.texture_x + x) * 4;
      if (texture.pixels[offset + 3] == 0) continue;
      bounds.left = std::min(bounds.left, x);
      bounds.top = std::min(bounds.top, y);
      bounds.right = std::max(bounds.right, x + 1);
      bounds.bottom = std::max(bounds.bottom, y + 1);
    }
  }
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
    return absl::FailedPreconditionError(
        absl::StrCat("sprite frame ", frame.index, " has no visible pixels"));
  }
  return bounds;
}

absl::StatusOr<RgbaImage> RenderContactSheet(const Sprite& sprite, const RgbaImage& texture) {
  constexpr int kColumns = 4;
  constexpr int kCell = 128;
  const int columns = std::min(kColumns, static_cast<int>(sprite.frames.size()));
  const int rows = (static_cast<int>(sprite.frames.size()) + columns - 1) / columns;
  ASSIGN_OR_RETURN(RgbaImage sheet, CreateCheckerboardRgbaImage(columns * kCell, rows * kCell, 8,
                                                                kCheckerLight, kCheckerDark));
  for (size_t index = 0; index < sprite.frames.size(); ++index) {
    const SpriteFrame& frame = sprite.frames[index];
    const double scale = std::min(4.0, std::min(112.0 / frame.render_w, 112.0 / frame.render_h));
    const double width = frame.render_w * scale;
    const double height = frame.render_h * scale;
    const int column = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    RETURN_IF_ERROR(CompositeRgbaNearest(sheet, texture,
                                         {.x = frame.texture_x,
                                          .y = frame.texture_y,
                                          .width = frame.texture_w,
                                          .height = frame.texture_h},
                                         {.x = column * kCell + (kCell - width) / 2.0,
                                          .y = row * kCell + (kCell - height) / 2.0,
                                          .width = width,
                                          .height = height}));
  }
  return sheet;
}

absl::StatusOr<RgbaImage> RenderAlignmentSheet(const Sprite& sprite, const RgbaImage& texture) {
  constexpr int kColumns = 4;
  constexpr int kCell = 128;
  constexpr RgbaColor8 kOrigin{.red = 255, .green = 80, .blue = 80, .alpha = 255};
  constexpr RgbaColor8 kContact{.red = 80, .green = 220, .blue = 255, .alpha = 255};
  constexpr RgbaColor8 kBounds{.red = 255, .green = 220, .blue = 80, .alpha = 255};
  const int columns = std::min(kColumns, static_cast<int>(sprite.frames.size()));
  const int rows = (static_cast<int>(sprite.frames.size()) + columns - 1) / columns;
  ASSIGN_OR_RETURN(RgbaImage sheet, CreateCheckerboardRgbaImage(columns * kCell, rows * kCell, 8,
                                                                kCheckerLight, kCheckerDark));
  for (size_t index = 0; index < sprite.frames.size(); ++index) {
    const SpriteFrame& frame = sprite.frames[index];
    ASSIGN_OR_RETURN(const PixelBounds bounds, ForegroundBounds(frame, texture));
    const double scale = std::min(1.0, std::min(112.0 / frame.render_w, 112.0 / frame.render_h));
    const int width = std::max(1, static_cast<int>(frame.render_w * scale));
    const int height = std::max(1, static_cast<int>(frame.render_h * scale));
    const int column = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    const int frame_x = column * kCell + (kCell - width) / 2;
    const int frame_y = row * kCell + (kCell - height) / 2;
    RETURN_IF_ERROR(CompositeRgbaNearest(sheet, texture,
                                         {.x = frame.texture_x,
                                          .y = frame.texture_y,
                                          .width = frame.texture_w,
                                          .height = frame.texture_h},
                                         {.x = static_cast<double>(frame_x),
                                          .y = static_cast<double>(frame_y),
                                          .width = static_cast<double>(width),
                                          .height = static_cast<double>(height)}));
    const double texture_scale = static_cast<double>(width) / frame.texture_w;
    const int origin_x = frame_x + static_cast<int>(-frame.offset_x * scale);
    const int origin_y = frame_y + static_cast<int>(-frame.offset_y * scale);
    RETURN_IF_ERROR(FillRgbaRect(sheet, column * kCell, origin_y, kCell, 1, kContact));
    RETURN_IF_ERROR(FillRgbaRect(sheet, origin_x, row * kCell, 1, kCell, kOrigin));
    RETURN_IF_ERROR(DrawRgbaCross(sheet, origin_x, origin_y, 3, kOrigin));
    RETURN_IF_ERROR(DrawRgbaOutline(sheet, frame_x + static_cast<int>(bounds.left * texture_scale),
                                    frame_y + static_cast<int>(bounds.top * texture_scale),
                                    frame_x + static_cast<int>(bounds.right * texture_scale) - 1,
                                    frame_y + static_cast<int>(bounds.bottom * texture_scale) - 1,
                                    kBounds));
  }
  return sheet;
}

struct DifferenceArtifact {
  RgbaImage image;
  size_t changed_pixels = 0;
  size_t union_pixels = 0;
};

absl::StatusOr<DifferenceArtifact> RenderDifference(const SpriteFrame& from, const SpriteFrame& to,
                                                    const RgbaImage& texture) {
  RETURN_IF_ERROR(ValidateFrame(from, texture));
  RETURN_IF_ERROR(ValidateFrame(to, texture));
  const int width = std::max(from.texture_w, to.texture_w);
  const int height = std::max(from.texture_h, to.texture_h);
  ASSIGN_OR_RETURN(RgbaImage difference, CreateSolidRgbaImage(width, height, kTransparent));
  size_t changed_pixels = 0;
  size_t union_pixels = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::array<uint8_t, 4> first{};
      std::array<uint8_t, 4> second{};
      if (x < from.texture_w && y < from.texture_h) {
        const size_t offset =
            (static_cast<size_t>(from.texture_y + y) * texture.width + from.texture_x + x) * 4;
        std::copy_n(texture.pixels.begin() + offset, 4, first.begin());
      }
      if (x < to.texture_w && y < to.texture_h) {
        const size_t offset =
            (static_cast<size_t>(to.texture_y + y) * texture.width + to.texture_x + x) * 4;
        std::copy_n(texture.pixels.begin() + offset, 4, second.begin());
      }
      if (first[3] != 0 || second[3] != 0) ++union_pixels;
      if (first == second) continue;
      ++changed_pixels;
      const size_t output_offset = (static_cast<size_t>(y) * width + x) * 4;
      difference.pixels[output_offset + 0] = 255;
      difference.pixels[output_offset + 1] = first[3] > second[3] ? 80 : 200;
      difference.pixels[output_offset + 2] = 220;
      difference.pixels[output_offset + 3] = 255;
    }
  }
  constexpr int kCanvas = 256;
  ASSIGN_OR_RETURN(RgbaImage enlarged,
                   CreateCheckerboardRgbaImage(kCanvas, kCanvas, 16, kCheckerLight, kCheckerDark));
  const double scale = std::min(8.0, std::min(224.0 / width, 224.0 / height));
  RETURN_IF_ERROR(CompositeRgbaNearest(enlarged, difference,
                                       {.x = 0, .y = 0, .width = width, .height = height},
                                       {.x = (kCanvas - width * scale) / 2.0,
                                        .y = (kCanvas - height * scale) / 2.0,
                                        .width = width * scale,
                                        .height = height * scale}));
  return DifferenceArtifact{
      .image = std::move(enlarged),
      .changed_pixels = changed_pixels,
      .union_pixels = union_pixels,
  };
}

nlohmann::json FrameToJson(const SpriteFrame& frame) {
  return {
      {"index", frame.index},         {"texture_x", frame.texture_x},
      {"texture_y", frame.texture_y}, {"texture_w", frame.texture_w},
      {"texture_h", frame.texture_h}, {"render_w", frame.render_w},
      {"render_h", frame.render_h},   {"frames_per_cycle", frame.frames_per_cycle},
      {"offset_x", frame.offset_x},   {"offset_y", frame.offset_y},
  };
}

}  // namespace

absl::StatusOr<CurationReview> SpriteReviewer::Review(Api& api,
                                                      const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(request.asset_id));
  if (sprite == nullptr) return absl::FailedPreconditionError("sprite lookup returned null");
  if (sprite->id.empty() || sprite->name.empty() || sprite->texture_id.empty() ||
      sprite->frames.empty()) {
    return absl::FailedPreconditionError(
        "sprite review needs identity, texture, and at least one frame");
  }
  if (!IsValidSpritePlaybackMode(sprite->playback_mode)) {
    return absl::FailedPreconditionError("sprite review needs a valid playback mode");
  }
  if (sprite->frames.size() > kMaximumFrames) {
    return absl::ResourceExhaustedError("sprite exceeds the headless frame artifact limit");
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(sprite->texture_id));
  if (texture == nullptr || texture->id != sprite->texture_id) {
    return absl::FailedPreconditionError("sprite texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture->id));

  absl::flat_hash_set<int> frame_indices;
  int64_t total_native_pixels = 0;
  for (const SpriteFrame& frame : sprite->frames) {
    RETURN_IF_ERROR(ValidateFrame(frame, pixels));
    if (!frame_indices.insert(frame.index).second) {
      return absl::FailedPreconditionError(
          absl::StrCat("sprite repeats frame index ", frame.index));
    }
    total_native_pixels += static_cast<int64_t>(frame.texture_w) * frame.texture_h;
    if (total_native_pixels > kMaximumNativeFramePixels) {
      return absl::ResourceExhaustedError("sprite native frame artifacts exceed the pixel limit");
    }
  }
  ASSIGN_OR_RETURN(RgbaImage strip, RenderAnimationStrip(*sprite, pixels));
  ASSIGN_OR_RETURN(RgbaImage contact_sheet, RenderContactSheet(*sprite, pixels));
  ASSIGN_OR_RETURN(RgbaImage alignment_sheet, RenderAlignmentSheet(*sprite, pixels));

  CurationReview review{
      .kind = std::string(kind()),
      .asset_id = sprite->id,
      .asset_name = sprite->name,
      .metadata =
          {
              {"texture_id", sprite->texture_id},
              {"playback_mode", SpritePlaybackModeId(sprite->playback_mode)},
              {"frame_count", sprite->frames.size()},
          },
      .artifacts =
          {
              {
                  .id = "animation-strip",
                  .relative_path = "animation-strip.png",
                  .description = "Authored frame sequence in animation order",
                  .image = std::move(strip),
                  .metadata = {{"view", "animation-strip"}},
              },
              {
                  .id = "contact-sheet",
                  .relative_path = "contact-sheet.png",
                  .description = "Nearest-neighbour contact sheet for every frame",
                  .image = std::move(contact_sheet),
                  .metadata = {{"view", "contact-sheet"}},
              },
              {
                  .id = "alignment-overlay",
                  .relative_path = "alignment-overlay.png",
                  .description = "Frame bounds with origin and contact-line alignment",
                  .image = std::move(alignment_sheet),
                  .metadata = {{"view", "alignment-overlay"},
                               {"origin_color", "red"},
                               {"contact_line_color", "cyan"},
                               {"bounds_color", "yellow"}},
              },
          },
  };
  nlohmann::json frame_definitions = nlohmann::json::array();
  for (const SpriteFrame& frame : sprite->frames) {
    ASSIGN_OR_RETURN(RgbaImage native, CropFrame(frame, pixels));
    ASSIGN_OR_RETURN(RgbaImage enlarged, RenderEnlargedFrame(frame, pixels));
    const nlohmann::json definition = FrameToJson(frame);
    const std::string prefix = absl::StrCat("frames/", frame.index);
    review.artifacts.push_back({
        .id = absl::StrCat("frame-", frame.index, "-native"),
        .relative_path = absl::StrCat(prefix, "-native.png"),
        .description = absl::StrCat("Native source pixels for frame ", frame.index),
        .image = std::move(native),
        .metadata = {{"view", "native-frame"}, {"frame", definition}},
    });
    review.artifacts.push_back({
        .id = absl::StrCat("frame-", frame.index, "-enlarged"),
        .relative_path = absl::StrCat(prefix, "-enlarged.png"),
        .description = absl::StrCat("Nearest-neighbour enlarged frame ", frame.index),
        .image = std::move(enlarged),
        .metadata = {{"view", "enlarged-frame"}, {"frame", definition}},
    });
    frame_definitions.push_back(definition);
  }
  nlohmann::json transitions = nlohmann::json::array();
  auto add_difference = [&](size_t from_index, size_t to_index, bool loop_closure) -> absl::Status {
    ASSIGN_OR_RETURN(
        DifferenceArtifact difference,
        RenderDifference(sprite->frames[from_index], sprite->frames[to_index], pixels));
    const std::string id =
        loop_closure ? "loop-closure" : absl::StrCat("transition-", from_index, "-", to_index);
    const nlohmann::json metadata = {
        {"view", loop_closure ? "loop-closure" : "adjacent-difference"},
        {"from_frame", sprite->frames[from_index].index},
        {"to_frame", sprite->frames[to_index].index},
        {"changed_pixels", difference.changed_pixels},
        {"union_pixels", difference.union_pixels},
    };
    review.artifacts.push_back({
        .id = id,
        .relative_path = absl::StrCat("transitions/", id, ".png"),
        .description =
            loop_closure ? "Last-to-first loop closure difference"
                         : absl::StrCat("Adjacent frame difference ", from_index, " to ", to_index),
        .image = std::move(difference.image),
        .metadata = metadata,
    });
    transitions.push_back(metadata);
    return absl::OkStatus();
  };
  for (size_t index = 1; index < sprite->frames.size(); ++index) {
    RETURN_IF_ERROR(add_difference(index - 1, index, false));
  }
  if (sprite->playback_mode == SpritePlaybackMode::kLoop) {
    RETURN_IF_ERROR(add_difference(sprite->frames.size() - 1, 0, true));
  } else {
    ASSIGN_OR_RETURN(RgbaImage hold_final, RenderEnlargedFrame(sprite->frames.back(), pixels));
    review.artifacts.push_back({
        .id = "hold-final",
        .relative_path = "hold-final.png",
        .description = "Final pose retained by hold-last playback",
        .image = std::move(hold_final),
        .metadata = {{"view", "hold-final"}, {"frame", sprite->frames.back().index}},
    });
  }
  review.metadata["transitions"] = std::move(transitions);
  review.metadata["frames"] = std::move(frame_definitions);
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
