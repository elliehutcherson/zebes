#include "terrain/terrain_field.h"

#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

// The reference the fast transform has to agree with. Quadratic, so it is only
// usable on small grids -- which is exactly what a test wants.
std::vector<float> BruteForceSquaredDistance(const std::vector<uint8_t>& solid, int width,
                                             int height) {
  std::vector<float> distance(static_cast<size_t>(width) * height, 0.0f);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (solid[static_cast<size_t>(y) * width + x] == 0) continue;
      float best = 1e18f;
      for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
          if (solid[static_cast<size_t>(oy) * width + ox] != 0) continue;
          const float dx = static_cast<float>(x - ox);
          const float dy = static_cast<float>(y - oy);
          best = std::min(best, dx * dx + dy * dy);
        }
      }
      distance[static_cast<size_t>(y) * width + x] = best;
    }
  }
  return distance;
}

std::vector<uint8_t> PatternedGrid(int width, int height, int modulus) {
  std::vector<uint8_t> solid(static_cast<size_t>(width) * height, 1);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if ((x * 7 + y * 13) % modulus == 0) solid[static_cast<size_t>(y) * width + x] = 0;
    }
  }
  return solid;
}

TEST(SquaredDistanceTransformTest, EmptyPixelsAreZero) {
  const std::vector<uint8_t> solid = {0, 1, 0, 1};
  const std::vector<float> distance = SquaredDistanceTransform(solid, 2, 2);
  EXPECT_EQ(distance[0], 0.0f);
  EXPECT_EQ(distance[2], 0.0f);
}

TEST(SquaredDistanceTransformTest, MatchesBruteForceOnPatternedGrids) {
  for (const int modulus : {3, 5, 11}) {
    const int width = 17;
    const int height = 13;
    const std::vector<uint8_t> solid = PatternedGrid(width, height, modulus);

    const std::vector<float> fast = SquaredDistanceTransform(solid, width, height);
    const std::vector<float> reference = BruteForceSquaredDistance(solid, width, height);

    for (size_t i = 0; i < fast.size(); ++i) {
      EXPECT_NEAR(fast[i], reference[i], 1e-3f)
          << "modulus " << modulus << " differs at index " << i;
    }
  }
}

TEST(SquaredDistanceTransformTest, DistanceIsMeasuredDiagonallyNotInSteps) {
  // A single hole: the pixel one step diagonally away is sqrt(2) from it, which
  // a city-block transform would call 2.
  std::vector<uint8_t> solid(25, 1);
  solid[12] = 0;
  const std::vector<float> distance = SquaredDistanceTransform(solid, 5, 5);
  EXPECT_FLOAT_EQ(distance[6], 2.0f);
  EXPECT_FLOAT_EQ(distance[7], 1.0f);
}

TEST(SquaredDistanceTransformTest, AGridWithNoHolesIsEntirelyInterior) {
  const std::vector<uint8_t> solid(16, 1);
  const std::vector<float> distance = SquaredDistanceTransform(solid, 4, 4);
  for (const float value : distance) EXPECT_GT(value, 1e6f);
}

// The property the whole seam story rests on: sampling one period away lands on
// exactly the same value, so two tiles that neighbour each other in a level
// agree about the band depth along their shared border.
TEST(RuffleFieldTest, IsExactlyPeriodic) {
  const absl::StatusOr<RuffleField> field =
      RuffleField::Create(/*period_px=*/64, /*tile_px=*/32, /*density=*/2.0f,
                          /*sharpness=*/0.65f, /*octaves=*/2, /*seed=*/7);
  ASSERT_TRUE(field.ok()) << field.status();

  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      EXPECT_EQ(field->Value(x, y), field->Value(x + 64, y));
      EXPECT_EQ(field->Value(x, y), field->Value(x, y + 64));
      EXPECT_EQ(field->Value(x, y), field->Value(x - 64, y - 64));
    }
  }
}

