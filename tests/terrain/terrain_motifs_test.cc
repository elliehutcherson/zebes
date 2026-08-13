#include "terrain/terrain_motifs.h"

#include <algorithm>

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(TerrainMotifsTest, EverySupportedProfileAndFamilyHasValidTypedArtwork) {
  for (const TerrainPixelProfile profile :
       {TerrainPixelProfile::kChunky16, TerrainPixelProfile::kBalanced32,
        TerrainPixelProfile::kDetailed64}) {
    for (const TerrainSubstratePattern pattern :
         {TerrainSubstratePattern::kPebbles, TerrainSubstratePattern::kFlecks,
          TerrainSubstratePattern::kCrosses, TerrainSubstratePattern::kDiamonds,
          TerrainSubstratePattern::kMixedEarth}) {
      const absl::Span<const TerrainMotif> motifs = TerrainSubstrateMotifsFor(pattern, profile);
      ASSERT_FALSE(motifs.empty());
      EXPECT_TRUE(ValidateTerrainMotifs(motifs).ok());
    }

    for (const TerrainDetailSet detail : {TerrainDetailSet::kMeadow, TerrainDetailSet::kForestFloor,
                                          TerrainDetailSet::kSnow, TerrainDetailSet::kCrystals}) {
      const absl::Span<const TerrainMotif> motifs = TerrainDetailMotifsFor(detail, profile);
      ASSERT_FALSE(motifs.empty());
      EXPECT_TRUE(ValidateTerrainMotifs(motifs).ok());
    }
  }
}

TEST(TerrainMotifsTest, ChunkySubstrateArtworkUsesSmallerMotifs) {
  for (const TerrainSubstratePattern pattern :
       {TerrainSubstratePattern::kPebbles, TerrainSubstratePattern::kFlecks,
        TerrainSubstratePattern::kCrosses, TerrainSubstratePattern::kDiamonds,
        TerrainSubstratePattern::kMixedEarth}) {
    int compact_edge = 0;
    for (const TerrainMotif& motif :
         TerrainSubstrateMotifsFor(pattern, TerrainPixelProfile::kChunky16)) {
      compact_edge = std::max({compact_edge, motif.width, motif.height});
    }
    int balanced_edge = 0;
    for (const TerrainMotif& motif :
         TerrainSubstrateMotifsFor(pattern, TerrainPixelProfile::kBalanced32)) {
      balanced_edge = std::max({balanced_edge, motif.width, motif.height});
    }
    EXPECT_LE(compact_edge, 3);
    EXPECT_LT(compact_edge, balanced_edge);
  }
}

TEST(TerrainMotifsTest, SubstrateFamiliesOwnDistinctBanks) {
  const absl::Span<const TerrainMotif> pebbles = TerrainSubstrateMotifsFor(
      TerrainSubstratePattern::kPebbles, TerrainPixelProfile::kBalanced32);
  for (const TerrainSubstratePattern pattern :
       {TerrainSubstratePattern::kFlecks, TerrainSubstratePattern::kCrosses,
        TerrainSubstratePattern::kDiamonds, TerrainSubstratePattern::kMixedEarth}) {
    EXPECT_NE(TerrainSubstrateMotifsFor(pattern, TerrainPixelProfile::kBalanced32).data(),
              pebbles.data());
  }
}

TEST(TerrainMotifsTest, CompactFamiliesDoNotSilentlyCollapseToPebbles) {
  const absl::Span<const TerrainMotif> pebbles =
      TerrainSubstrateMotifsFor(TerrainSubstratePattern::kPebbles, TerrainPixelProfile::kChunky16);
  for (const TerrainDetailSet detail : {TerrainDetailSet::kMeadow, TerrainDetailSet::kForestFloor,
                                        TerrainDetailSet::kSnow, TerrainDetailSet::kCrystals}) {
    const absl::Span<const TerrainMotif> motifs =
        TerrainDetailMotifsFor(detail, TerrainPixelProfile::kChunky16);
    EXPECT_NE(motifs.data(), pebbles.data());
  }
}

TEST(TerrainMotifsTest, ValidationRejectsMalformedExternalMotifData) {
  const TerrainMotifPixel invalid_pixels[] = {static_cast<TerrainMotifPixel>(255),
                                              TerrainMotifPixel::kTransparent};
  const TerrainMotif wrong_size{1, 1, invalid_pixels};
  EXPECT_FALSE(ValidateTerrainMotifs(absl::MakeConstSpan(&wrong_size, 1)).ok());

  const TerrainMotif invalid_value{2, 1, invalid_pixels};
  EXPECT_FALSE(ValidateTerrainMotifs(absl::MakeConstSpan(&invalid_value, 1)).ok());
}

}  // namespace
}  // namespace zebes
