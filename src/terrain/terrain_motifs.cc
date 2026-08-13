#include "terrain/terrain_motifs.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

using P = TerrainMotifPixel;
constexpr P T = P::kTransparent;
constexpr P A = P::kAutoShaded;
constexpr P D = P::kDecor;
constexpr P S = P::kDecorShade;
constexpr P B = P::kBotanical;
constexpr P G = P::kBotanicalShade;
constexpr P X = P::kAccentPrimary;
constexpr P Y = P::kAccentSecondary;

constexpr P kDotPixels[] = {T, A, T, A, A, A, T, A, T};
constexpr P kPebblePixels[] = {T, A, A, T, A, A, A, A, T, A, A, T};
constexpr P kFleckLinePixels[] = {A, S, A};
constexpr P kFleckCornerPixels[] = {A, T, S, A};
constexpr P kCrossPixels[] = {T, T, A, T, T, T, T, A, T, T, A, A, S,
                              A, A, T, T, A, T, T, T, T, A, T, T};
constexpr P kXMarkPixels[] = {A, T, A, T, S, T, A, T, A};
constexpr P kDiamondPixels[] = {T, T, A, T, T, T, A, A, A, T, A, A, A,
                                A, A, T, A, A, A, T, T, T, A, T, T};
constexpr P kFlakePixels[] = {A, T, T, A, T, T, A, T, A, T, A, T, A, T, T, T, A,
                              A, A, T, T, A, A, A, T, A, A, A, T, T, A, A, A, T,
                              T, T, A, T, A, T, A, T, A, T, T, A, T, T, A};
constexpr P kStarPixels[] = {T, T, A, T, T, T, T, A, T, T, A, A, A,
                             A, A, T, T, A, T, T, T, T, A, T, T};
constexpr P kSproutPixels[] = {T, B, T, T, B, B, B, T, T, G, T, T, G, T, T};
constexpr P kFlowerPixels[] = {T, X, T, Y, X, Y, T, X, T, T, B, T, T, G, T};
constexpr P kRootPixels[] = {D, T, D, T, D, D, D, T, D, T, D, T, S, T, S};

constexpr P kCompactDotPixels[] = {A};
constexpr P kCompactFleckPixels[] = {A, S};
constexpr P kCompactPebblePixels[] = {A, A, A, A};
constexpr P kCompactDiamondPixels[] = {T, A, T, A, S, A, T, A, T};
constexpr P kCompactSproutPixels[] = {T, B, T, B, G, B};
constexpr P kCompactFlowerPixels[] = {X, Y, X, B};
constexpr P kCompactRootPixels[] = {D, T, D, D, S, T};
constexpr P kCompactFlakePixels[] = {T, X, T, X, Y, X, T, X, T};
constexpr P kCompactCrystalPixels[] = {T, X, T, X, Y, X, T, X, T};

constexpr TerrainMotif kPebbleSet[] = {
    {3, 3, kDotPixels}, {4, 3, kPebblePixels}, {5, 5, kDiamondPixels}};
constexpr TerrainMotif kFleckSet[] = {
    {1, 1, kCompactDotPixels}, {3, 1, kFleckLinePixels}, {2, 2, kFleckCornerPixels}};
constexpr TerrainMotif kCrossSet[] = {{3, 3, kXMarkPixels}, {5, 5, kCrossPixels}};
constexpr TerrainMotif kDiamondSet[] = {{3, 3, kDotPixels}, {5, 5, kDiamondPixels}};
// Small marks intentionally occupy half the reference-like mixed bank, leaving
// its large crosses and diamonds as occasional accents.
constexpr TerrainMotif kMixedEarthSet[] = {{1, 1, kCompactDotPixels},  {3, 1, kFleckLinePixels},
                                           {2, 2, kFleckCornerPixels}, {3, 3, kXMarkPixels},
                                           {5, 5, kCrossPixels},       {5, 5, kDiamondPixels}};
constexpr TerrainMotif kMeadowSet[] = {
    {3, 5, kSproutPixels}, {3, 5, kFlowerPixels}, {3, 5, kFlowerPixels}};
constexpr TerrainMotif kForestSet[] = {{3, 5, kSproutPixels}, {3, 5, kRootPixels}};
constexpr TerrainMotif kSnowSet[] = {{7, 7, kFlakePixels}, {3, 3, kDotPixels}, {5, 5, kStarPixels}};
constexpr TerrainMotif kCrystalSet[] = {
    {5, 5, kStarPixels}, {3, 3, kDotPixels}, {5, 5, kDiamondPixels}};

constexpr TerrainMotif kCompactPebbleSet[] = {{1, 1, kCompactDotPixels},
                                              {2, 2, kCompactPebblePixels}};
constexpr TerrainMotif kCompactFleckSet[] = {{1, 1, kCompactDotPixels},
                                             {2, 1, kCompactFleckPixels}};