// Asking for a multi-tile period has to actually buy a multi-tile pattern.
// Rounding the requested density to a whole number of repeats per tile makes
// every frequency a multiple of the tile count, and the field quietly collapses
// back to repeating every single tile -- which looks identical to period 1 and
// makes the extra variants it costs pure waste.
TEST(RuffleFieldTest, AMultiTilePeriodDoesNotCollapseToOneTile) {
  for (const int tiles : {2, 3, 4}) {
    const absl::StatusOr<RuffleField> field =
        RuffleField::Create(/*period_px=*/32 * tiles, /*tile_px=*/32, /*density=*/2.0f,
                            /*sharpness=*/1.0f, /*octaves=*/1, /*seed=*/11);
    ASSERT_TRUE(field.ok()) << field.status();

    bool differs_within_period = false;
    for (int y = 0; y < 32 * tiles && !differs_within_period; ++y) {
      for (int x = 0; x < 32 * tiles; ++x) {
        if (field->Value(x, y) != field->Value(x + 32, y)) {
          differs_within_period = true;
          break;
        }
      }
    }
    EXPECT_TRUE(differs_within_period) << "a " << tiles << "-tile period repeats every tile anyway";
  }
}

TEST(RuffleFieldTest, StaysWithinTheUnitRange) {
  const absl::StatusOr<RuffleField> field = RuffleField::Create(96, 32, 3.0f, 1.0f, 1, 99);
  ASSERT_TRUE(field.ok()) << field.status();

  for (int y = 0; y < 96; ++y) {
    for (int x = 0; x < 96; ++x) {
      EXPECT_GE(field->Value(x, y), 0.0f);
      EXPECT_LE(field->Value(x, y), 1.0f);
    }
  }
}

// A washed-out field would produce a flat band and defeat the point of ruffling
// at all, which is what the contrast stretch inside Create exists to prevent.
TEST(RuffleFieldTest, UsesTheFullContrastRange) {
  const absl::StatusOr<RuffleField> field = RuffleField::Create(64, 32, 2.0f, 0.65f, 1, 3);
  ASSERT_TRUE(field.ok()) << field.status();

  float low = 1.0f;
  float high = 0.0f;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      low = std::min(low, field->Value(x, y));
      high = std::max(high, field->Value(x, y));
    }
  }
  EXPECT_LT(low, 0.1f);
  EXPECT_GT(high, 0.9f);
}

TEST(RuffleFieldTest, SameSeedGivesTheSameField) {
  const absl::StatusOr<RuffleField> first = RuffleField::Create(64, 32, 2.0f, 0.65f, 1, 42);
  const absl::StatusOr<RuffleField> second = RuffleField::Create(64, 32, 2.0f, 0.65f, 1, 42);
  const absl::StatusOr<RuffleField> other = RuffleField::Create(64, 32, 2.0f, 0.65f, 1, 43);
  ASSERT_TRUE(first.ok() && second.ok() && other.ok());

  bool differs_from_other = false;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      EXPECT_EQ(first->Value(x, y), second->Value(x, y));
      if (first->Value(x, y) != other->Value(x, y)) differs_from_other = true;
    }
  }
  EXPECT_TRUE(differs_from_other) << "a different seed produced an identical field";
}

TEST(ValueNoiseFieldTest, WrapsOnItsPeriod) {
  const absl::StatusOr<ValueNoiseField> field =
      ValueNoiseField::Create(/*period_px=*/64, /*cells_per_period=*/5, /*octaves=*/3, /*seed=*/5);
  ASSERT_TRUE(field.ok()) << field.status();

  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      EXPECT_EQ(field->Value(x, y), field->Value(x + 64, y + 64));
      EXPECT_EQ(field->Value(x, y), field->Value(x - 64, y));
    }
  }
}

TEST(ValueNoiseFieldTest, SpansTheUnitRange) {
  const absl::StatusOr<ValueNoiseField> field = ValueNoiseField::Create(64, 4, 3, 17);
  ASSERT_TRUE(field.ok()) << field.status();

  float low = 1.0f;
  float high = 0.0f;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      low = std::min(low, field->Value(x, y));
      high = std::max(high, field->Value(x, y));
    }
  }
  EXPECT_FLOAT_EQ(low, 0.0f);
  EXPECT_FLOAT_EQ(high, 1.0f);
}

