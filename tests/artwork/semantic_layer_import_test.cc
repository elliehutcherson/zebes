#include "artwork/semantic_layer_import.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

size_t Offset(const RgbaImage& image, int x, int y) {
  return (static_cast<size_t>(y) * image.width + x) * 4;
}

void SetPixel(RgbaImage& image, int x, int y, std::array<uint8_t, 4> color) {
  const size_t offset = Offset(image, x, y);
  for (size_t channel = 0; channel < color.size(); ++channel) {
    image.pixels[offset + channel] = color[channel];
  }
}

RgbaImage EmptyImage(int width, int height) {
  return RgbaImage{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0),
  };
}

TEST(SemanticLayerImportTest, RestoresCropAtExactCanvasPosition) {
  RgbaImage cropped = EmptyImage(2, 2);
  SetPixel(cropped, 0, 0, {10, 20, 30, 255});
  SetPixel(cropped, 1, 1, {40, 50, 60, 255});

  const absl::StatusOr<RgbaImage> restored =
      RestoreSemanticLayer(cropped, {.x = 3, .y = 2, .width = 2, .height = 2}, 8, 6);

  ASSERT_TRUE(restored.ok()) << restored.status();
  EXPECT_EQ(std::vector<uint8_t>(restored->pixels.begin() + Offset(*restored, 3, 2),
                                 restored->pixels.begin() + Offset(*restored, 3, 2) + 4),
            (std::vector<uint8_t>{10, 20, 30, 255}));
  EXPECT_EQ(std::vector<uint8_t>(restored->pixels.begin() + Offset(*restored, 4, 3),
                                 restored->pixels.begin() + Offset(*restored, 4, 3) + 4),
            (std::vector<uint8_t>{40, 50, 60, 255}));
  EXPECT_EQ(restored->pixels[Offset(*restored, 2, 2) + 3], 0);
}

TEST(SemanticLayerImportTest, DownsamplesWithAlphaWeightedColorAndBinaryCoverage) {
  RgbaImage source = EmptyImage(4, 4);
  SetPixel(source, 0, 0, {200, 0, 0, 255});
  SetPixel(source, 1, 0, {0, 100, 0, 128});
  SetPixel(source, 3, 3, {0, 0, 255, 255});

  const absl::StatusOr<RgbaImage> output = DownsampleSemanticLayer(source, 2, 2, 0.2);

  ASSERT_TRUE(output.ok()) << output.status();
  const size_t first = Offset(*output, 0, 0);
  EXPECT_EQ(output->pixels[first], 133);
  EXPECT_EQ(output->pixels[first + 1], 33);
  EXPECT_EQ(output->pixels[first + 2], 0);
  EXPECT_EQ(output->pixels[first + 3], 255);
  EXPECT_EQ(output->pixels[Offset(*output, 1, 1) + 3], 255);
  EXPECT_EQ(output->pixels[Offset(*output, 1, 0) + 3], 0);
}

TEST(SemanticLayerImportTest, VisibleMaskRestoresOriginalPixelsExactly) {
  RgbaImage source = EmptyImage(3, 2);
  RgbaImage candidate = EmptyImage(3, 2);
  RgbaImage mask = EmptyImage(3, 2);
  SetPixel(source, 1, 0, {17, 29, 43, 255});
  SetPixel(candidate, 1, 0, {220, 180, 140, 255});
  SetPixel(candidate, 2, 1, {90, 80, 70, 255});
  SetPixel(mask, 1, 0, {255, 255, 255, 255});

  const absl::StatusOr<RgbaImage> output = PreserveSemanticVisiblePixels(candidate, source, mask);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(std::vector<uint8_t>(output->pixels.begin() + Offset(*output, 1, 0),
                                 output->pixels.begin() + Offset(*output, 1, 0) + 4),
            (std::vector<uint8_t>{17, 29, 43, 255}));
  EXPECT_EQ(std::vector<uint8_t>(output->pixels.begin() + Offset(*output, 2, 1),
                                 output->pixels.begin() + Offset(*output, 2, 1) + 4),
            (std::vector<uint8_t>{90, 80, 70, 255}));
}

TEST(SemanticLayerImportTest, ClipsGeneratedCompletionToAllowedAlpha) {
  RgbaImage candidate = EmptyImage(3, 1);
  RgbaImage mask = EmptyImage(3, 1);
  SetPixel(candidate, 0, 0, {10, 20, 30, 255});
  SetPixel(candidate, 2, 0, {40, 50, 60, 255});
  SetPixel(mask, 2, 0, {255, 255, 255, 255});

  const absl::StatusOr<RgbaImage> clipped = ClipSemanticLayerToMask(candidate, mask);

  ASSERT_TRUE(clipped.ok()) << clipped.status();
  EXPECT_EQ(clipped->pixels[Offset(*clipped, 0, 0) + 3], 0);
  EXPECT_EQ(std::vector<uint8_t>(clipped->pixels.begin() + Offset(*clipped, 2, 0),
                                 clipped->pixels.begin() + Offset(*clipped, 2, 0) + 4),
            (std::vector<uint8_t>{40, 50, 60, 255}));
}

