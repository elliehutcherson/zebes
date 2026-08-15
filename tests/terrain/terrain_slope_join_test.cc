#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/tileset.h"
#include "terrain/terrain_generator.h"

namespace zebes {
namespace {

// What the shipped atlas can express about a slope's surroundings, measured
// against what is actually next to it.
//
// The generator bakes artwork before any level exists, so RenderShapeTile
// infers a slope's neighbourhood from the shape's own polygon: a face the
// polygon covers becomes a full square, a face it merely grazes becomes air.
// These tests render the same scene twice -- once the way the atlas draws it,
// once against the neighbours actually present -- and pin which joins that
// inference already gets right.
//
// The point is to stop slope connectivity work from buying artwork for joins
// that are correct today. Every expectation below is a measurement rather than
// a wish: when the generator learns a join, the test saying it cannot is the
// one that has to change.

TerrainGenConfig SceneConfig() {
  TerrainGenConfig config;
  config.tile_size = 32;
  // Enough supersampling for the band to be shaped rather than aliased, and
  // cheap enough that a scene of a dozen cells renders in test time.
  config.supersample = 2;
  // A single phase. With more, every slope would be drawn at phase 0 while the
  // ground beside it took its own, mixing a phase difference into every
  // comparison this file makes.
  config.variant_period = 1;
  config.seed = 20260814;
  return config;
}

// Scene shorthand. '/' rises to the right, '\' rises to the left; 'a' and 'b'
// are the lower and upper halves of a gentle ramp rising to the right.
TileShape ShapeFromChar(char c) {
  switch (c) {
    case '#': return TileShape::kFullBlock;
    case '/': return TileShape::kSlope45BottomLeft;
    case '\\': return TileShape::kSlope45BottomRight;
    case 'a': return TileShape::kGentleSlopeBottomLeft_Lower;
    case 'b': return TileShape::kGentleSlopeBottomLeft_Upper;
    default: return TileShape::kNone;
  }
}

ShapeScene SceneFrom(const std::vector<std::string>& rows) {
  ShapeScene scene;
  scene.height = static_cast<int>(rows.size());
  for (const std::string& row : rows) {
    scene.width = std::max(scene.width, static_cast<int>(row.size()));
  }
  scene.cells.assign(static_cast<size_t>(scene.width) * scene.height, TileShape::kNone);
  for (size_t y = 0; y < rows.size(); ++y) {
    for (size_t x = 0; x < rows[y].size(); ++x) {
      scene.cells[y * scene.width + x] = ShapeFromChar(rows[y][x]);
    }
  }
  return scene;
}

class SlopeJoinTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = TerrainRenderer::Create(SceneConfig());
    ASSERT_OK(renderer_);
  }

  // How many of a cell's pixels the atlas draws differently from the truth.
  //
  // Counted exactly rather than as a percentage: a truncated percent reads
  // "0%" for a handful of differing pixels, which is how a join that is merely
  // very close was once described as identical.
  //
  // A render that fails already fails the test; returning a value no comparison
  // below can accept keeps a broken scene from reading as an agreeing one.
  int DisagreementPixels(const ShapeScene& scene, int x, int y) {
    const absl::StatusOr<RgbaImage> as_atlas =
        RenderSceneCell(*renderer_, scene, x, y, SceneContext::kAsAtlas);
    const absl::StatusOr<RgbaImage> truth =
        RenderSceneCell(*renderer_, scene, x, y, SceneContext::kTrueNeighbors);
    EXPECT_OK(as_atlas);
    EXPECT_OK(truth);
    if (!as_atlas.ok() || !truth.ok()) return -1;

    int different = 0;
    for (size_t i = 0; i + 3 < as_atlas->pixels.size(); i += 4) {
      const bool same = as_atlas->pixels[i + 0] == truth->pixels[i + 0] &&
                        as_atlas->pixels[i + 1] == truth->pixels[i + 1] &&
                        as_atlas->pixels[i + 2] == truth->pixels[i + 2] &&
                        as_atlas->pixels[i + 3] == truth->pixels[i + 3];
      if (!same) ++different;
    }
    return different;
  }

  absl::StatusOr<TerrainRenderer> renderer_ = absl::UnknownError("SetUp did not run");
};

TEST_F(SlopeJoinTest, PaintedGroundNeedsNoContextBeyondItsMask) {
  // A scene with no slope in it must render identically both ways, or every
  // other measurement here is reading the harness rather than the generator.
  const ShapeScene scene = SceneFrom({"....", "####"});

  for (int x = 0; x < scene.width; ++x) {
    EXPECT_EQ(DisagreementPixels(scene, x, 1), 0) << "ground cell " << x;
  }
}

