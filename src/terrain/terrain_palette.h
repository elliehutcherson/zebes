#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "terrain/terrain_style.h"

namespace zebes {

inline constexpr int kTerrainAccentRampSteps = 8;

struct RgbaColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;

  bool operator==(const RgbaColor& other) const = default;
};

// Stable semantic positions used by terrain's indexed raster and by artwork
// tools selecting meaningful subsets. The values intentionally match the
// private pixel indices terrain emits before its final colourization pass.
enum class TerrainPaletteRole : uint8_t {
  kEmpty = 0,
  kOutline = 1,
  kSurfaceHigh = 2,
  kSurface = 3,
  kSurfaceShade = 4,
  kContact = 5,
  kInterior = 6,
  kInteriorShade = 7,
  kPattern = 8,
  kPatternShade = 9,
  kDecor = 10,
  kDecorShade = 11,
  kSurfaceTextureHigh = 12,
  kSurfaceTextureShade = 13,
  kInteriorHigh = 14,
  kBotanical = 15,
  kBotanicalShade = 16,
  kAccent0 = 17,
  kAccent1 = 18,
  kAccent2 = 19,
  kAccent3 = 20,
  kAccent4 = 21,
  kAccent5 = 22,
  kAccent6 = 23,
  kAccent7 = 24,
  kWall = 25,
  kCount = 26,
};

inline constexpr size_t kTerrainPaletteColorCount = static_cast<size_t>(TerrainPaletteRole::kCount);

struct ResolvedTerrainPalette {
  std::array<RgbaColor, kTerrainPaletteColorCount> colors;

  const RgbaColor& at(TerrainPaletteRole role) const { return colors[static_cast<size_t>(role)]; }

  // Exact duplicate colours are common in compact profiles. Quantizers should
  // not give duplicates accidental extra weight, so this preserves first-role
  // order while returning each opaque colour once.
  std::vector<RgbaColor> OpaqueColors() const;
};

// Builds the exact palette used by TerrainRenderer from an already validated
// configuration and its matching resolved style.
ResolvedTerrainPalette BuildTerrainPalette(const TerrainGenConfig& config,
                                           const ResolvedTerrainStyle& style);

// Validates and resolves a standalone palette for artwork tools.
absl::StatusOr<ResolvedTerrainPalette> ResolveTerrainPalette(const TerrainGenConfig& config);

}  // namespace zebes