// The reason this field exists rather than more sinusoids: no harmonic
// structure, so nothing lines up into a lattice. Sampling along a row of a
// sinusoid field revisits the same few values; noise should not.
TEST(ValueNoiseFieldTest, DoesNotRepeatWithinItsPeriod) {
  const absl::StatusOr<ValueNoiseField> field = ValueNoiseField::Create(64, 4, 3, 23);
  ASSERT_TRUE(field.ok()) << field.status();

  for (const int stride : {8, 16, 32}) {
    bool differs = false;
    for (int x = 0; x + stride < 64 && !differs; ++x) {
      if (field->Value(x, 0) != field->Value(x + stride, 0)) differs = true;
    }
    EXPECT_TRUE(differs) << "noise repeats every " << stride << " pixels";
  }
}

TEST(ValueNoiseFieldTest, RejectsUnusableParameters) {
  EXPECT_FALSE(ValueNoiseField::Create(0, 4, 1, 1).ok());
  EXPECT_FALSE(ValueNoiseField::Create(64, 0, 1, 1).ok());
  EXPECT_FALSE(ValueNoiseField::Create(64, 65, 1, 1).ok());
  EXPECT_FALSE(ValueNoiseField::Create(64, 4, 0, 1).ok());
}

TEST(RuffleFieldTest, RejectsAPeriodThatIsNotAWholeNumberOfTiles) {
  EXPECT_FALSE(RuffleField::Create(48, 32, 2.0f, 1.0f, 1, 1).ok());
  EXPECT_FALSE(RuffleField::Create(0, 32, 2.0f, 1.0f, 1, 1).ok());
  EXPECT_FALSE(RuffleField::Create(64, 32, 2.0f, 1.0f, 0, 1).ok());
  EXPECT_FALSE(RuffleField::Create(64, 32, 0.0f, 1.0f, 1, 1).ok());
  EXPECT_FALSE(RuffleField::Create(64, 32, 2.0f, 0.0f, 1, 1).ok());
}

TEST(PeriodicPatternGridTest, FitsAnIndivisibleFeatureSizeWithoutLosingPeriodicity) {
  // Cozy Meadow's 96px repeat and approximately 5px scallops are deliberately
  // indivisible. The fitted grid still has exactly the requested 96px period.
  const absl::StatusOr<PeriodicPatternGrid> grid =
      PeriodicPatternGrid::Create(/*period_px=*/96, /*cells_per_period=*/19);
  ASSERT_TRUE(grid.ok()) << grid.status();

  for (int x = -96; x < 192; ++x) {
    EXPECT_EQ(grid->Cell(x), grid->Cell(x + 96));
    EXPECT_FLOAT_EQ(grid->Phase(x), grid->Phase(x + 96));
  }
  EXPECT_FALSE(PeriodicPatternGrid::Create(0, 19).ok());
  EXPECT_FALSE(PeriodicPatternGrid::Create(96, 0).ok());
}

TEST(CellularFieldTest, IsDeterministicAndExactlyPeriodic) {
  const absl::StatusOr<CellularField> first = CellularField::Create(96, 16, 123);
  const absl::StatusOr<CellularField> second = CellularField::Create(96, 16, 123);
  ASSERT_TRUE(first.ok() && second.ok());

  for (int y = 0; y < 96; ++y) {
    for (int x = 0; x < 96; ++x) {
      EXPECT_EQ(first->BoundaryDistance(x, y), second->BoundaryDistance(x, y));
      EXPECT_EQ(first->BoundaryDistance(x, y), first->BoundaryDistance(x + 96, y - 96));
    }
  }
  EXPECT_FALSE(CellularField::Create(0, 4, 1).ok());
  EXPECT_FALSE(CellularField::Create(32, 0, 1).ok());
}

}  // namespace
}  // namespace zebes
