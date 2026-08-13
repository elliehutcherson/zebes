#include "terrain/terrain_generator.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "gtest/gtest.h"
#include "terrain/terrain_detect.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// A flat run of ground: solid to the east, west and south with air above. Two
// of these side by side is the commonest thing a level contains and the case a
// visible seam would be most obvious in.
constexpr uint8_t kFlatTopMask = Neighbor::kEast | Neighbor::kSouthEast | Neighbor::kSouth |
                                 Neighbor::kSouthWest | Neighbor::kWest;

struct Pixel {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;

  bool operator==(const Pixel& other) const {
    return r == other.r && g == other.g && b == other.b && a == other.a;
  }
};

Pixel At(const RgbaImage& image, int x, int y) {
  const size_t index = (static_cast<size_t>(y) * image.width + x) * 4;
  return Pixel{image.pixels[index], image.pixels[index + 1], image.pixels[index + 2],
               image.pixels[index + 3]};
}

int ColorDistance(const Pixel& a, const Pixel& b) {
  return std::abs(static_cast<int>(a.r) - b.r) + std::abs(static_cast<int>(a.g) - b.g) +
         std::abs(static_cast<int>(a.b) - b.b) + std::abs(static_cast<int>(a.a) - b.a);
}

// Tests that measure the surface band turn off the interior texture, so that
// "not the interior colour" is an exact test rather than an approximate one.
TerrainGenConfig FlatInteriorConfig(int variant_period, int supersample = 2) {
  TerrainGenConfig config;
  config.tile_size = 32;
  config.supersample = supersample;
  config.variant_period = variant_period;
  config.interior.base.mottle_coverage = 0.0f;
  config.interior.pattern.density = 0;
  config.interior.details.density = 0;
  config.seed = 20260812;
  return config;
}

// How deep the surface band reaches down one column, in pixels. Everything from
// the outline through the contact shadow counts; the interior does not.
int BandDepth(const RgbaImage& image, int x) {
  const Pixel interior = At(image, x, image.height - 1);
  int depth = 0;
  for (int y = 0; y < image.height; ++y) {
    if (At(image, x, y) == interior) break;
    ++depth;
  }
  return depth;
}

TEST(TerrainGeneratorTest, RendersAFullTileForASolidNeighbourhood) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(/*mask=*/255, /*variant=*/0);
  ASSERT_TRUE(tile.ok()) << tile.status();
  ASSERT_TRUE(tile->IsValid());
  EXPECT_EQ(tile->width, 32);
  EXPECT_EQ(tile->height, 32);

  for (int y = 0; y < tile->height; ++y) {
    for (int x = 0; x < tile->width; ++x) {
      EXPECT_EQ(At(*tile, x, y).a, 255) << "fully enclosed tile has a hole at " << x << "," << y;
    }
  }
}

TEST(TerrainGeneratorTest, AnIsolatedTileIsSurfaceOnEverySide) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(/*mask=*/0, /*variant=*/0);
  ASSERT_TRUE(tile.ok()) << tile.status();

  // With air on all sides every edge pixel is outline, so no edge pixel can
  // match the tile's own centre.
  const Pixel center = At(*tile, 16, 16);
  for (int i = 0; i < 32; ++i) {
    EXPECT_FALSE(At(*tile, i, 0) == center);
    EXPECT_FALSE(At(*tile, i, 31) == center);
    EXPECT_FALSE(At(*tile, 0, i) == center);
    EXPECT_FALSE(At(*tile, 31, i) == center);
  }
}

// The seam guarantee. Two tiles that meet in a level must arrive at their
// shared border with the band at the same depth, or the surface line visibly
// steps at every tile boundary.
TEST(TerrainGeneratorTest, TheBandIsContinuousAcrossATileSeam) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(kFlatTopMask, /*variant=*/0);
  ASSERT_TRUE(tile.ok()) << tile.status();

  // The largest step the band takes between neighbouring columns inside the
  // tile is the yardstick: crossing the seam must not be worse than that.
  int largest_interior_step = 0;
  for (int x = 0; x + 1 < tile->width; ++x) {
    largest_interior_step =
        std::max(largest_interior_step, std::abs(BandDepth(*tile, x + 1) - BandDepth(*tile, x)));
  }

  const int seam_step = std::abs(BandDepth(*tile, 0) - BandDepth(*tile, tile->width - 1));
  EXPECT_LE(seam_step, largest_interior_step)
      << "the band steps by " << seam_step << " across the seam but at most "
      << largest_interior_step << " anywhere inside the tile";
}

// The same guarantee with a multi-tile pattern, where the neighbour is a
// different variant rather than an identical copy. This is what makes
// variant_period safe to raise.
TEST(TerrainGeneratorTest, TheBandIsContinuousBetweenAdjacentVariants) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/2));
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  ASSERT_EQ(renderer->variant_count(), 4);

  // A level lays variant (y mod 2) * 2 + (x mod 2), so variant 0's eastern
  // neighbour is variant 1 and variant 1's is variant 0 again.
  const absl::StatusOr<RgbaImage> left = renderer->RenderBlobTile(kFlatTopMask, /*variant=*/0);
  const absl::StatusOr<RgbaImage> right = renderer->RenderBlobTile(kFlatTopMask, /*variant=*/1);
  ASSERT_TRUE(left.ok() && right.ok());

  int largest_interior_step = 0;
  for (int x = 0; x + 1 < left->width; ++x) {
    largest_interior_step =
        std::max(largest_interior_step, std::abs(BandDepth(*left, x + 1) - BandDepth(*left, x)));
    largest_interior_step =
        std::max(largest_interior_step, std::abs(BandDepth(*right, x + 1) - BandDepth(*right, x)));
  }

  const int seam_step = std::abs(BandDepth(*right, 0) - BandDepth(*left, left->width - 1));
  EXPECT_LE(seam_step, largest_interior_step)
      << "the band steps by " << seam_step << " where variant 0 meets variant 1";
}

