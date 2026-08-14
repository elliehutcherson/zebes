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

// Small forms which hang from the inner edge of the surface band. They are
// painted inside solid terrain, never outside the collision silhouette.
enum class TerrainEdgeDetailSet : uint8_t {
  kNone = 0,
  kShortGrass = 1,
  kDryGrass = 2,
  kMossFringe = 3,
  kSnowLip = 4,
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

// How a motif layer colours the pixels a motif leaves to the renderer. Material
// keeps the layer's own ramp, so marks read as the substrate they sit in. The
// accent modes hand the layer over to the material's accent pair instead, which
// is what lets a crystal read as a gem rather than as tinted dirt.
//
// Gradient sweeps the pair across each motif rather than across the terrain, so
// the effect survives a motif being placed anywhere and does not depend on the
// tile a mark happens to land in.
enum class TerrainAccentMode : uint8_t {
  kMaterial = 0,
  kAccent = 1,
  kGradient = 2,
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
  // Zero blends marks into the substrate; one uses the full pattern ramp. Has
  // no effect unless accent_mode is kMaterial, since the accent modes bypass
  // the pattern ramp entirely.
  float contrast = 0.70f;
  // Integer magnification of the motif stamp. The banks are drawn once at each
  // profile's reference size, so magnifying them keeps the pixel art crisp
  // where redrawing every bank at every size would not.
  int scale = 1;
  TerrainAccentMode accent_mode = TerrainAccentMode::kMaterial;
};

struct TerrainSemanticDetailConfig {
  TerrainDetailSet family = TerrainDetailSet::kNone;
  // Semantic objects have their own placement policy so their abundance can
  // change without disturbing the substrate pattern underneath them.
  int density = 0;
  int spacing = 13;
  int margin = 2;
  // See TerrainSubstratePatternConfig for both of these.
  int scale = 1;
  TerrainAccentMode accent_mode = TerrainAccentMode::kMaterial;
};

// The three interior concepts have independent switches and amounts. New
// substrate pattern families can be added without expanding the cellular base
// algorithm or pretending that decorative objects are dirt texture.
struct TerrainInteriorConfig {
  TerrainInteriorBaseConfig base;
  TerrainSubstratePatternConfig pattern;
  TerrainSemanticDetailConfig details;
};

struct TerrainEdgeDetailConfig {
  TerrainEdgeDetailSet family = TerrainEdgeDetailSet::kNone;
  // Amount is the fraction of atlas-global clump cells which are occupied.
  float amount = 0.65f;
  // Length and clump size use the selected pixel profile's reference pixels.
  int length = 4;
  int clump_size = 5;
  // Signed tangent offset per unit of depth. Negative and positive values lean
  // in opposite directions without changing where the terrain collides.
  float lean = 0.0f;
  // Probability that the lit root of a motif receives the surface highlight.
  float highlight = 0.35f;
};

// Resolution-independent description of the material around an exposed edge.
//
// Top, side, and underside depths are authored independently and blended from
// the distance-field normal. This is deliberately material-agnostic: grass,
// snow, moss, and sand can all share the same facing calculation while choosing
// very different coverage. Wall shading begins after the contact shadow and is
// restricted to side- and downward-facing edges.
struct TerrainSurfaceConfig {
  float top_depth = 9.0f;
  float side_depth = 7.0f;
  float underside_depth = 5.0f;

  float ruffle_amplitude = 3.0f;
  float ruffle_density = 2.0f;
  float ruffle_sharpness = 0.65f;
  int ruffle_octaves = 1;

  int outline_depth = 1;
  int highlight_depth = 3;
  int shade_depth = 3;
  int contact_depth = 2;
  int wall_depth = 0;
  // Strength of the blend from substrate toward the authored outline colour.
  // Zero matches the interior; larger values approach, but never overshoot,
  // the outline. This keeps very dark materials from clipping to black.
  float wall_darkness = 1.0f;

  float texture_size = 5.0f;
  // Zero disables colour clustering without changing the selected style.
  float texture_amount = 0.45f;

  // A separate semantic layer so adding blades or a snow lip does not distort
  // the surface-band field. This boundary is what lets future motif families
  // grow independently of the geometry algorithm.
  TerrainEdgeDetailConfig edge_detail;
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

  // --- Facing-aware surface and wall treatment ---
  TerrainSurfaceConfig surface;

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

// The accent mode a detail set is normally authored with. Snow and crystals are
// the sets whose colour is the whole point of choosing them, so selecting one
// and getting substrate-tinted marks would be a surprise. This is a starting
// point the author can override, not a constraint the renderer enforces.
TerrainAccentMode DefaultAccentModeFor(TerrainDetailSet details);

absl::Span<const TerrainPreset> BuiltInTerrainPresets();

// Concrete raster measurements derived and validated once before rendering.
struct ResolvedTerrainStyle {
  int reference_tile_size = 32;
  float scale = 1.0f;
  float surface_top_depth = 0.0f;
  float surface_side_depth = 0.0f;
  float surface_underside_depth = 0.0f;
  float ruffle_amplitude = 0.0f;
  int outline_depth = 0;
  int highlight_depth = 0;
  int shade_depth = 0;
  int contact_depth = 0;
  int wall_depth = 0;
  int surface_texture_size = 0;
  int surface_pattern_cells = 0;
  int edge_detail_length = 0;
  int edge_pattern_cells = 0;
  int interior_feature_size = 0;
  int interior_cells = 0;
  int pattern_spacing = 0;
  int pattern_margin = 0;
  int pattern_scale = 1;
  int detail_spacing = 0;
  int detail_margin = 0;
  int detail_scale = 1;
  bool compact_palette = false;
};

absl::StatusOr<ResolvedTerrainStyle> ResolveTerrainStyle(const TerrainGenConfig& config);

}  // namespace zebes