constexpr TerrainMotif kCompactCrossSet[] = {{3, 3, kXMarkPixels}};
constexpr TerrainMotif kCompactDiamondSet[] = {{3, 3, kCompactDiamondPixels}};
constexpr TerrainMotif kCompactMixedEarthSet[] = {{1, 1, kCompactDotPixels},
                                                  {2, 1, kCompactFleckPixels},
                                                  {3, 3, kXMarkPixels},
                                                  {3, 3, kCompactDiamondPixels}};
constexpr TerrainMotif kCompactMeadowSet[] = {{3, 2, kCompactSproutPixels},
                                              {2, 2, kCompactFlowerPixels}};
constexpr TerrainMotif kCompactForestSet[] = {{3, 2, kCompactSproutPixels},
                                              {3, 2, kCompactRootPixels}};
constexpr TerrainMotif kCompactSnowSet[] = {{3, 3, kCompactFlakePixels}, {1, 1, kCompactDotPixels}};
constexpr TerrainMotif kCompactCrystalSet[] = {{3, 3, kCompactCrystalPixels},
                                               {2, 2, kCompactPebblePixels}};

}  // namespace

absl::Span<const TerrainMotif> TerrainSubstrateMotifsFor(TerrainSubstratePattern pattern,
                                                         TerrainPixelProfile profile) {
  switch (pattern) {
    case TerrainSubstratePattern::kNone:
      return {};
    case TerrainSubstratePattern::kPebbles:
      return profile == TerrainPixelProfile::kChunky16 ? absl::MakeConstSpan(kCompactPebbleSet)
                                                       : absl::MakeConstSpan(kPebbleSet);
    case TerrainSubstratePattern::kFlecks:
      return profile == TerrainPixelProfile::kChunky16 ? absl::MakeConstSpan(kCompactFleckSet)
                                                       : absl::MakeConstSpan(kFleckSet);
    case TerrainSubstratePattern::kCrosses:
      return profile == TerrainPixelProfile::kChunky16 ? absl::MakeConstSpan(kCompactCrossSet)
                                                       : absl::MakeConstSpan(kCrossSet);
    case TerrainSubstratePattern::kDiamonds:
      return profile == TerrainPixelProfile::kChunky16 ? absl::MakeConstSpan(kCompactDiamondSet)
                                                       : absl::MakeConstSpan(kDiamondSet);
    case TerrainSubstratePattern::kMixedEarth:
      return profile == TerrainPixelProfile::kChunky16 ? absl::MakeConstSpan(kCompactMixedEarthSet)
                                                       : absl::MakeConstSpan(kMixedEarthSet);
  }
  return {};
}

absl::Span<const TerrainMotif> TerrainDetailMotifsFor(TerrainDetailSet detail_set,
                                                      TerrainPixelProfile profile) {
  if (detail_set == TerrainDetailSet::kNone) return {};
  const bool compact = profile == TerrainPixelProfile::kChunky16;
  if (compact) {
    switch (detail_set) {
      case TerrainDetailSet::kMeadow:
        return kCompactMeadowSet;
      case TerrainDetailSet::kForestFloor:
        return kCompactForestSet;
      case TerrainDetailSet::kSnow:
        return kCompactSnowSet;
      case TerrainDetailSet::kCrystals:
        return kCompactCrystalSet;
      case TerrainDetailSet::kNone:
        return {};
    }
  }

  switch (detail_set) {
    case TerrainDetailSet::kMeadow:
      return kMeadowSet;
    case TerrainDetailSet::kForestFloor:
      return kForestSet;
    case TerrainDetailSet::kSnow:
      return kSnowSet;
    case TerrainDetailSet::kCrystals:
      return kCrystalSet;
    case TerrainDetailSet::kNone:
      return {};
  }
  return {};
}

absl::Status ValidateTerrainMotifs(absl::Span<const TerrainMotif> motifs) {
  for (size_t i = 0; i < motifs.size(); ++i) {
    const TerrainMotif& motif = motifs[i];
    if (motif.width <= 0 || motif.height <= 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("terrain motif ", i, " has non-positive dimensions"));
    }
    const size_t expected = static_cast<size_t>(motif.width) * motif.height;
    if (motif.pixels.size() != expected) {
      return absl::InvalidArgumentError(absl::StrCat(
          "terrain motif ", i, " holds ", motif.pixels.size(), " pixels; expected ", expected));
    }
    for (const TerrainMotifPixel pixel : motif.pixels) {
      switch (pixel) {
        case TerrainMotifPixel::kTransparent:
        case TerrainMotifPixel::kAutoShaded:
        case TerrainMotifPixel::kDecor:
        case TerrainMotifPixel::kDecorShade:
        case TerrainMotifPixel::kBotanical:
        case TerrainMotifPixel::kBotanicalShade:
        case TerrainMotifPixel::kAccentPrimary:
        case TerrainMotifPixel::kAccentSecondary:
          continue;
      }
      return absl::InvalidArgumentError(
          absl::StrCat("terrain motif ", i, " contains an unknown semantic pixel"));
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes
