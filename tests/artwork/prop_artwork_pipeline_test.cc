#include "artwork/prop_artwork_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/strings/string_view.h"
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

TEST(PropArtworkPipelineTest, IsolationClearsEnclosedPixelsThatCloselyMatchTheBackdrop) {
  RgbaImage source = SolidImage(12, 12, RgbaColor{240, 240, 240, 255});
  PaintRect(source, 2, 2, 8, 8, RgbaColor{60, 70, 80, 255});
  PaintRect(source, 5, 5, 2, 2, RgbaColor{240, 240, 240, 255});

  ASSERT_OK_AND_ASSIGN(const RgbaImage isolated,
                       IsolateSubject(source, SubjectIsolationConfig{.minimum_subject_area = 4}));

  const size_t enclosed = (static_cast<size_t>(5) * isolated.width + 5) * 4;
  EXPECT_EQ(isolated.pixels[enclosed + 3], 0);
}

TEST(PropArtworkPipelineTest, IsolationUsesMeaningfulSourceAlphaAsAuthority) {
  RgbaImage source = SolidImage(10, 10, RgbaColor{80, 90, 100, 0});
  PaintRect(source, 3, 3, 4, 4, RgbaColor{80, 90, 100, 255});

  ASSERT_OK_AND_ASSIGN(const RgbaImage isolated,
                       IsolateSubject(source, SubjectIsolationConfig{.minimum_subject_area = 4}));

  EXPECT_EQ(isolated.pixels[3], 0);
  const size_t subject = (static_cast<size_t>(4) * isolated.width + 4) * 4;
  EXPECT_EQ(isolated.pixels[subject + 3], 255);
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

TEST(PropArtworkPipelineTest, RasterizationRequiresWholePixelBlocks) {
  const PropArtwork artwork{
      .image = SolidImage(4, 4, RgbaColor{255, 0, 0, 255}), .anchor_x = 1, .anchor_y = 3};

  const absl::Status status = RasterizeProp(artwork, PropRasterConfig{.tile_size = 3,
                                                                      .canvas_tiles_wide = 1,
                                                                      .canvas_tiles_high = 1,
                                                                      .pixel_block_size = 2})
                                  .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(PropArtworkPipelineTest, EdgeTreatmentDoesNotExpandTheSilhouette) {
  RgbaImage image = SolidImage(3, 3, RgbaColor{0, 0, 0, 0});
  PaintRect(image, 1, 1, 1, 1, RgbaColor{100, 100, 100, 255});
  const PropArtwork artwork{.image = image, .anchor_x = 1, .anchor_y = 1};

  ASSERT_OK_AND_ASSIGN(
      const PropArtwork treated,
      ApplyPropEdgeTreatment(artwork, RgbaColor{10, 20, 30, 255}, PropEdgeConfig{}));

  for (size_t pixel = 0; pixel < 9; ++pixel) {
    EXPECT_EQ(treated.image.pixels[pixel * 4 + 3], artwork.image.pixels[pixel * 4 + 3]);
  }
}

TEST(PropArtworkPipelineTest, CleanupRejectsAColourOutsideTheAcceptedPalette) {
  RgbaImage image = SolidImage(8, 8, RgbaColor{0, 0, 0, 0});
  PaintRect(image, 3, 3, 2, 2, RgbaColor{100, 100, 100, 255});
  const PropArtwork artwork{.image = image, .anchor_x = 3, .anchor_y = 3};
  const std::vector<RgbaColor> palette = {RgbaColor{10, 20, 30, 255}};

  const absl::Status status = CleanupAndValidateProp(artwork, palette, 8,
                                                     PropCleanupConfig{.minimum_component_area = 1,
                                                                       .grounded_tolerance = 0})
                                  .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
}

TEST(PropArtworkPipelineTest, AcceptedPaletteContainsEveryResolvedTerrainColour) {
  const TerrainGenConfig config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(config));
  ASSERT_OK_AND_ASSIGN(const PropPalette palette, BuildPropPalette(terrain));

  EXPECT_EQ(palette.colors, terrain.OpaqueColors());
  EXPECT_EQ(palette.outline, terrain.at(TerrainPaletteRole::kOutline));
}

TEST(PropArtworkPipelineTest, CoordinatorRetainsEveryPreviewAndProducesAValidatedProp) {
  RgbaImage source = SolidImage(32, 24, RgbaColor{236, 232, 228, 255});
  PaintRect(source, 7, 6, 18, 13, RgbaColor{74, 68, 64, 255});
  PaintRect(source, 10, 7, 8, 5, RgbaColor{126, 116, 104, 255});

  const TerrainGenConfig terrain_config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(terrain_config));
  const PropArtworkStyle style{.tile_size = 8, .palette = terrain};
  PropArtworkPipelineConfig pipeline_config;
  pipeline_config.isolation.minimum_subject_area = 16;
  pipeline_config.composition = PropCompositionConfig{
      .canvas_tiles_wide = 2, .canvas_tiles_high = 1, .padding_fraction = 0.05f};
  pipeline_config.cleanup.grounded_tolerance = 2;
  ASSERT_OK_AND_ASSIGN(const PropArtworkPipelineResult result,
                       RunPropArtworkPipeline(source, style, pipeline_config));

  EXPECT_EQ(result.pipeline_version, kPropArtworkPipelineVersion);
  EXPECT_EQ(result.source_digest.size(), 64);
  EXPECT_TRUE(result.isolated.IsValid());
  EXPECT_TRUE(result.composed.IsValid());
  EXPECT_TRUE(result.rasterized.IsValid());
  EXPECT_TRUE(result.quantized.IsValid());
  EXPECT_TRUE(result.edge_treated.IsValid());
  EXPECT_EQ(result.finished.image.width, 16);
  EXPECT_EQ(result.finished.image.height, 8);
  for (size_t index = 0; index < result.diagnostics.size(); ++index) {
    EXPECT_EQ(static_cast<size_t>(result.diagnostics[index].stage), index);
    EXPECT_GT(result.diagnostics[index].visible_pixels, 0);
  }
  for (size_t pixel = 0;
       pixel < static_cast<size_t>(result.finished.image.width) * result.finished.image.height;
       ++pixel) {
    EXPECT_TRUE(result.finished.image.pixels[pixel * 4 + 3] == 0 ||
                result.finished.image.pixels[pixel * 4 + 3] == 255);
  }
}

TEST(PropArtworkPipelineTest, CoordinatorRejectsAnInvalidStyleBeforeTransforming) {
  RgbaImage source = SolidImage(16, 16, RgbaColor{236, 232, 228, 255});
  PaintRect(source, 4, 4, 8, 8, RgbaColor{74, 68, 64, 255});
  const TerrainGenConfig terrain_config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(terrain_config));
  const PropArtworkStyle style{.tile_size = 7, .pixel_block_size = 2, .palette = terrain};
  PropArtworkPipelineConfig pipeline_config;
  pipeline_config.isolation.minimum_subject_area = 4;

  const absl::Status status = RunPropArtworkPipeline(source, style, pipeline_config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("integer pixel block"), absl::string_view::npos);
}

TEST(PropArtworkPipelineTest, CoordinatorIsByteDeterministic) {
  RgbaImage source = SolidImage(32, 24, RgbaColor{236, 232, 228, 255});
  PaintRect(source, 7, 6, 18, 13, RgbaColor{74, 68, 64, 255});
  const TerrainGenConfig terrain_config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(terrain_config));
  const PropArtworkStyle style{.palette = terrain};
  PropArtworkPipelineConfig pipeline_config;
  pipeline_config.isolation.minimum_subject_area = 16;

  ASSERT_OK_AND_ASSIGN(const PropArtworkPipelineResult first,
                       RunPropArtworkPipeline(source, style, pipeline_config));
  ASSERT_OK_AND_ASSIGN(const PropArtworkPipelineResult second,
                       RunPropArtworkPipeline(source, style, pipeline_config));

  EXPECT_EQ(first.source_digest, second.source_digest);
  EXPECT_EQ(first.finished.anchor_x, second.finished.anchor_x);
  EXPECT_EQ(first.finished.anchor_y, second.finished.anchor_y);
  EXPECT_EQ(first.finished.image.pixels, second.finished.image.pixels);
}

TEST(PropArtworkPipelineTest, CoordinatorNamesTheFailingStage) {
  RgbaImage source = SolidImage(16, 16, RgbaColor{236, 232, 228, 255});
  PaintRect(source, 0, 4, 8, 8, RgbaColor{74, 68, 64, 255});
  const TerrainGenConfig terrain_config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette terrain, ResolveTerrainPalette(terrain_config));
  const PropArtworkStyle style{.palette = terrain};
  PropArtworkPipelineConfig pipeline_config;
  pipeline_config.isolation.minimum_subject_area = 4;

  const absl::Status status = RunPropArtworkPipeline(source, style, pipeline_config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message().find("isolation:"), 0);
}

TEST(PropArtworkPipelineTest, SourceLimitsFailBeforeAnyTransformRuns) {
  const RgbaImage source = SolidImage(16, 16, RgbaColor{74, 68, 64, 255});
  const absl::Status status = ValidatePropSource(source, PropSourceLimits{.maximum_width = 8});

  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_NE(status.message().find("exceeds configured limits"), absl::string_view::npos);
}

}  // namespace
}  // namespace zebes