TEST_F(SlopeJoinTest, TwoRampsMeetingAtAPeakAreDrawnAlmostCorrectly) {
  // Both shapes are full height along the face they share, so substituting a
  // square for the neighbour is very nearly exact: the two differ only where
  // the descending ramp falls away deeper into the neighbouring cell, at the
  // very edge of what the distance transform can still see.
  //
  // Two pixels in a thousand, not zero. The distinction matters twice over:
  // measured as a truncated percentage this read "0%" and was written up as
  // identical, and content deduplication is exact, so a near miss still earns
  // its own tile rather than collapsing onto the wall drawing.
  const ShapeScene scene = SceneFrom({"./\\.", "####"});

  EXPECT_GT(DisagreementPixels(scene, 1, 0), 0);
  EXPECT_LT(DisagreementPixels(scene, 1, 0), 16)
      << "a peak is a near miss, not a missing variant";
  EXPECT_LT(DisagreementPixels(scene, 2, 0), 16);
}

TEST_F(SlopeJoinTest, ARampEndingInAirIsDrawnAsThoughItWereBuried) {
  // The join that is genuinely wrong. AutoContext reads the ramp's full-height
  // uphill face as covered and fills the cell beyond it with solid, so the
  // artwork carries interior where the level has open air.
  const ShapeScene into_air = SceneFrom({"../.", "###."});
  const ShapeScene into_ground = SceneFrom({"../#", "####"});

  EXPECT_GT(DisagreementPixels(into_air, 2, 0), 10);
  // The same ramp running uphill into ground is what the inference was written
  // for, and it is exact.
  EXPECT_EQ(DisagreementPixels(into_ground, 2, 0), 0);
}

TEST_F(SlopeJoinTest, AGentleRampsUpperHalfEndingInAirIsAlsoDrawnAsBuried) {
  // ApplyPartner joins the two halves of a ramp to each other. It says nothing
  // about what the pair as a whole runs into, so the same defect reaches the
  // multi-cell families.
  const ShapeScene scene = SceneFrom({".ab..", "####."});

  EXPECT_GT(DisagreementPixels(scene, 2, 0), 10);
}

TEST_F(SlopeJoinTest, ASlopeIsDrawnTheSameWhicheverWayTheGroundPastItsToeRuns) {
  // AutoContext takes a corner only when both flanking edges are solid, so a
  // floor ramp's south-west corner is inferred open even where ground continues
  // below and to the left. That inference turns out never to reach the artwork:
  // both scenes yield the same tile, so the corner rule is not something slope
  // connectivity has to correct.
  const ShapeScene continuing = SceneFrom({"../#", "####"});
  const ShapeScene ending = SceneFrom({"../#", "..##"});

  const absl::StatusOr<RgbaImage> over_continuing =
      RenderSceneCell(*renderer_, continuing, 2, 0, SceneContext::kTrueNeighbors);
  const absl::StatusOr<RgbaImage> over_ending =
      RenderSceneCell(*renderer_, ending, 2, 0, SceneContext::kTrueNeighbors);
  ASSERT_OK(over_continuing);
  ASSERT_OK(over_ending);

  EXPECT_EQ(over_continuing->pixels, over_ending->pixels);
}

TEST_F(SlopeJoinTest, GroundBesideASlopeIsDrawnAgainstASquareThatIsNotThere) {
  // The defect no amount of slope artwork can fix, because the wrong tile is a
  // blob tile. Its mask records only that the neighbour is the same terrain, so
  // the band is measured against a full square when a wedge is what is there.
  // The ground under a ramp's toe and the floor of a valley both come out with
  // a band that stops where it should carry on into the slope.
  const ShapeScene toe = SceneFrom({"../#", "####"});
  const ShapeScene valley = SceneFrom({"#\\./#", "#####"});

  EXPECT_GT(DisagreementPixels(toe, 2, 1), 0);
  EXPECT_GT(DisagreementPixels(valley, 1, 1), 0);
  EXPECT_GT(DisagreementPixels(valley, 3, 1), 0);
}

TEST_F(SlopeJoinTest, AirHasNoArtworkToRender) {
  const ShapeScene scene = SceneFrom({"....", "####"});

  EXPECT_FALSE(RenderSceneCell(*renderer_, scene, 0, 0, SceneContext::kAsAtlas).ok());
}

TEST_F(SlopeJoinTest, ANeighbourhoodIsEightShapes) {
  const std::vector<TileShape> too_few(7, TileShape::kNone);

  EXPECT_FALSE(
      renderer_->RenderShapeTileInContext(TileShape::kFullBlock, too_few, /*variant=*/0).ok());
}

}  // namespace
}  // namespace zebes