TEST(TerrainGeneratorTest, SurfaceColourDoesNotJumpAtTheHorizontalOrVerticalPeriodWrap) {
  constexpr int kPeriod = 3;
  for (const TerrainSurfaceStyle surface_style :
       {TerrainSurfaceStyle::kSmooth, TerrainSurfaceStyle::kTufted, TerrainSurfaceStyle::kScalloped,
        TerrainSurfaceStyle::kMossy}) {
    TerrainGenConfig config = FlatInteriorConfig(kPeriod, /*supersample=*/1);
    config.material.surface_style = surface_style;
    config.surface_texture_amount = 1.0f;
    config.surface_texture_size = 5.0f;  // Does not divide the 96px period.
    config.ruffle_amplitude = 0.0f;
    const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
    ASSERT_TRUE(renderer.ok()) << renderer.status();

    std::array<RgbaImage, kPeriod> horizontal;
    std::array<RgbaImage, kPeriod> vertical;
    // Solid north/south/east and their reachable corners, open to the west.
    constexpr uint8_t kExposedWestMask = 31;
    for (int phase = 0; phase < kPeriod; ++phase) {
      const absl::StatusOr<RgbaImage> h = renderer->RenderBlobTile(kFlatTopMask, /*variant=*/phase);
      const absl::StatusOr<RgbaImage> v =
          renderer->RenderBlobTile(kExposedWestMask, /*variant=*/phase * kPeriod);
      ASSERT_TRUE(h.ok() && v.ok());
      horizontal[phase] = *h;
      vertical[phase] = *v;
    }

    int largest_horizontal_step = 0;
    int largest_vertical_step = 0;
    for (int phase = 0; phase < kPeriod; ++phase) {
      for (int y = 0; y < config.tile_size; ++y) {
        for (int x = 0; x + 1 < config.tile_size; ++x) {
          largest_horizontal_step =
              std::max(largest_horizontal_step,
                       ColorDistance(At(horizontal[phase], x, y), At(horizontal[phase], x + 1, y)));
        }
      }
      for (int y = 0; y + 1 < config.tile_size; ++y) {
        for (int x = 0; x < config.tile_size; ++x) {
          largest_vertical_step =
              std::max(largest_vertical_step,
                       ColorDistance(At(vertical[phase], x, y), At(vertical[phase], x, y + 1)));
        }
      }
    }

    for (int i = 0; i < config.tile_size; ++i) {
      EXPECT_LE(ColorDistance(At(horizontal.back(), config.tile_size - 1, i),
                              At(horizontal.front(), 0, i)),
                largest_horizontal_step);
      EXPECT_LE(
          ColorDistance(At(vertical.back(), i, config.tile_size - 1), At(vertical.front(), i, 0)),
          largest_vertical_step);
    }
  }
}

TEST(TerrainGeneratorTest, VariantsOfTheSameMaskDiffer) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/2));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  const absl::StatusOr<RgbaImage> first = renderer->RenderBlobTile(kFlatTopMask, 0);
  const absl::StatusOr<RgbaImage> second = renderer->RenderBlobTile(kFlatTopMask, 3);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->pixels, second->pixels)
      << "every variant drew the same tile, so raising variant_period bought nothing";
}

// The outer silhouette is never displaced -- only the interior boundary
// ruffles. A slope whose artwork wandered off its polygon would not line up
// with the collision shape the tile declares.
TEST(TerrainGeneratorTest, SlopeArtworkFollowsItsPolygonExactly) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  const absl::StatusOr<RgbaImage> tile =
      renderer->RenderShapeTile(TileShape::kSlope45BottomLeft, /*variant=*/0);
  ASSERT_TRUE(tile.ok()) << tile.status();

  // Solid below the hypotenuse running from the bottom-left to the top-right,
  // air above it. Pixels straddling the line are left to the rasterizer.
  for (int y = 0; y < tile->height; ++y) {
    for (int x = 0; x < tile->width; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(tile->width);
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(tile->height);
      const float side = u + v - 1.0f;
      if (side > 0.05f) EXPECT_EQ(At(*tile, x, y).a, 255) << "hole at " << x << "," << y;
      if (side < -0.05f) EXPECT_EQ(At(*tile, x, y).a, 0) << "spill at " << x << "," << y;
    }
  }
}

TEST(TerrainGeneratorTest, EverySlopeShapeRenders) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  for (int i = 0; i < kSlopeShapeCount; ++i) {
    const TileShape shape = static_cast<TileShape>(kFirstSlopeShape + i);
    const absl::StatusOr<RgbaImage> tile = renderer->RenderShapeTile(shape, /*variant=*/0);
    ASSERT_TRUE(tile.ok()) << kTileShapeIdentifiers[static_cast<int>(shape)] << ": "
                           << tile.status();

    int opaque = 0;
    for (size_t i = 3; i < tile->pixels.size(); i += 4) {
      if (tile->pixels[i] != 0) ++opaque;
    }
    EXPECT_GT(opaque, 0) << kTileShapeIdentifiers[static_cast<int>(shape)] << " rendered nothing";
  }
}