TEST(SemanticLayerImportTest, MeasuresExclusiveVisibleOwnership) {
  RgbaImage source = EmptyImage(4, 1);
  RgbaImage first = EmptyImage(4, 1);
  RgbaImage second = EmptyImage(4, 1);
  for (int x = 0; x < 3; ++x) SetPixel(source, x, 0, {20, 30, 40, 255});
  SetPixel(first, 0, 0, {20, 30, 40, 255});
  SetPixel(first, 1, 0, {20, 30, 40, 255});
  SetPixel(second, 2, 0, {20, 30, 40, 255});
  const std::array<RgbaImage, 2> exclusive = {first, second};

  const absl::StatusOr<SemanticVisibleOwnership> accepted =
      MeasureSemanticVisibleOwnership(source, exclusive);

  ASSERT_TRUE(accepted.ok()) << accepted.status();
  EXPECT_EQ(accepted->source_pixels, 3);
  EXPECT_EQ(accepted->singly_owned_pixels, 3);
  EXPECT_EQ(accepted->unowned_pixels, 0);
  EXPECT_EQ(accepted->multiply_owned_pixels, 0);
  EXPECT_EQ(accepted->ownership_outside_source_pixels, 0);

  SetPixel(second, 1, 0, {20, 30, 40, 255});
  SetPixel(second, 3, 0, {20, 30, 40, 255});
  const std::array<RgbaImage, 2> invalid = {first, second};
  const absl::StatusOr<SemanticVisibleOwnership> rejected =
      MeasureSemanticVisibleOwnership(source, invalid);

  ASSERT_TRUE(rejected.ok()) << rejected.status();
  EXPECT_EQ(rejected->singly_owned_pixels, 2);
  EXPECT_EQ(rejected->multiply_owned_pixels, 1);
  EXPECT_EQ(rejected->ownership_outside_source_pixels, 1);
}

TEST(SemanticLayerImportTest, MeasuresImmutableLayerMutation) {
  RgbaImage source = EmptyImage(3, 1);
  SetPixel(source, 0, 0, {10, 20, 30, 255});
  SetPixel(source, 2, 0, {40, 50, 60, 255});

  const absl::StatusOr<SemanticLayerMutation> exact = MeasureSemanticLayerMutation(source, source);

  ASSERT_TRUE(exact.ok()) << exact.status();
  EXPECT_EQ(exact->changed_pixels, 0);
  EXPECT_EQ(exact->alpha_added_pixels, 0);
  EXPECT_EQ(exact->alpha_removed_pixels, 0);

  RgbaImage changed = source;
  SetPixel(changed, 0, 0, {11, 20, 30, 255});
  SetPixel(changed, 1, 0, {70, 80, 90, 255});
  SetPixel(changed, 2, 0, {0, 0, 0, 0});
  const absl::StatusOr<SemanticLayerMutation> mutation =
      MeasureSemanticLayerMutation(source, changed);

  ASSERT_TRUE(mutation.ok()) << mutation.status();
  EXPECT_EQ(mutation->changed_pixels, 3);
  EXPECT_EQ(mutation->alpha_added_pixels, 1);
  EXPECT_EQ(mutation->alpha_removed_pixels, 1);
}

TEST(SemanticLayerImportTest, SplitsAndOrdersOpaqueComponents) {
  RgbaImage source = EmptyImage(8, 5);
  SetPixel(source, 1, 1, {200, 20, 20, 255});
  SetPixel(source, 1, 2, {200, 20, 20, 255});
  SetPixel(source, 5, 2, {20, 20, 200, 255});
  SetPixel(source, 6, 2, {20, 20, 200, 255});
  SetPixel(source, 7, 4, {20, 200, 20, 255});

  const absl::StatusOr<std::vector<SemanticLayerComponent>> components =
      SplitSemanticLayerComponents(source, 2);

  ASSERT_TRUE(components.ok()) << components.status();
  ASSERT_EQ(components->size(), 2);
  EXPECT_EQ((*components)[0].minimum_x, 1);
  EXPECT_EQ((*components)[0].maximum_x, 2);
  EXPECT_EQ((*components)[0].pixel_count, 2);
  EXPECT_EQ((*components)[1].minimum_x, 5);
  EXPECT_EQ((*components)[1].maximum_x, 7);
  EXPECT_EQ((*components)[1].pixel_count, 2);
}

TEST(SemanticLayerImportTest, RejectsVisibleMaskOverTransparentSource) {
  const RgbaImage source = EmptyImage(2, 2);
  const RgbaImage candidate = EmptyImage(2, 2);
  RgbaImage mask = EmptyImage(2, 2);
  SetPixel(mask, 1, 1, {255, 255, 255, 255});

  const absl::Status status = PreserveSemanticVisiblePixels(candidate, source, mask).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("transparent source pixel"), std::string::npos);
}

}  // namespace
}  // namespace zebes
