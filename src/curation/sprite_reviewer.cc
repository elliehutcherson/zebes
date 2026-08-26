#include "curation/sprite_reviewer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
  if (sprite->frames.size() > kMaximumFrames) {
    return absl::ResourceExhaustedError("sprite exceeds the headless frame artifact limit");
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(sprite->texture_id));
  if (texture == nullptr || texture->id != sprite->texture_id) {
    return absl::FailedPreconditionError("sprite texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture->id));

  std::set<int> frame_indices;
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

  CurationReview review{
      .kind = std::string(kind()),
      .asset_id = sprite->id,
      .asset_name = sprite->name,
      .metadata =
          {
              {"texture_id", sprite->texture_id},
              {"frame_count", sprite->frames.size()},
          },
      .artifacts = {{
          .id = "animation-strip",
          .relative_path = "animation-strip.png",
          .description = "Authored frame sequence in animation order",
          .image = std::move(strip),
          .metadata = {{"view", "animation-strip"}},
      }},
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
  review.metadata["frames"] = std::move(frame_definitions);
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