TEST(TerrainGeneratorTest, SameConfigRendersTheSamePixels) {
  const TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1);
  const absl::StatusOr<TerrainRenderer> first = TerrainRenderer::Create(config);
  const absl::StatusOr<TerrainRenderer> second = TerrainRenderer::Create(config);
  ASSERT_TRUE(first.ok() && second.ok());

  const absl::StatusOr<RgbaImage> a = first->RenderBlobTile(kFlatTopMask, 0);
  const absl::StatusOr<RgbaImage> b = second->RenderBlobTile(kFlatTopMask, 0);
  ASSERT_TRUE(a.ok() && b.ok());
  EXPECT_EQ(a->pixels, b->pixels);
}

TEST(TerrainGeneratorTest, RejectsMasksAndVariantsItCannotDraw) {
  const absl::StatusOr<TerrainRenderer> renderer =
      TerrainRenderer::Create(FlatInteriorConfig(/*variant_period=*/1));
  ASSERT_TRUE(renderer.ok()) << renderer.status();

  // 2 is the north-east bit with no north or east to reach it around.
  EXPECT_FALSE(renderer->RenderBlobTile(/*mask=*/2, /*variant=*/0).ok());
  EXPECT_FALSE(renderer->RenderBlobTile(/*mask=*/255, /*variant=*/1).ok());
  EXPECT_FALSE(renderer->RenderShapeTile(TileShape::kNone, /*variant=*/0).ok());
}

TEST(TerrainGeneratorTest, RejectsUnusableConfigurations) {
  TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1);
  config.tile_size = 0;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/0);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.supersample = 0;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.surface_texture_amount = 1.1f;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.base.feature_size = 0.0f;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.ruffle_density = 0.0f;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.grass_bottom_bias = 1.1f;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.outline_width = -1;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.material.surface_style = static_cast<TerrainSurfaceStyle>(255);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.pattern.family = static_cast<TerrainSubstratePattern>(255);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.pattern.contrast = 1.1f;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.pattern.accent_mode = static_cast<TerrainAccentMode>(255);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.details.accent_mode = static_cast<TerrainAccentMode>(255);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.pattern.scale = 0;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.details.scale = 9;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  // A stamp magnified past the tile can never satisfy the interior test, so it
  // would place nothing and render an empty layer rather than failing.
  config = FlatInteriorConfig(/*variant_period=*/1);
  config.tile_size = 8;
  config.interior.details.family = TerrainDetailSet::kSnow;
  config.interior.details.density = 2;
  config.interior.details.scale = 4;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.interior.details.family = static_cast<TerrainDetailSet>(255);
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());

  config = FlatInteriorConfig(/*variant_period=*/1);
  config.tile_size = 4096;
  config.supersample = 2;
  EXPECT_FALSE(TerrainRenderer::Create(config).ok());
}

TEST(TerrainGeneratorTest, ResolvesMeasurementsThroughThePixelProfile) {
  TerrainGenConfig config;
  config.tile_size = 16;
  config.pixel_profile = TerrainPixelProfile::kChunky16;
  config.grass_band = 5.0f;
  config.surface_texture_size = 3.0f;
  config.interior.base.feature_size = 4.0f;

  const absl::StatusOr<ResolvedTerrainStyle> compact = ResolveTerrainStyle(config);
  ASSERT_TRUE(compact.ok()) << compact.status();
  EXPECT_TRUE(compact->compact_palette);
  EXPECT_FLOAT_EQ(compact->grass_band, 5.0f);
  EXPECT_EQ(compact->surface_texture_size, 3);
  EXPECT_EQ(compact->interior_feature_size, 4);

  // Tile size and detail policy are independent. Rendering the same Chunky16
  // design on a 32px grid doubles its concrete measurements; it does not
  // silently become the Balanced32 policy.
  config.tile_size = 32;
  const absl::StatusOr<ResolvedTerrainStyle> enlarged = ResolveTerrainStyle(config);
  ASSERT_TRUE(enlarged.ok()) << enlarged.status();
  EXPECT_TRUE(enlarged->compact_palette);
  EXPECT_FLOAT_EQ(enlarged->grass_band, 10.0f);
  EXPECT_EQ(enlarged->surface_texture_size, 6);
  EXPECT_EQ(enlarged->interior_feature_size, 8);
}

TEST(TerrainGeneratorTest, ShipsRichAndChunkyPresetsAsCompleteConfigurations) {
  const absl::Span<const TerrainPreset> presets = BuiltInTerrainPresets();
  const auto cozy = std::find_if(presets.begin(), presets.end(), [](const TerrainPreset& preset) {
    return preset.name == "Cozy Meadow";
  });
  const auto chunky = std::find_if(presets.begin(), presets.end(), [](const TerrainPreset& preset) {
    return preset.name == "Chunky Grass 16";
  });
  ASSERT_NE(cozy, presets.end());
  ASSERT_NE(chunky, presets.end());

  EXPECT_EQ(cozy->config.pixel_profile, TerrainPixelProfile::kBalanced32);
  EXPECT_EQ(cozy->config.material.surface_style, TerrainSurfaceStyle::kScalloped);
  EXPECT_EQ(cozy->config.interior.base.style, TerrainInteriorStyle::kSoilClods);
  EXPECT_EQ(cozy->config.interior.pattern.family, TerrainSubstratePattern::kMixedEarth);
  EXPECT_EQ(cozy->config.interior.details.family, TerrainDetailSet::kMeadow);
  EXPECT_EQ(cozy->config.variant_period, 3);

  EXPECT_EQ(chunky->config.tile_size, 16);
  EXPECT_EQ(chunky->config.pixel_profile, TerrainPixelProfile::kChunky16);
  EXPECT_LE(chunky->config.interior.pattern.density, 1);
  EXPECT_LE(chunky->config.interior.details.density, 1);

  TerrainGenConfig quick = chunky->config;
  quick.supersample = 1;
  const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(quick);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(kFlatTopMask, 0);
  ASSERT_TRUE(tile.ok()) << tile.status();
  EXPECT_EQ(tile->width, 16);
  EXPECT_EQ(tile->height, 16);
}

TEST(TerrainGeneratorTest, EveryBuiltInPresetCreatesARenderer) {
  for (const TerrainPreset& preset : BuiltInTerrainPresets()) {
    TerrainGenConfig quick = preset.config;
    quick.supersample = 1;
    const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(quick);
    ASSERT_TRUE(renderer.ok()) << preset.name << ": " << renderer.status();
    const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(kFlatTopMask, 0);
    EXPECT_TRUE(tile.ok()) << preset.name << ": " << tile.status();
  }
}

TEST(TerrainGeneratorTest, SubstratePatternDoesNotMoveWhenTheNeighborMaskChanges) {
  TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  config.interior.base.style = TerrainInteriorStyle::kFlat;
  config.interior.pattern.family = TerrainSubstratePattern::kPebbles;
  config.interior.pattern.density = 10;
  config.interior.pattern.spacing = 1;
  config.interior.pattern.margin = 0;

  const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  const absl::StatusOr<RgbaImage> enclosed = renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> exposed = renderer->RenderBlobTile(kFlatTopMask, 0);
  ASSERT_TRUE(enclosed.ok() && exposed.ok());

  std::set<std::array<uint8_t, 4>> colors;
  for (int y = 20; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      const Pixel a = At(*enclosed, x, y);
      const Pixel b = At(*exposed, x, y);
      EXPECT_EQ(a, b) << "substrate pattern moved at " << x << "," << y
                      << " when only the northern edge changed";
      colors.insert({a.r, a.g, a.b, a.a});
    }
  }
  EXPECT_GT(colors.size(), 1u) << "comparison region did not exercise the substrate pattern";
}

// Magnified stamps do their own wrapping and clipping arithmetic, so the
// property that a mark is anchored to the field rather than to the tile has to
// hold at size > 1 as well.
TEST(TerrainGeneratorTest, MagnifiedSubstratePatternDoesNotMoveWhenTheNeighborMaskChanges) {
  TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  config.interior.base.style = TerrainInteriorStyle::kFlat;
  config.interior.pattern.family = TerrainSubstratePattern::kPebbles;
  config.interior.pattern.density = 10;
  config.interior.pattern.spacing = 1;
  config.interior.pattern.margin = 0;
  config.interior.pattern.scale = 3;

  const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  const absl::StatusOr<RgbaImage> enclosed = renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> exposed = renderer->RenderBlobTile(kFlatTopMask, 0);
  ASSERT_TRUE(enclosed.ok() && exposed.ok());

  std::set<std::array<uint8_t, 4>> colors;
  for (int y = 20; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      const Pixel a = At(*enclosed, x, y);
      const Pixel b = At(*exposed, x, y);
      EXPECT_EQ(a, b) << "magnified substrate pattern moved at " << x << "," << y
                      << " when only the northern edge changed";
      colors.insert({a.r, a.g, a.b, a.a});
    }
  }
  EXPECT_GT(colors.size(), 1u) << "comparison region did not exercise the substrate pattern";
}

TEST(TerrainGeneratorTest, SemanticDetailsDoNotMoveWhenTheNeighborMaskChanges) {
  TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  config.interior.base.style = TerrainInteriorStyle::kFlat;
  config.interior.pattern.family = TerrainSubstratePattern::kNone;
  config.interior.details.family = TerrainDetailSet::kMeadow;
  config.interior.details.density = 10;
  config.interior.details.spacing = 1;
  config.interior.details.margin = 0;

  const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  const absl::StatusOr<RgbaImage> enclosed = renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> exposed = renderer->RenderBlobTile(kFlatTopMask, 0);
  ASSERT_TRUE(enclosed.ok() && exposed.ok());

  std::set<std::array<uint8_t, 4>> colors;
  for (int y = 20; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      const Pixel a = At(*enclosed, x, y);
      const Pixel b = At(*exposed, x, y);
      EXPECT_EQ(a, b) << "semantic detail moved at " << x << "," << y
                      << " when only the northern edge changed";
      colors.insert({a.r, a.g, a.b, a.a});
    }
  }
  EXPECT_GT(colors.size(), 1u) << "comparison region did not exercise semantic details";
}

