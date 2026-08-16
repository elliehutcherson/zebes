#include <cstddef>
#include <cstdint>

#include "artwork/cleanup_prop.h"
#include "artwork/compose_prop.h"
#include "artwork/edge_treatment.h"
#include "artwork/isolate_subject.h"
#include "artwork/quantize_prop.h"
#include "artwork/rasterize_prop.h"
#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SolidImage(int width, int height, RgbaColor color) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<size_t>(width) * height * 4);
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    image.pixels[pixel * 4 + 0] = color.r;
    image.pixels[pixel * 4 + 1] = color.g;
    image.pixels[pixel * 4 + 2] = color.b;
    image.pixels[pixel * 4 + 3] = color.a;
  }
  return image;
}

void PaintRect(RgbaImage& image, int left, int top, int width, int height, RgbaColor color) {
  for (int y = top; y < top + height; ++y) {
    for (int x = left; x < left + width; ++x) {
      const size_t pixel = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[pixel + 0] = color.r;
      image.pixels[pixel + 1] = color.g;
      image.pixels[pixel + 2] = color.b;
      image.pixels[pixel + 3] = color.a;
    }
  }
}

TEST(PropArtworkPipelineTest, IsolationRemovesOnlyBorderConnectedBackground) {
  RgbaImage source = SolidImage(8, 8, RgbaColor{240, 240, 240, 255});
  PaintRect(source, 2, 2, 4, 4, RgbaColor{60, 70, 80, 255});
  ASSERT_OK_AND_ASSIGN(const RgbaImage isolated,
                       IsolateSubject(source, SubjectIsolationConfig{.minimum_subject_area = 4}));

  EXPECT_EQ(isolated.pixels[3], 0);
  const size_t center = (static_cast<size_t>(3) * isolated.width + 3) * 4;
  EXPECT_EQ(isolated.pixels[center + 3], 255);
}

TEST(PropArtworkPipelineTest, IsolationRefusesCompetingSubjects) {
  RgbaImage source = SolidImage(12, 8, RgbaColor{240, 240, 240, 255});
  PaintRect(source, 1, 2, 3, 3, RgbaColor{60, 70, 80, 255});
  PaintRect(source, 8, 2, 3, 3, RgbaColor{80, 70, 60, 255});

  EXPECT_FALSE(IsolateSubject(source, SubjectIsolationConfig{.minimum_subject_area = 4}).ok());
}

TEST(PropArtworkPipelineTest, RasterizationUsesPremultipliedAlpha) {
  RgbaImage source;
  source.width = 2;
  source.height = 1;
  source.pixels = {255, 0, 0, 255, 255, 255, 255, 0};
  const PropArtwork artwork{.image = source, .anchor_x = 0, .anchor_y = 0};

  ASSERT_OK_AND_ASSIGN(
      const PropArtwork rasterized,
      RasterizeProp(artwork, PropRasterConfig{
                                 .tile_size = 1, .canvas_tiles_wide = 1, .canvas_tiles_high = 1}));
  EXPECT_EQ(rasterized.image.pixels[0], 255);
  EXPECT_EQ(rasterized.image.pixels[1], 0);
  EXPECT_EQ(rasterized.image.pixels[2], 0);
  EXPECT_NEAR(rasterized.image.pixels[3], 128, 1);
}

TEST(PropArtworkPipelineTest, PalettePoliciesProduceDistinctExperiments) {
  const TerrainGenConfig config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(config));
  ASSERT_OK_AND_ASSIGN(const PropPalette full,
                       BuildPropPalette(terrain, config.material, PropPalettePolicy::kFullTerrain));
  ASSERT_OK_AND_ASSIGN(
      const PropPalette semantic,
      BuildPropPalette(terrain, config.material, PropPalettePolicy::kSemanticSubset));
  ASSERT_OK_AND_ASSIGN(
      const PropPalette derived,
      BuildPropPalette(terrain, config.material, PropPalettePolicy::kDerivedRamps));

  EXPECT_GT(full.colors.size(), semantic.colors.size());
  EXPECT_GT(derived.colors.size(), semantic.colors.size());
}

TEST(PropArtworkPipelineTest, FullPipelineProducesAValidatedTileSizedProp) {
  RgbaImage source = SolidImage(32, 24, RgbaColor{236, 232, 228, 255});
  PaintRect(source, 7, 6, 18, 13, RgbaColor{74, 68, 64, 255});
  PaintRect(source, 10, 7, 8, 5, RgbaColor{126, 116, 104, 255});

  ASSERT_OK_AND_ASSIGN(const RgbaImage isolated,
                       IsolateSubject(source, SubjectIsolationConfig{.minimum_subject_area = 16}));
  ASSERT_OK_AND_ASSIGN(const PropArtwork composed,
                       ComposeProp(isolated, PropCompositionConfig{.canvas_tiles_wide = 2,
                                                                   .canvas_tiles_high = 1,
                                                                   .padding_fraction = 0.05f}));
  ASSERT_OK_AND_ASSIGN(
      const PropArtwork rasterized,
      RasterizeProp(composed, PropRasterConfig{
                                  .tile_size = 8, .canvas_tiles_wide = 2, .canvas_tiles_high = 1}));

  const TerrainGenConfig terrain_config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(terrain_config));
  ASSERT_OK_AND_ASSIGN(
      const PropPalette palette,
      BuildPropPalette(terrain, terrain_config.material, PropPalettePolicy::kSemanticSubset));
  ASSERT_OK_AND_ASSIGN(const PropArtwork quantized, QuantizeProp(rasterized, palette));
  ASSERT_OK_AND_ASSIGN(const PropArtwork outlined,
                       ApplyPropEdgeTreatment(quantized, palette.outline, PropEdgeConfig{}));
  ASSERT_OK_AND_ASSIGN(
      const PropArtwork finished,
      CleanupAndValidateProp(outlined, palette.colors,
                             PropCleanupConfig{.tile_size = 8, .grounded_tolerance = 2}));

  EXPECT_EQ(finished.image.width, 16);
  EXPECT_EQ(finished.image.height, 8);
  for (size_t pixel = 0; pixel < static_cast<size_t>(finished.image.width) * finished.image.height;
       ++pixel) {
    EXPECT_TRUE(finished.image.pixels[pixel * 4 + 3] == 0 ||
                finished.image.pixels[pixel * 4 + 3] == 255);
  }
}

}  // namespace
}  // namespace zebes
