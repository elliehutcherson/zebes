#include "terrain/terrain_palette.h"

#include <cstddef>

#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

TEST(TerrainPaletteTest, PreservesAuthoredColoursAtTheirSemanticRoles) {
  const TerrainGenConfig config;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(config));

  EXPECT_EQ(palette.at(TerrainPaletteRole::kEmpty), (RgbaColor{0, 0, 0, 0}));
  EXPECT_EQ(palette.at(TerrainPaletteRole::kOutline), (RgbaColor{0x3b, 0x2b, 0x2a, 255}));
  EXPECT_EQ(palette.at(TerrainPaletteRole::kSurface), (RgbaColor{0x6e, 0xc4, 0x4a, 255}));
  EXPECT_EQ(palette.at(TerrainPaletteRole::kInterior), (RgbaColor{0x8a, 0x5a, 0x3b, 255}));
  EXPECT_EQ(palette.at(TerrainPaletteRole::kAccent0), (RgbaColor{0xf6, 0xd5, 0x6a, 255}));
  EXPECT_EQ(palette.at(TerrainPaletteRole::kAccent7), (RgbaColor{0xf2, 0x8f, 0xa7, 255}));
}

TEST(TerrainPaletteTest, OpaqueColoursAreUniqueAndKeepSemanticOrder) {
  TerrainGenConfig config;
  config.pixel_profile = TerrainPixelProfile::kChunky16;
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(config));

  const std::vector<RgbaColor> colors = palette.OpaqueColors();
  ASSERT_FALSE(colors.empty());
  EXPECT_EQ(colors.front(), palette.at(TerrainPaletteRole::kOutline));
  for (size_t left = 0; left < colors.size(); ++left) {
    EXPECT_EQ(colors[left].a, 255);
    for (size_t right = left + 1; right < colors.size(); ++right) {
      EXPECT_NE(colors[left], colors[right]);
    }
  }
}

TEST(TerrainPaletteTest, RejectsTheSameInvalidConfigAsTerrainStyleResolution) {
  TerrainGenConfig config;
  config.tile_size = 0;
  EXPECT_FALSE(ResolveTerrainPalette(config).ok());
}

}  // namespace
}  // namespace zebes