TEST(TerrainGeneratorTest, SemanticDetailsPreserveTheSubstratePatternBelowThem) {
  TerrainGenConfig blank = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  blank.interior.base.style = TerrainInteriorStyle::kFlat;
  blank.interior.pattern.family = TerrainSubstratePattern::kNone;
  blank.interior.details.family = TerrainDetailSet::kNone;

  TerrainGenConfig pattern = blank;
  pattern.interior.pattern.family = TerrainSubstratePattern::kPebbles;
  pattern.interior.pattern.density = 10;
  pattern.interior.pattern.spacing = 1;
  pattern.interior.pattern.margin = 0;

  TerrainGenConfig combined = pattern;
  combined.interior.details.family = TerrainDetailSet::kMeadow;
  combined.interior.details.density = 10;
  combined.interior.details.spacing = 1;
  combined.interior.details.margin = 0;

  const absl::StatusOr<TerrainRenderer> blank_renderer = TerrainRenderer::Create(blank);
  const absl::StatusOr<TerrainRenderer> pattern_renderer = TerrainRenderer::Create(pattern);
  const absl::StatusOr<TerrainRenderer> combined_renderer = TerrainRenderer::Create(combined);
  ASSERT_TRUE(blank_renderer.ok() && pattern_renderer.ok() && combined_renderer.ok());

  const absl::StatusOr<RgbaImage> blank_tile = blank_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> pattern_tile = pattern_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> combined_tile =
      combined_renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(blank_tile.ok() && pattern_tile.ok() && combined_tile.ok());

  int pattern_pixels = 0;
  int detail_pixels = 0;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      const Pixel base = At(*blank_tile, x, y);
      const Pixel substrate = At(*pattern_tile, x, y);
      const Pixel full = At(*combined_tile, x, y);
      if (!(substrate == base)) {
        ++pattern_pixels;
        EXPECT_EQ(full, substrate) << "semantic details overwrote substrate at " << x << "," << y;
      }
      if (!(full == substrate)) ++detail_pixels;
    }
  }
  EXPECT_GT(pattern_pixels, 0);
  EXPECT_GT(detail_pixels, 0);
}

TEST(TerrainGeneratorTest, PatternContrastCanHideMarksWithoutChangingTheirFamily) {
  TerrainGenConfig blank = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  blank.interior.base.style = TerrainInteriorStyle::kFlat;
  blank.interior.pattern.family = TerrainSubstratePattern::kNone;

  TerrainGenConfig quiet = blank;
  quiet.interior.pattern.family = TerrainSubstratePattern::kMixedEarth;
  quiet.interior.pattern.density = 10;
  quiet.interior.pattern.spacing = 1;
  quiet.interior.pattern.margin = 0;
  quiet.interior.pattern.contrast = 0.0f;

  TerrainGenConfig strong = quiet;
  strong.interior.pattern.contrast = 1.0f;

  const absl::StatusOr<TerrainRenderer> blank_renderer = TerrainRenderer::Create(blank);
  const absl::StatusOr<TerrainRenderer> quiet_renderer = TerrainRenderer::Create(quiet);
  const absl::StatusOr<TerrainRenderer> strong_renderer = TerrainRenderer::Create(strong);
  ASSERT_TRUE(blank_renderer.ok() && quiet_renderer.ok() && strong_renderer.ok());

  const absl::StatusOr<RgbaImage> blank_tile = blank_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> quiet_tile = quiet_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> strong_tile = strong_renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(blank_tile.ok() && quiet_tile.ok() && strong_tile.ok());
  EXPECT_EQ(quiet_tile->pixels, blank_tile->pixels);
  EXPECT_NE(strong_tile->pixels, blank_tile->pixels);
}

// A tile whose interior is deliberately featureless, so any pixel that is not
// the interior colour is a motif and can be counted.
TerrainGenConfig MotifOnlyConfig() {
  TerrainGenConfig config = FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1);
  config.interior.base.style = TerrainInteriorStyle::kFlat;
  config.interior.pattern.family = TerrainSubstratePattern::kNone;
  config.interior.details.family = TerrainDetailSet::kNone;
  return config;
}

// Colours covering the interior of a fully enclosed tile, and how many pixels
// each covers. The interior colour itself is excluded, so what is left is
// exactly the motif layer.
std::map<std::array<uint8_t, 4>, int> MotifColors(const RgbaImage& tile) {
  const Pixel interior = At(tile, tile.width / 2, tile.height / 2);
  std::map<std::array<uint8_t, 4>, int> colors;
  for (int y = 0; y < tile.height; ++y) {
    for (int x = 0; x < tile.width; ++x) {
      const Pixel pixel = At(tile, x, y);
      if (pixel == interior) continue;
      ++colors[{pixel.r, pixel.g, pixel.b, pixel.a}];
    }
  }
  return colors;
}

int TotalPixels(const std::map<std::array<uint8_t, 4>, int>& colors) {
  int total = 0;
  for (const auto& [color, count] : colors) total += count;
  return total;
}

// A single mark, so the comparison isolates the size of a stamp from how many
// of them the placement builder fits. Spacing grows with size deliberately, so
// at a higher density a larger size draws fewer, bigger marks.
TEST(TerrainGeneratorTest, MotifSizeQuadruplesTheAreaAMarkCovers) {
  TerrainGenConfig small = MotifOnlyConfig();
  small.interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  small.interior.pattern.density = 1;
  small.interior.pattern.margin = 0;

  TerrainGenConfig large = small;
  large.interior.pattern.scale = 2;

  const absl::StatusOr<TerrainRenderer> small_renderer = TerrainRenderer::Create(small);
  const absl::StatusOr<TerrainRenderer> large_renderer = TerrainRenderer::Create(large);
  ASSERT_TRUE(small_renderer.ok()) << small_renderer.status();
  ASSERT_TRUE(large_renderer.ok()) << large_renderer.status();

  const absl::StatusOr<RgbaImage> small_tile = small_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> large_tile = large_renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(small_tile.ok()) << small_tile.status();
  ASSERT_TRUE(large_tile.ok()) << large_tile.status();

  const int small_covered = TotalPixels(MotifColors(*small_tile));
  const int large_covered = TotalPixels(MotifColors(*large_tile));
  ASSERT_GT(small_covered, 0) << "the size-1 tile drew no mark to compare against";
  // Doubling both axes of a stamp quadruples the pixels it covers.
  EXPECT_EQ(large_covered, small_covered * 4)
      << "size 2 covered " << large_covered << " pixels, not four times size 1's " << small_covered;
}

