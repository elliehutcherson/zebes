#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace zebes {

// Pixel profiles are rasterisation policies, not materials. The same meadow can
// therefore render as deliberately sparse 16px art or as a more textured 32px
// tile without either being a scaled copy of the other.
enum class TerrainPixelProfile : uint8_t {
  kChunky16 = 0,
  kBalanced32 = 1,
  kDetailed64 = 2,
};

enum class TerrainSurfaceStyle : uint8_t {
  kSmooth = 0,
  kTufted = 1,
  kScalloped = 2,
  kMossy = 3,
};

enum class TerrainInteriorStyle : uint8_t {
  kFlat = 0,
  kMottle = 1,
  kSoilClods = 2,
  kCobbles = 3,
};

// Small material marks embedded in the substrate. These are intentionally
// separate from semantic objects such as flowers and crystals: a dirt pattern
// should survive when those objects are turned off.
enum class TerrainSubstratePattern : uint8_t {
  kNone = 0,
  kPebbles = 1,
  kFlecks = 2,
  kCrosses = 3,
  kDiamonds = 4,
  kMixedEarth = 5,
};

enum class TerrainDetailSet : uint8_t {
  kNone = 0,
  kMeadow = 1,
  kForestFloor = 2,
  kSnow = 3,
  kCrystals = 4,
};

struct TerrainInteriorBaseConfig {
  TerrainInteriorStyle style = TerrainInteriorStyle::kMottle;
  // Density is the approximate number of noise cells per tile. Coverage and
  // relief are normalized amounts, portable between pixel profiles.
  float mottle_density = 2.5f;
  float mottle_coverage = 0.30f;
  float feature_size = 6.0f;
  float relief = 0.55f;
};

struct TerrainSubstratePatternConfig {
  TerrainSubstratePattern family = TerrainSubstratePattern::kPebbles;
  // Density is the target number of motifs per tile in the complete periodic
  // field. Spacing and margin use the pixel profile's reference pixels.
  int density = 2;
  int spacing = 13;
  int margin = 2;
  // Zero blends marks into the substrate; one uses the full pattern ramp.
  float contrast = 0.70f;
};

struct TerrainSemanticDetailConfig {
  TerrainDetailSet family = TerrainDetailSet::kNone;
  // Semantic objects have their own placement policy so their abundance can
  // change without disturbing the substrate pattern underneath them.
  int density = 0;
  int spacing = 13;
  int margin = 2;
};

// The three interior concepts have independent switches and amounts. New
// substrate pattern families can be added without expanding the cellular base
// algorithm or pretending that decorative objects are dirt texture.
struct TerrainInteriorConfig {
  TerrainInteriorBaseConfig base;
  TerrainSubstratePatternConfig pattern;
  TerrainSemanticDetailConfig details;
};

// Artistic choices which do not depend on an output resolution. Concrete pixel
// widths and profile-specific motif sprites are selected by ResolveTerrainStyle.
struct TerrainMaterial {
  std::string name = "Classic Grass";
  // Packed 0xRRGGBB. Surface is the band along the outside, substrate the bulk.
  uint32_t surface = 0x6ec44a;
  uint32_t substrate = 0x8a5a3b;
  uint32_t outline = 0x3b2b2a;
  uint32_t accent_primary = 0xf6d56a;
  uint32_t accent_secondary = 0xf28fa7;
  float hue_shift = 0.06f;
  float contrast = 1.0f;
  TerrainSurfaceStyle surface_style = TerrainSurfaceStyle::kTufted;
};

struct TerrainGenConfig {
  // --- Geometry ---
  int tile_size = 32;
  // Pixels are rendered this many times over on each axis and averaged down.
  // A value of 1 is the inexpensive interactive-preview policy.
  int supersample = 4;
  // Edge length of the wrapping art field in tiles. The atlas carries
  // variant_period^2 phases so the level brush can lay the field back down.
  int variant_period = 1;
  TerrainPixelProfile pixel_profile = TerrainPixelProfile::kBalanced32;

  // --- Surface band; measurements use the profile's reference pixels. ---
  float grass_band = 9.0f;
  float ruffle_amplitude = 3.0f;
  float ruffle_density = 2.0f;
  float ruffle_sharpness = 0.65f;
  int ruffle_octaves = 1;
  // One is a uniform band; zero leaves downward-facing overhangs nearly bare.
  float grass_bottom_bias = 0.55f;

  // Layer widths in the selected profile's reference pixels.
  int outline_width = 1;
  int grass_hi_depth = 3;
  int grass_shade_depth = 3;
  int contact_depth = 2;

  float surface_texture_size = 5.0f;
  // Zero disables colour clustering without changing the selected style.
  float surface_texture_amount = 0.45f;

  // --- Interior base, substrate pattern, and semantic details ---
  TerrainInteriorConfig interior;

  uint64_t seed = 1234;
  TerrainMaterial material;
};

// A complete authoring starting point. The editor preserves output quality and
// seed when applying one, but all visual settings come from the preset.
struct TerrainPreset {
  std::string name;
  TerrainGenConfig config;
};

absl::Span<const TerrainPreset> BuiltInTerrainPresets();

// Concrete raster measurements derived and validated once before rendering.
struct ResolvedTerrainStyle {
  int reference_tile_size = 32;
  float scale = 1.0f;
  float grass_band = 0.0f;
  float ruffle_amplitude = 0.0f;
  int outline_width = 0;
  int grass_hi_depth = 0;
  int grass_shade_depth = 0;
  int contact_depth = 0;
  int surface_texture_size = 0;
  int surface_pattern_cells = 0;
  int interior_feature_size = 0;
  int interior_cells = 0;
  int pattern_spacing = 0;
  int pattern_margin = 0;
  int detail_spacing = 0;
  int detail_margin = 0;
  bool compact_palette = false;
};

absl::StatusOr<ResolvedTerrainStyle> ResolveTerrainStyle(const TerrainGenConfig& config);

}  // namespace zebes