// Magnifying a stamp must magnify its shape, not resample it. Every colour a
// magnified diamond uses has to be one the unmagnified diamond already used.
TEST(TerrainGeneratorTest, MotifSizeIntroducesNoNewColours) {
  TerrainGenConfig small = MotifOnlyConfig();
  small.interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  small.interior.pattern.density = 4;
  small.interior.pattern.margin = 0;

  TerrainGenConfig large = small;
  large.interior.pattern.scale = 3;

  const absl::StatusOr<TerrainRenderer> small_renderer = TerrainRenderer::Create(small);
  const absl::StatusOr<TerrainRenderer> large_renderer = TerrainRenderer::Create(large);
  ASSERT_TRUE(small_renderer.ok()) << small_renderer.status();
  ASSERT_TRUE(large_renderer.ok()) << large_renderer.status();

  const absl::StatusOr<RgbaImage> small_tile = small_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> large_tile = large_renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(small_tile.ok() && large_tile.ok());

  const std::map<std::array<uint8_t, 4>, int> small_colors = MotifColors(*small_tile);
  const std::map<std::array<uint8_t, 4>, int> large_colors = MotifColors(*large_tile);
  ASSERT_FALSE(small_colors.empty());
  ASSERT_FALSE(large_colors.empty());
  for (const auto& [color, count] : large_colors) {
    EXPECT_TRUE(small_colors.count(color) != 0)
        << "magnifying the stamp invented a colour the source art does not contain";
  }
}

// The substrate pattern had no route to the accent colours at all before the
// accent mode existed: it was hardcoded to the snow and crystal detail sets.
TEST(TerrainGeneratorTest, SubstratePatternsCanUseAccentColours) {
  TerrainGenConfig material = MotifOnlyConfig();
  material.interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  material.interior.pattern.density = 6;
  material.interior.pattern.margin = 0;
  material.material.accent_primary = 0xff0000;
  material.material.accent_secondary = 0x0000ff;

  TerrainGenConfig accented = material;
  accented.interior.pattern.accent_mode = TerrainAccentMode::kAccent;

  const absl::StatusOr<TerrainRenderer> material_renderer = TerrainRenderer::Create(material);
  const absl::StatusOr<TerrainRenderer> accent_renderer = TerrainRenderer::Create(accented);
  ASSERT_TRUE(material_renderer.ok()) << material_renderer.status();
  ASSERT_TRUE(accent_renderer.ok()) << accent_renderer.status();

  const absl::StatusOr<RgbaImage> material_tile = material_renderer->RenderBlobTile(255, 0);
  const absl::StatusOr<RgbaImage> accent_tile = accent_renderer->RenderBlobTile(255, 0);
  ASSERT_TRUE(material_tile.ok() && accent_tile.ok());

  const std::array<uint8_t, 4> red{0xff, 0x00, 0x00, 0xff};
  const std::array<uint8_t, 4> blue{0x00, 0x00, 0xff, 0xff};
  const std::map<std::array<uint8_t, 4>, int> material_colors = MotifColors(*material_tile);
  const std::map<std::array<uint8_t, 4>, int> accent_colors = MotifColors(*accent_tile);
  ASSERT_FALSE(material_colors.empty()) << "no marks were drawn to colour";

  EXPECT_EQ(material_colors.count(red), 0u);
  EXPECT_EQ(material_colors.count(blue), 0u);
  EXPECT_GT(accent_colors.count(red) + accent_colors.count(blue), 0u)
      << "accent mode left the substrate marks on the material ramp";
}

// The point of the gradient: a swept motif holds more than the two endpoint
// colours a flat accent gives it.
TEST(TerrainGeneratorTest, GradientAccentSweepsBetweenTheAccentColours) {
  TerrainGenConfig flat = MotifOnlyConfig();
  flat.interior.details.family = TerrainDetailSet::kCrystals;
  flat.interior.details.density = 6;
  flat.interior.details.margin = 0;
  flat.interior.details.scale = 2;
  flat.interior.details.accent_mode = TerrainAccentMode::kAccent;
  flat.material.accent_primary = 0xffd000;
  flat.material.accent_secondary = 0x00b0ff;

  TerrainGenConfig swept = flat;
  swept.interior.details.accent_mode = TerrainAccentMode::kGradient;

  const absl::StatusOr<TerrainRenderer> flat_renderer = TerrainRenderer::Create(flat);
  const absl::StatusOr<TerrainRenderer> swept_renderer = TerrainRenderer::Create(swept);
  ASSERT_TRUE(flat_renderer.ok()) << flat_renderer.status();
  ASSERT_TRUE(swept_renderer.ok()) << swept_renderer.status();

  const absl::StatusOr<RgbaImage> flat_tile = flat_renderer->RenderBlobTile(/*mask=*/255, 0);
  const absl::StatusOr<RgbaImage> swept_tile = swept_renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(flat_tile.ok() && swept_tile.ok());

  const std::map<std::array<uint8_t, 4>, int> flat_colors = MotifColors(*flat_tile);
  const std::map<std::array<uint8_t, 4>, int> swept_colors = MotifColors(*swept_tile);
  ASSERT_FALSE(flat_colors.empty()) << "no crystals were drawn to colour";
  EXPECT_EQ(flat_colors.size(), 2u) << "flat accent should use exactly the two accent colours";
  EXPECT_GT(swept_colors.size(), flat_colors.size())
      << "the gradient produced no more colours than the flat accent pair";

  // The ramp's endpoints must be the authored colours exactly, so introducing
  // it changed nothing about how flat accent shading already looked.
  const std::array<uint8_t, 4> primary{0xff, 0xd0, 0x00, 0xff};
  const std::array<uint8_t, 4> secondary{0x00, 0xb0, 0xff, 0xff};
  EXPECT_EQ(flat_colors.count(primary), 1u) << "flat accent lost the authored primary colour";
  EXPECT_EQ(flat_colors.count(secondary), 1u) << "flat accent lost the authored secondary colour";
  // The sweep runs between those same endpoints rather than beside them.
  EXPECT_EQ(swept_colors.count(primary), 1u) << "the gradient never reaches the primary colour";
  EXPECT_EQ(swept_colors.count(secondary), 1u) << "the gradient never reaches the secondary colour";
}

// A sweep between two saturated colours must not pass through grey; that is the
// whole reason the ramp is mixed in HSV along the shorter hue arc.
TEST(TerrainGeneratorTest, TheAccentGradientStaysSaturated) {
  TerrainGenConfig config = MotifOnlyConfig();
  config.interior.details.family = TerrainDetailSet::kCrystals;
  config.interior.details.density = 6;
  config.interior.details.margin = 0;
  config.interior.details.scale = 2;
  config.interior.details.accent_mode = TerrainAccentMode::kGradient;
  config.material.accent_primary = 0xffd000;
  config.material.accent_secondary = 0x00b0ff;

  const absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(config);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  const absl::StatusOr<RgbaImage> tile = renderer->RenderBlobTile(/*mask=*/255, 0);
  ASSERT_TRUE(tile.ok()) << tile.status();

  const std::map<std::array<uint8_t, 4>, int> colors = MotifColors(*tile);
  ASSERT_GT(colors.size(), 2u) << "the gradient did not produce intermediate steps to check";
  for (const auto& [color, count] : colors) {
    const int high = std::max({color[0], color[1], color[2]});
    const int low = std::min({color[0], color[1], color[2]});
    // Both endpoints are fully saturated, so every step between them should be
    // too. An RGB blend would collapse to near-grey in the middle.
    EXPECT_GT(high - low, 100) << "a gradient step desaturated to " << high - low
                               << "; the mix is passing through grey";
  }
}

// Turning both layers back to material colours must reproduce the artwork the
// generator drew before accent modes existed.
TEST(TerrainGeneratorTest, MaterialAccentModeIsTheDefaultForNonAccentFamilies) {
  EXPECT_EQ(DefaultAccentModeFor(TerrainDetailSet::kMeadow), TerrainAccentMode::kMaterial);
  EXPECT_EQ(DefaultAccentModeFor(TerrainDetailSet::kForestFloor), TerrainAccentMode::kMaterial);
  EXPECT_EQ(DefaultAccentModeFor(TerrainDetailSet::kNone), TerrainAccentMode::kMaterial);
  EXPECT_EQ(DefaultAccentModeFor(TerrainDetailSet::kSnow), TerrainAccentMode::kAccent);
  EXPECT_EQ(DefaultAccentModeFor(TerrainDetailSet::kCrystals), TerrainAccentMode::kAccent);

  const TerrainGenConfig defaults;
  EXPECT_EQ(defaults.interior.pattern.accent_mode, TerrainAccentMode::kMaterial);
  EXPECT_EQ(defaults.interior.details.accent_mode, TerrainAccentMode::kMaterial);
  EXPECT_EQ(defaults.interior.pattern.scale, 1);
  EXPECT_EQ(defaults.interior.details.scale, 1);
}

// The atlas has to be laid out exactly the way ComposeBlob47 lays out
// hand-drawn art, or the importer and every downstream test would need a second
// code path for generated sets.
TEST(TerrainGeneratorTest, AtlasMatchesTheComposedLayout) {
  const absl::StatusOr<Blob47Atlas> atlas =
      GenerateBlob47Atlas(FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  EXPECT_EQ(atlas->tile_size, 32);
  EXPECT_EQ(atlas->image.width, kBlob47Columns * 32);
  EXPECT_TRUE(atlas->image.IsValid());
  ASSERT_EQ(atlas->tiles.size(), static_cast<size_t>(kBlob47TileCount));
  EXPECT_EQ(atlas->slopes.size(), static_cast<size_t>(kSlopeShapeCount));

  const absl::Span<const uint8_t> masks = Blob47MaskTable();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    const ComposedTile& tile = atlas->tiles[i];
    EXPECT_EQ(tile.index, i);
    EXPECT_EQ(tile.mask, masks[i]);
    EXPECT_EQ(tile.variant, 0);
    EXPECT_EQ(tile.source_x, (i % kBlob47Columns) * 32);
    EXPECT_EQ(tile.source_y, (i / kBlob47Columns) * 32);
  }

  for (int i = 0; i < kSlopeShapeCount; ++i) {
    EXPECT_EQ(static_cast<int>(atlas->slopes[i].shape), kFirstSlopeShape + i);
  }
}

TEST(TerrainGeneratorTest, AtlasStacksOneBlockPerVariantThenTheSlopes) {
  const absl::StatusOr<Blob47Atlas> atlas =
      GenerateBlob47Atlas(FlatInteriorConfig(/*variant_period=*/2, /*supersample=*/1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  EXPECT_EQ(atlas->tiles.size(), static_cast<size_t>(kBlob47TileCount) * 4);

  const int slope_rows = (kSlopeShapeCount + kBlob47Columns - 1) / kBlob47Columns;
  EXPECT_EQ(atlas->image.height, (kBlob47Rows * 4 + slope_rows) * 32);

  for (const ComposedTile& tile : atlas->tiles) {
    EXPECT_EQ(tile.source_x % 32, 0);
    EXPECT_EQ(tile.source_y % 32, 0);
    EXPECT_LT(tile.source_x, atlas->image.width);
    EXPECT_LT(tile.source_y, atlas->image.height);
    EXPECT_EQ(tile.source_y / 32 / kBlob47Rows, tile.variant);
  }
  for (const ComposedSlope& slope : atlas->slopes) {
    EXPECT_GE(slope.source_y, kBlob47Rows * 4 * 32);
    EXPECT_LT(slope.source_y, atlas->image.height);
  }
}

// Every cell the manifest names has to actually contain artwork, or the brush
// paints a hole the first time that neighbourhood comes up.
TEST(TerrainGeneratorTest, EveryAtlasCellHasArtwork) {
  const absl::StatusOr<Blob47Atlas> atlas =
      GenerateBlob47Atlas(FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  for (const ComposedTile& tile : atlas->tiles) {
    int opaque = 0;
    for (int y = 0; y < atlas->tile_size; ++y) {
      for (int x = 0; x < atlas->tile_size; ++x) {
        if (At(atlas->image, tile.source_x + x, tile.source_y + y).a != 0) ++opaque;
      }
    }
    EXPECT_GT(opaque, 0) << "mask " << static_cast<int>(tile.mask) << " is blank";
  }
}

// A generated atlas has to arrive at a terrain by the same road a composed one
// does. This is the join between the generator and the importer, and it is the
// test that would catch the generator drifting away from the layout the rest of
// the pipeline assumes.
TEST(TerrainGeneratorTest, AGeneratedAtlasBuildsAPaintableTerrain) {
  const absl::StatusOr<Blob47Atlas> atlas =
      GenerateBlob47Atlas(FlatInteriorConfig(/*variant_period=*/2, /*supersample=*/1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  const absl::StatusOr<TerrainCandidate> candidate =
      BuildTerrainCandidate(*atlas, /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  // One rule per mask, in the table's order, each carrying every variant.
  ASSERT_EQ(candidate->terrain.rules.size(), static_cast<size_t>(kBlob47TileCount));
  const absl::Span<const uint8_t> masks = Blob47MaskTable();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    EXPECT_EQ(candidate->terrain.rules[i].mask, masks[i]);
    EXPECT_EQ(candidate->terrain.rules[i].variants.size(), 4u);
  }

  // Slopes are members, never rules: the brush must not try to paint them.
  EXPECT_EQ(candidate->terrain.member_tile_ids.size(), static_cast<size_t>(kSlopeShapeCount));
  EXPECT_EQ(candidate->tiles.size(), static_cast<size_t>(kBlob47TileCount) * 4 + kSlopeShapeCount);

  absl::flat_hash_set<int> tile_ids;
  for (const Tile& tile : candidate->tiles) {
    EXPECT_GT(tile.id, 0) << "0 is reserved for empty";
    EXPECT_TRUE(tile_ids.insert(tile.id).second) << "duplicate tile ID " << tile.id;
    EXPECT_FALSE(tile.name.empty());
  }

  // Every variant and member has to name a tile that exists, or painting hits a
  // dangling ID.
  for (const TerrainRule& rule : candidate->terrain.rules) {
    for (const TerrainVariant& variant : rule.variants) {
      EXPECT_TRUE(tile_ids.contains(variant.tile_id))
          << "mask " << static_cast<int>(rule.mask) << " points at missing tile";
      EXPECT_GT(variant.weight, 0);
    }
  }
  for (const int member : candidate->terrain.member_tile_ids) {
    EXPECT_TRUE(tile_ids.contains(member));
  }
}

TEST(TerrainGeneratorTest, GeneratedTilesCarryTheRightShapes) {
  const absl::StatusOr<Blob47Atlas> atlas =
      GenerateBlob47Atlas(FlatInteriorConfig(/*variant_period=*/1, /*supersample=*/1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();
  const absl::StatusOr<TerrainCandidate> candidate = BuildTerrainCandidate(*atlas, 1, 1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  const absl::flat_hash_set<int> members(candidate->terrain.member_tile_ids.begin(),
                                         candidate->terrain.member_tile_ids.end());
  for (const Tile& tile : candidate->tiles) {
    if (members.contains(tile.id)) {
      EXPECT_GE(static_cast<int>(tile.shape), kFirstSlopeShape) << tile.name;
      EXPECT_LT(static_cast<int>(tile.shape), kFirstSlopeShape + kSlopeShapeCount) << tile.name;
      continue;
    }
    EXPECT_EQ(tile.shape, TileShape::kFullBlock) << tile.name << " is painted by the brush";
  }
}

}  // namespace
}  // namespace zebes
