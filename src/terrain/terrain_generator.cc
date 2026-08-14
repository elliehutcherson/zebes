#include "terrain/terrain_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "terrain/terrain_mask.h"
#include "terrain/terrain_motifs.h"

namespace zebes {
namespace {

// How many palette entries the accent gradient spans. Eight is enough for a
// sweep to read as continuous at these tile sizes without the palette growing
// past what a pixel-art tileset should hold.
constexpr int kAccentRampSteps = 8;
// A compact palette carries the same ramp quantised to this many colours, so
// chunky artwork gets genuinely fewer tones rather than eight near-identical
// ones.
constexpr int kCompactAccentRampSteps = 4;

// Semantic pixel indices. Everything stays in this space until the very last
// step, which keeps palette changes independent from the geometry passes.
enum PixelIndex : uint8_t {
  kIndexEmpty = 0,
  kIndexOutline = 1,
  kIndexSurfaceHigh = 2,
  kIndexSurface = 3,
  kIndexSurfaceShade = 4,
  kIndexContact = 5,
  kIndexInterior = 6,
  kIndexInteriorShade = 7,
  kIndexPattern = 8,
  kIndexPatternShade = 9,
  kIndexDecor = 10,
  kIndexDecorShade = 11,
  kIndexSurfaceTextureHigh = 12,
  kIndexSurfaceTextureShade = 13,
  kIndexInteriorHigh = 14,
  kIndexBotanical = 15,
  kIndexBotanicalShade = 16,
  // The accent pair is a ramp rather than two entries so a motif can be swept
  // between them. Its length is fixed even for compact palettes, which keeps
  // both endpoints constant expressions: flat accent shading stays a two-entry
  // lookup, and no pass has to know how many steps are in use. A compact
  // palette quantises the ramp instead of shortening it.
  kIndexAccentRamp = 17,
  kIndexAccentPrimary = kIndexAccentRamp,
  kIndexAccentSecondary = kIndexAccentRamp + kAccentRampSteps - 1,
  // Kept after the accent ramp so adding wall treatment cannot renumber any
  // existing semantic pixel. That makes legacy-output comparisons easier to
  // reason about even though indices never leave the renderer.
  kIndexWall = kIndexAccentRamp + kAccentRampSteps,
  kIndexCount = kIndexWall + 1,
};

// Where the light comes from, as a pixel offset. Only decoration shading uses
// it; everything else takes its shading from surface orientation.
constexpr int kLightDx = -1;
constexpr int kLightDy = -1;

uint64_t MixBits(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

float HashUnit(uint64_t value) {
  return static_cast<float>(MixBits(value) >> 40) / static_cast<float>(1ULL << 24);
}

// Neighbour slots, in the same order as the Neighbor bits in terrain_mask.h so
// a blob mask can be walked bit by bit.
constexpr int kNeighborCount = 8;
constexpr int kNeighborCellX[kNeighborCount] = {1, 2, 2, 2, 1, 0, 0, 0};
constexpr int kNeighborCellY[kNeighborCount] = {0, 0, 1, 2, 2, 2, 1, 0};

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;
};

struct Hsv {
  float h = 0.0f;
  float s = 0.0f;
  float v = 0.0f;
};

Hsv ToHsv(uint32_t rgb) {
  const float r = static_cast<float>((rgb >> 16) & 0xff) / 255.0f;
  const float g = static_cast<float>((rgb >> 8) & 0xff) / 255.0f;
  const float b = static_cast<float>(rgb & 0xff) / 255.0f;
  const float high = std::max({r, g, b});
  const float low = std::min({r, g, b});
  const float span = high - low;

  Hsv hsv;
  hsv.v = high;
  hsv.s = high <= 0.0f ? 0.0f : span / high;
  if (span <= 0.0f) return hsv;

  if (high == r) {
    hsv.h = (g - b) / span / 6.0f;
  } else if (high == g) {
    hsv.h = (2.0f + (b - r) / span) / 6.0f;
  } else {
    hsv.h = (4.0f + (r - g) / span) / 6.0f;
  }
  hsv.h = hsv.h - std::floor(hsv.h);
  return hsv;
}

Rgba ToRgba(const Hsv& hsv) {
  const float h = (hsv.h - std::floor(hsv.h)) * 6.0f;
  const int sector = static_cast<int>(h) % 6;
  const float fraction = h - std::floor(h);
  const float p = hsv.v * (1.0f - hsv.s);
  const float q = hsv.v * (1.0f - hsv.s * fraction);
  const float t = hsv.v * (1.0f - hsv.s * (1.0f - fraction));

  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  switch (sector) {
    case 0:
      r = hsv.v;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = hsv.v;
      b = p;
      break;
    case 2:
      r = p;
      g = hsv.v;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = hsv.v;
      break;
    case 4:
      r = t;
      g = p;
      b = hsv.v;
      break;
    default:
      r = hsv.v;
      g = p;
      b = q;
      break;
  }
  return Rgba{static_cast<uint8_t>(std::lround(r * 255.0f)),
              static_cast<uint8_t>(std::lround(g * 255.0f)),
              static_cast<uint8_t>(std::lround(b * 255.0f)), 255};
}

// Blends two colours in HSV, taking the shorter way around the hue circle.
//
// The hue arc is the whole reason this is not a component-wise RGB mix. Between
// a warm gold and a cool blue, RGB passes through desaturated grey and the
// sweep reads as dirt; rotating the hue instead keeps every intermediate step
// as saturated as its endpoints, which is what makes the result read as
// iridescent rather than as two colours fading into each other.
Hsv MixHsv(const Hsv& from, const Hsv& to, float t) {
  float arc = to.h - from.h;
  arc -= std::floor(arc);
  if (arc > 0.5f) arc -= 1.0f;

  Hsv mixed;
  mixed.h = from.h + arc * t;
  mixed.h -= std::floor(mixed.h);
  mixed.s = from.s + (to.s - from.s) * t;
  mixed.v = from.v + (to.v - from.v) * t;
  return mixed;
}

// One step along a theme ramp. Negative steps are lighter and warmer, positive
// darker and cooler; the hue drift is the point, not a side effect.
Rgba Ramp(uint32_t base, float step, float hue_shift, float saturation_shift = 0.10f) {
  Hsv hsv = ToHsv(base);
  hsv.h = hsv.h + hue_shift * step;
  hsv.s = std::clamp(hsv.s + saturation_shift * step, 0.0f, 1.0f);
  hsv.v = std::clamp(hsv.v - 0.13f * step, 0.0f, 1.0f);
  return ToRgba(hsv);
}

std::array<Rgba, kIndexCount> BuildPalette(const TerrainMaterial& material, float pattern_contrast,
                                           float wall_darkness, bool compact_palette) {
  std::array<Rgba, kIndexCount> palette{};
  palette[kIndexEmpty] = Rgba{0, 0, 0, 0};
  const float contrast = material.contrast;
  palette[kIndexSurfaceHigh] = Ramp(material.surface, -1.0f * contrast, material.hue_shift);
  palette[kIndexSurface] = Ramp(material.surface, 0.0f, material.hue_shift);
  palette[kIndexSurfaceShade] = Ramp(material.surface, 1.2f * contrast, material.hue_shift);
  palette[kIndexContact] = Ramp(material.substrate, 1.9f * contrast, material.hue_shift);
  palette[kIndexInterior] = Ramp(material.substrate, 0.0f, material.hue_shift);
  // The interior mottle is a texture, not a feature: it wants to read as the
  // same material catching the light differently. A full ramp step here drags
  // the hue far enough to look like a second material growing in patches, so it
  // takes a short step with most of the hue drift held back.
  palette[kIndexInteriorShade] = Ramp(
      material.substrate, (compact_palette ? 0.9f : 0.5f) * contrast, material.hue_shift * 0.3f);
  palette[kIndexInteriorHigh] =
      Ramp(material.substrate, (compact_palette ? -0.65f : -0.35f) * contrast,
           material.hue_shift * 0.2f);
  palette[kIndexOutline] = ToRgba(ToHsv(material.outline));
  // Substrate marks own a short ramp. They can be made quiet without also
  // flattening roots, flowers, or the base material's cellular relief.
  palette[kIndexPattern] =
      Ramp(material.substrate, -0.85f * contrast * pattern_contrast, material.hue_shift * 0.3f);
  palette[kIndexPatternShade] =
      Ramp(material.substrate, 0.75f * contrast * pattern_contrast, material.hue_shift * 0.3f);
  // Neutral semantic decorations are derived from the substrate, not the
  // surface. Surface-tinted roots read as stray noise rather than objects in
  // the dirt.
  palette[kIndexDecor] = Ramp(material.substrate, -0.85f * contrast, material.hue_shift);
  palette[kIndexDecorShade] = Ramp(material.substrate, 0.75f * contrast, material.hue_shift);
  palette[kIndexSurfaceTextureHigh] =
      compact_palette ? palette[kIndexSurfaceHigh]
                      : Ramp(material.surface, -0.55f * contrast, material.hue_shift);
  palette[kIndexSurfaceTextureShade] =
      compact_palette ? palette[kIndexSurfaceShade]
                      : Ramp(material.surface, 0.65f * contrast, material.hue_shift);
  palette[kIndexBotanical] = Ramp(material.surface, -0.25f * contrast, material.hue_shift);
  palette[kIndexBotanicalShade] = Ramp(material.surface, 0.9f * contrast, material.hue_shift);
  // The endpoints land on the authored colours exactly, so flat accent shading
  // is unchanged by the ramp existing.
  const Hsv accent_from = ToHsv(material.accent_primary);
  const Hsv accent_to = ToHsv(material.accent_secondary);
  const int distinct = compact_palette ? kCompactAccentRampSteps : kAccentRampSteps;
  for (int step = 0; step < kAccentRampSteps; ++step) {
    // Quantising the position before mixing, rather than the colour after,
    // keeps the two endpoints exact at every quantisation.
    const int bucket = step * (distinct - 1) / (kAccentRampSteps - 1);
    const float t = static_cast<float>(bucket) / static_cast<float>(distinct - 1);
    palette[kIndexAccentRamp + step] = ToRgba(MixHsv(accent_from, accent_to, t));
  }
  // Wall darkness is a bounded material transition, not another open-ended
  // ramp step. Subtractive shading clipped dark substrates to RGB black long
  // before the slider reached its maximum (Autumn Forest clipped around 1.2),
  // erasing both hue and authored outline warmth. An exponential approach
  // keeps the existing 0..4 strength range useful while guaranteeing that the
  // wall stays between the substrate and the explicitly authored outline.
  const float wall_mix = 1.0f - std::exp(-wall_darkness);
  palette[kIndexWall] =
      ToRgba(MixHsv(ToHsv(material.substrate), ToHsv(material.outline), wall_mix));
  return palette;
}

// The lowest and highest position along the light diagonal that a motif
// actually paints, in source pixels.
//
// The sweep is normalised over this rather than over the stamp's bounding box
// because the corners of a diamond are transparent: measured against the box, a
// diamond's sweep would stop three quarters of the way along and the second
// accent colour would never appear on it. Derived from the stamp alone, so
// every wrapped copy of a motif agrees about its gradient across a tile seam.
std::pair<int, int> MotifGradientSpan(const TerrainMotif& stamp) {
  int lowest = stamp.width + stamp.height;
  int highest = 0;
  for (int y = 0; y < stamp.height; ++y) {
    for (int x = 0; x < stamp.width; ++x) {
      if (stamp.pixels[static_cast<size_t>(y) * stamp.width + x] ==
          TerrainMotifPixel::kTransparent) {
        continue;
      }
      lowest = std::min(lowest, x + y);
      highest = std::max(highest, x + y);
    }
  }
  return {lowest, highest};
}

// The three indices a motif may be drawn over. Anything else is surface, band
// or outline, which motifs must never touch.
bool IsInteriorIndex(uint8_t value) {
  return value == kIndexInterior || value == kIndexInteriorShade || value == kIndexInteriorHigh;
}

// How one motif layer paints the pixels its motifs leave to the renderer.
struct MotifPaint {
  TerrainAccentMode accent_mode = TerrainAccentMode::kMaterial;
  bool substrate_layer = false;
};

// gradient_t runs from 0 at the lit corner of a motif to 1 at its shadowed one,
// so the two accent modes agree about which end is which.
uint8_t MotifPixelIndex(TerrainMotifPixel pixel, bool lit, float gradient_t,
                        const MotifPaint& paint) {
  switch (pixel) {
    case TerrainMotifPixel::kTransparent:
      return kIndexEmpty;
    case TerrainMotifPixel::kAutoShaded:
      if (paint.accent_mode == TerrainAccentMode::kGradient) {
        const int step = static_cast<int>(gradient_t * kAccentRampSteps);
        return kIndexAccentRamp + std::clamp(step, 0, kAccentRampSteps - 1);
      }
      if (paint.accent_mode == TerrainAccentMode::kAccent) {
        return lit ? kIndexAccentPrimary : kIndexAccentSecondary;
      }
      if (paint.substrate_layer) return lit ? kIndexPattern : kIndexPatternShade;
      return lit ? kIndexDecor : kIndexDecorShade;
    case TerrainMotifPixel::kDecor:
      return paint.substrate_layer ? kIndexPattern : kIndexDecor;
    case TerrainMotifPixel::kDecorShade:
      return paint.substrate_layer ? kIndexPatternShade : kIndexDecorShade;
    case TerrainMotifPixel::kBotanical:
      return kIndexBotanical;
    case TerrainMotifPixel::kBotanicalShade:
      return kIndexBotanicalShade;
    // A motif that names an accent explicitly is an author's choice about that
    // mark, so it keeps the endpoint colour in every mode.
    case TerrainMotifPixel::kAccentPrimary:
      return kIndexAccentPrimary;
    case TerrainMotifPixel::kAccentSecondary:
      return kIndexAccentSecondary;
  }
  return kIndexEmpty;
}

// --------------------------------------------------------------------------
// polygon rasterisation

// Scanline-fills a unit-square polygon into a boolean canvas. (cell_x, cell_y)
// places it on the 3x3 grid of tiles; resolution is supersampled pixels per
// tile.
void RasterizePolygon(absl::Span<const TilePoint> polygon, int resolution, int cell_x, int cell_y,
                      int canvas, std::vector<uint8_t>& out) {
  if (polygon.empty()) return;

  std::vector<float> xs;
  std::vector<float> ys;
  xs.reserve(polygon.size());
  ys.reserve(polygon.size());
  for (const TilePoint& point : polygon) {
    xs.push_back((point.x + static_cast<float>(cell_x)) * static_cast<float>(resolution));
    ys.push_back((point.y + static_cast<float>(cell_y)) * static_cast<float>(resolution));
  }

  const int first_row =
      std::max(0, static_cast<int>(std::floor(*std::min_element(ys.begin(), ys.end()))));
  const int last_row =
      std::min(canvas, static_cast<int>(std::ceil(*std::max_element(ys.begin(), ys.end()))));

  std::vector<float> crossings;
  for (int row = first_row; row < last_row; ++row) {
    const float center = static_cast<float>(row) + 0.5f;
    crossings.clear();
    for (size_t i = 0; i < polygon.size(); ++i) {
      const size_t j = (i + 1) % polygon.size();
      const bool spans = (ys[i] <= center && center < ys[j]) || (ys[j] <= center && center < ys[i]);
      if (!spans) continue;
      const float t = (center - ys[i]) / (ys[j] - ys[i]);
      crossings.push_back(xs[i] + t * (xs[j] - xs[i]));
    }

    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      const int from = std::max(0, static_cast<int>(std::ceil(crossings[i] - 0.5f)));
      const int to = std::min(canvas, static_cast<int>(std::ceil(crossings[i + 1] - 0.5f)));
      for (int x = from; x < to; ++x) out[static_cast<size_t>(row) * canvas + x] = 1;
    }
  }
}

// How much of one tile face a polygon covers, in [0,1].
float EdgeCoverage(absl::Span<const TilePoint> polygon, int neighbor) {
  float covered = 0.0f;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const TilePoint& p = polygon[i];
    const TilePoint& q = polygon[(i + 1) % polygon.size()];
    switch (neighbor) {
      case 0:  // North
        if (p.y == 0.0f && q.y == 0.0f) covered += std::abs(p.x - q.x);
        break;
      case 2:  // East
        if (p.x == 1.0f && q.x == 1.0f) covered += std::abs(p.y - q.y);
        break;
      case 4:  // South
        if (p.y == 1.0f && q.y == 1.0f) covered += std::abs(p.x - q.x);
        break;
      case 6:  // West
        if (p.x == 0.0f && q.x == 0.0f) covered += std::abs(p.y - q.y);
        break;
      default:
        break;
    }
  }
  return covered;
}

// Infers which neighbours are solid from how fully the polygon covers each
// face. A face it covers is a face the terrain continues through; one it merely
// grazes is open to air. This single rule gives every slope in TileShape a
// correct band with no hand-written table.
void AutoContext(absl::Span<const TilePoint> polygon,
                 std::vector<absl::Span<const TilePoint>>& neighbors) {
  const absl::Span<const TilePoint> square = TileShapePolygon(TileShape::kFullBlock);
  for (const int edge : {0, 2, 4, 6}) {
    neighbors[edge] = EdgeCoverage(polygon, edge) >= 0.5f ? square : absl::Span<const TilePoint>();
  }
  // A corner is only solid when both of its flanking edges are, which is the
  // same rule the brush normalizes masks with.
  for (const int corner : {1, 3, 5, 7}) {
    const int before = (corner + 7) % 8;
    const int after = (corner + 1) % 8;
    const bool solid = !neighbors[before].empty() && !neighbors[after].empty();
    neighbors[corner] = solid ? square : absl::Span<const TilePoint>();
  }
}

// The two-cell slope families, as (left or top cell, right or bottom cell).
//
// Which half sits on which side follows from the geometry in
// tile_shape_geometry.h: a gentle ramp's Lower half is the end that starts at
// zero height, so it leads when the ramp rises to the right and trails when it
// rises to the left. Steep units always stack Bottom below Top.
struct SlopePair {
  TileShape first = TileShape::kNone;
  TileShape second = TileShape::kNone;
  // True when the halves sit side by side, false when they stack.
  bool horizontal = true;
};

constexpr SlopePair kSlopePairs[] = {
    {TileShape::kGentleSlopeBottomLeft_Lower, TileShape::kGentleSlopeBottomLeft_Upper, true},
    {TileShape::kGentleSlopeBottomRight_Upper, TileShape::kGentleSlopeBottomRight_Lower, true},
    {TileShape::kGentleSlopeTopLeft_Lower, TileShape::kGentleSlopeTopLeft_Upper, true},
    {TileShape::kGentleSlopeTopRight_Upper, TileShape::kGentleSlopeTopRight_Lower, true},
    {TileShape::kSteepSlopeBottomLeft_Top, TileShape::kSteepSlopeBottomLeft_Bottom, false},
    {TileShape::kSteepSlopeBottomRight_Top, TileShape::kSteepSlopeBottomRight_Bottom, false},
    {TileShape::kSteepSlopeTopLeft_Top, TileShape::kSteepSlopeTopLeft_Bottom, false},
    {TileShape::kSteepSlopeTopRight_Top, TileShape::kSteepSlopeTopRight_Bottom, false},
};

// Replaces the inferred neighbour with the partner's actual polygon, so the
// band is continuous across the seam inside a two-cell ramp rather than
// stopping at it.
void ApplyPartner(TileShape shape, std::vector<absl::Span<const TilePoint>>& neighbors) {
  for (const SlopePair& pair : kSlopePairs) {
    if (pair.first == shape) {
      neighbors[pair.horizontal ? 2 : 4] = TileShapePolygon(pair.second);
      return;
    }
    if (pair.second == shape) {
      neighbors[pair.horizontal ? 6 : 0] = TileShapePolygon(pair.first);
      return;
    }
  }
}

}  // namespace

TerrainRenderer::TerrainRenderer(TerrainGenConfig config, ResolvedTerrainStyle style,
                                 RuffleField ruffle, ValueNoiseField surface_texture,
                                 ValueNoiseField mottle, PeriodicPatternGrid surface_pattern,
                                 CellularField cellular, PeriodicPatternGrid edge_pattern,
                                 std::vector<MotifPlacement> pattern_placements,
                                 std::vector<MotifPlacement> detail_placements)
    : config_(std::move(config)),
      style_(std::move(style)),
      ruffle_(std::move(ruffle)),
      surface_texture_(std::move(surface_texture)),
      mottle_(std::move(mottle)),
      surface_pattern_(std::move(surface_pattern)),
      cellular_(std::move(cellular)),
      edge_pattern_(std::move(edge_pattern)),
      pattern_placements_(std::move(pattern_placements)),
      detail_placements_(std::move(detail_placements)),
      resolution_(config_.tile_size * config_.supersample),
      canvas_(resolution_ * 3) {}

absl::StatusOr<TerrainRenderer> TerrainRenderer::Create(TerrainGenConfig config) {
  if (config.tile_size <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("tile size must be positive; got ", config.tile_size));
  }
  if (config.supersample < 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("supersample must be at least 1; got ", config.supersample));
  }
  if (config.variant_period < 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("variant period must be at least 1; got ", config.variant_period));
  }

  ASSIGN_OR_RETURN(ResolvedTerrainStyle style, ResolveTerrainStyle(config));
  const absl::Span<const TerrainMotif> pattern_motifs =
      TerrainSubstrateMotifsFor(config.interior.pattern.family, config.pixel_profile);
  const absl::Span<const TerrainMotif> detail_motifs =
      TerrainDetailMotifsFor(config.interior.details.family, config.pixel_profile);
  const absl::Span<const TerrainEdgeMotif> edge_motifs =
      TerrainEdgeMotifsFor(config.surface.edge_detail.family, config.pixel_profile);
  RETURN_IF_ERROR(ValidateTerrainMotifs(pattern_motifs));
  RETURN_IF_ERROR(ValidateTerrainMotifs(detail_motifs));
  RETURN_IF_ERROR(ValidateTerrainEdgeMotifs(edge_motifs));

  // A magnified stamp wider than a tile can never satisfy the interior test, so
  // it would place nothing and render an empty layer. Refusing here names the
  // cause instead of leaving the author to wonder where the pattern went. The
  // check lives with the renderer because terrain_style.cc cannot see the motif
  // banks without a dependency cycle.
  const auto largest_stamp = [](absl::Span<const TerrainMotif> motifs) {
    int largest = 0;
    for (const TerrainMotif& motif : motifs)
      largest = std::max({largest, motif.width, motif.height});
    return largest;
  };
  const int pattern_extent = largest_stamp(pattern_motifs) * config.interior.pattern.scale;
  const int detail_extent = largest_stamp(detail_motifs) * config.interior.details.scale;
  if (pattern_extent > config.tile_size || detail_extent > config.tile_size) {
    return absl::InvalidArgumentError(absl::StrCat(
        "motif size ", std::max(config.interior.pattern.scale, config.interior.details.scale),
        " needs ", std::max(pattern_extent, detail_extent), " pixels, more than the ",
        config.tile_size, " pixel tile"));
  }

  const int resolution = config.tile_size * config.supersample;
  const int period = resolution * config.variant_period;

  ASSIGN_OR_RETURN(RuffleField ruffle,
                   RuffleField::Create(period, resolution, config.surface.ruffle_density,
                                       config.surface.ruffle_sharpness,
                                       config.surface.ruffle_octaves, config.seed));
  const int surface_cells = std::max(
      1, static_cast<int>(std::lround(static_cast<float>(config.tile_size * config.variant_period) /
                                      style.surface_texture_size)));
  ASSIGN_OR_RETURN(
      ValueNoiseField surface_texture,
      ValueNoiseField::Create(period, surface_cells, /*octaves=*/2, config.seed ^ 0x41c64e6d));
  // Value noise, not sinusoids, breaks up the interior: see ValueNoiseField.
  // Its seed is offset so the mottling cannot line up with the surface ruffle.
  const int mottle_cells = std::max(
      1,
      static_cast<int>(std::lround(config.interior.base.mottle_density * config.variant_period)));
  ASSIGN_OR_RETURN(
      ValueNoiseField mottle,
      ValueNoiseField::Create(period, mottle_cells, /*octaves=*/3, config.seed ^ 0x9e3779b9));
  const int final_period = config.tile_size * config.variant_period;
  ASSIGN_OR_RETURN(PeriodicPatternGrid surface_pattern,
                   PeriodicPatternGrid::Create(final_period, style.surface_pattern_cells));
  ASSIGN_OR_RETURN(PeriodicPatternGrid edge_pattern,
                   PeriodicPatternGrid::Create(final_period, style.edge_pattern_cells));

  CellularField cellular;
  if (config.interior.base.style == TerrainInteriorStyle::kSoilClods ||
      config.interior.base.style == TerrainInteriorStyle::kCobbles) {
    ASSIGN_OR_RETURN(cellular,
                     CellularField::Create(final_period, style.interior_cells, config.seed));
  }

  const auto build_placements = [&](absl::Span<const TerrainMotif> motifs, int density, int spacing,
                                    uint64_t seed) {
    const int target = density * config.variant_period * config.variant_period;
    const int spacing_squared = spacing * spacing;
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<int> position(0, final_period - 1);
    std::vector<MotifPlacement> placements;
    for (int attempt = 0; !motifs.empty() && attempt < std::max(32, target * 80) &&
                          static_cast<int>(placements.size()) < target;
         ++attempt) {
      MotifPlacement candidate{position(generator), position(generator),
                               generator() % motifs.size()};
      bool crowded = false;
      for (const MotifPlacement& placed : placements) {
        int dx = std::abs(candidate.x - placed.x);
        int dy = std::abs(candidate.y - placed.y);
        dx = std::min(dx, final_period - dx);
        dy = std::min(dy, final_period - dy);
        if (dx * dx + dy * dy < spacing_squared) {
          crowded = true;
          break;
        }
      }
      if (!crowded) placements.push_back(candidate);
    }
    return placements;
  };
  std::vector<MotifPlacement> pattern_placements =
      build_placements(pattern_motifs, config.interior.pattern.density, style.pattern_spacing,
                       config.seed ^ 0xc2b2ae35);
  std::vector<MotifPlacement> detail_placements =
      build_placements(detail_motifs, config.interior.details.density, style.detail_spacing,
                       config.seed ^ 0x85ebca6b);
  return TerrainRenderer(std::move(config), std::move(style), std::move(ruffle),
                         std::move(surface_texture), std::move(mottle), std::move(surface_pattern),
                         std::move(cellular), std::move(edge_pattern),
                         std::move(pattern_placements), std::move(detail_placements));
}

std::vector<uint8_t> TerrainRenderer::Occupancy(
    absl::Span<const TilePoint> polygon,
    absl::Span<const absl::Span<const TilePoint>> neighbors) const {
  std::vector<uint8_t> occupancy(static_cast<size_t>(canvas_) * canvas_, 0);
  RasterizePolygon(polygon, resolution_, 1, 1, canvas_, occupancy);
  for (int i = 0; i < kNeighborCount; ++i) {
    if (neighbors[i].empty()) continue;
    RasterizePolygon(neighbors[i], resolution_, kNeighborCellX[i], kNeighborCellY[i], canvas_,
                     occupancy);
  }
  return occupancy;
}

TerrainRenderer::SurfaceField TerrainRenderer::MeasureSurface(const std::vector<float>& depth,
                                                              int origin_x, int origin_y) const {
  SurfaceField surface;
  surface.band.resize(depth.size(), 0.0f);
  surface.normal_x.resize(depth.size(), 0.0f);
  surface.upness.resize(depth.size(), 0.0f);

  for (int y = 0; y < canvas_; ++y) {
    for (int x = 0; x < canvas_; ++x) {
      const size_t index = static_cast<size_t>(y) * canvas_ + x;
      // Central differences, one-sided at the canvas edge. The depth gradient
      // points away from air, so a positive y component means the air is above
      // this pixel and the surface is facing up.
      const int up = std::max(0, y - 1);
      const int down = std::min(canvas_ - 1, y + 1);
      const int left = std::max(0, x - 1);
      const int right = std::min(canvas_ - 1, x + 1);
      const float dy = (depth[static_cast<size_t>(down) * canvas_ + x] -
                        depth[static_cast<size_t>(up) * canvas_ + x]) /
                       static_cast<float>(down - up == 0 ? 1 : down - up);
      const float dx = (depth[static_cast<size_t>(y) * canvas_ + right] -
                        depth[static_cast<size_t>(y) * canvas_ + left]) /
                       static_cast<float>(right - left == 0 ? 1 : right - left);

      const float length = std::hypot(dx, dy) + 1e-6f;
      const float upness = std::clamp(dy / length, -1.0f, 1.0f);
      surface.normal_x[index] = std::clamp(dx / length, -1.0f, 1.0f);
      surface.upness[index] = upness;
      const float facing_depth =
          upness >= 0.0f
              ? style_.surface_side_depth +
                    (style_.surface_top_depth - style_.surface_side_depth) * upness
              : style_.surface_side_depth +
                    (style_.surface_underside_depth - style_.surface_side_depth) * -upness;

      // Ruffle strength tracks local coverage. This exactly represents the v1
      // underside-bias model after migration, while still allowing newly
      // authored side and underside depths to vary independently.
      const float ruffle_scale = facing_depth / style_.surface_top_depth;

      // Sampled in atlas-global coordinates so neighbouring tiles agree.
      const float ruffle = ruffle_.Value(origin_x + x - resolution_, origin_y + y - resolution_);
      surface.band[index] =
          static_cast<float>(config_.supersample) *
          (facing_depth + style_.ruffle_amplitude * ruffle_scale * (ruffle * 2.0f - 1.0f));
    }
  }
  return surface;
}

void TerrainRenderer::ApplyEdgeDetails(std::vector<uint8_t>& indices,
                                       const std::vector<float>& depth, const SurfaceField& surface,
                                       int origin_x, int origin_y) const {
  const TerrainEdgeDetailConfig& config = config_.surface.edge_detail;
  if (config.family == TerrainEdgeDetailSet::kNone || config.amount <= 0.0f ||
      style_.edge_detail_length <= 0) {
    return;
  }

  const absl::Span<const TerrainEdgeMotif> motifs =
      TerrainEdgeMotifsFor(config.family, config_.pixel_profile);
  if (motifs.empty()) return;  // Create validated this, but keep the pass total.

  const int tile = config_.tile_size;
  const int step = config_.supersample;
  const float sample_count = static_cast<float>(step * step);
  const float cell_width = static_cast<float>(edge_pattern_.period_px()) /
                           static_cast<float>(edge_pattern_.cells_per_period());
  const int tile_origin_x = origin_x / step;
  const int tile_origin_y = origin_y / step;

  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      const size_t pixel = static_cast<size_t>(y) * tile + x;
      if (indices[pixel] == kIndexEmpty || indices[pixel] == kIndexOutline) continue;

      float depth_sum = 0.0f;
      float band_sum = 0.0f;
      float normal_x_sum = 0.0f;
      float upness_sum = 0.0f;
      for (int sy = 0; sy < step; ++sy) {
        for (int sx = 0; sx < step; ++sx) {
          const size_t at = static_cast<size_t>(resolution_ + y * step + sy) * canvas_ +
                            (resolution_ + x * step + sx);
          depth_sum += depth[at];
          band_sum += surface.band[at];
          normal_x_sum += surface.normal_x[at];
          upness_sum += surface.upness[at];
        }
      }
      const float d = depth_sum / sample_count / static_cast<float>(step);
      const float band = band_sum / sample_count / static_cast<float>(step);
      const float extension = d - band;
      if (extension <= 0.0f || extension > static_cast<float>(style_.edge_detail_length)) continue;

      const float normal_x = std::clamp(normal_x_sum / sample_count, -1.0f, 1.0f);
      const float upness = std::clamp(upness_sum / sample_count, -1.0f, 1.0f);
      // Grass and snow belong on ground-facing edges. Moss is the family that
      // deliberately wraps down walls, but still stops short of undersides.
      const float minimum_upness =
          config.family == TerrainEdgeDetailSet::kMossFringe ? -0.35f : 0.15f;
      if (upness < minimum_upness) continue;

      // The dominant tangent axis gives crisp pixel stamps on cardinal edges
      // while the distance-field extension makes them follow arbitrary slopes.
      const bool tangent_is_x = std::abs(upness) >= std::abs(normal_x);
      const int tangent = tangent_is_x ? tile_origin_x + x : tile_origin_y + y;
      const int orientation = (tangent_is_x ? 0 : 2) + ((tangent_is_x ? upness : normal_x) < 0);
      const int cell = edge_pattern_.Cell(tangent);
      const uint64_t key = config_.seed ^ 0xd6e8feb86659fd93ULL ^
                           (static_cast<uint64_t>(cell) * 0x9e3779b97f4a7c15ULL) ^
                           (static_cast<uint64_t>(orientation) << 56);
      if (HashUnit(key) >= config.amount) continue;

      const TerrainEdgeMotif& motif = motifs[MixBits(key ^ 0xa0761d6478bd642fULL) % motifs.size()];
      float phase = edge_pattern_.Phase(tangent);
      phase -= config.lean * extension / std::max(1.0f, cell_width);
      if (phase < -0.5f || phase >= 0.5f) continue;
      const int source_x =
          std::min(static_cast<int>(motif.depths.size()) - 1,
                   static_cast<int>((phase + 0.5f) * static_cast<float>(motif.depths.size())));
      const uint8_t relative_depth = motif.depths[static_cast<size_t>(source_x)];
      if (relative_depth == 0) continue;
      const int painted_depth =
          std::max(1, static_cast<int>(std::lround(static_cast<float>(style_.edge_detail_length) *
                                                   static_cast<float>(relative_depth) / 4.0f)));
      if (extension > static_cast<float>(painted_depth)) continue;

      const float along = extension / static_cast<float>(painted_depth);
      const float highlight_roll =
          HashUnit(key ^ (static_cast<uint64_t>(source_x + 1) * 0xe7037ed1a0b428dbULL));
      if (along <= 0.38f && highlight_roll < config.highlight) {
        indices[pixel] = kIndexSurfaceTextureHigh;
      } else if (along >= 0.78f) {
        indices[pixel] = kIndexSurfaceShade;
      } else {
        indices[pixel] = kIndexSurface;
      }
    }
  }
}

std::vector<uint8_t> TerrainRenderer::Classify(const std::vector<uint8_t>& occupancy,
                                               const std::vector<float>& depth,
                                               const SurfaceField& surface) const {
  const int tile = config_.tile_size;
  const int step = config_.supersample;
  const float samples = static_cast<float>(step) * step;
  std::vector<uint8_t> indices(static_cast<size_t>(tile) * tile, kIndexEmpty);

  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      float solid_sum = 0.0f;
      float depth_sum = 0.0f;
      float band_sum = 0.0f;
      float upness_sum = 0.0f;
      for (int sy = 0; sy < step; ++sy) {
        for (int sx = 0; sx < step; ++sx) {
          const size_t at = static_cast<size_t>(resolution_ + y * step + sy) * canvas_ +
                            (resolution_ + x * step + sx);
          solid_sum += occupancy[at];
          depth_sum += depth[at];
          band_sum += surface.band[at];
          upness_sum += surface.upness[at];
        }
      }
      if (solid_sum / samples < 0.5f) continue;

      // Depth and band are measured in supersampled pixels; the layer depths
      // the caller configured are in final ones.
      const float d = depth_sum / samples / static_cast<float>(step);
      const float b = band_sum / samples / static_cast<float>(step);
      const float upness = std::clamp(upness_sum / samples, -1.0f, 1.0f);
      const size_t index = static_cast<size_t>(y) * tile + x;

      indices[index] = kIndexInterior;
      if (d > b + static_cast<float>(style_.contact_depth)) {
        // Up-facing ground remains the ordinary interior. Vertical and
        // downward faces can carry a deeper, independently shaded wall.
        const float wall_facing = 1.0f - std::max(0.0f, upness);
        const float wall_end = b + static_cast<float>(style_.contact_depth) +
                               static_cast<float>(style_.wall_depth) * wall_facing;
        if (d <= wall_end) indices[index] = kIndexWall;
        continue;
      }
      indices[index] = kIndexContact;
      if (d > b) continue;
      indices[index] = kIndexSurfaceShade;
      if (d <= b - static_cast<float>(style_.shade_depth)) indices[index] = kIndexSurface;
      if (d <= static_cast<float>(style_.outline_depth + style_.highlight_depth)) {
        indices[index] = kIndexSurfaceHigh;
      }
      if (d <= static_cast<float>(style_.outline_depth)) indices[index] = kIndexOutline;
    }
  }
  return indices;
}

void TerrainRenderer::ApplySurfaceTexture(std::vector<uint8_t>& indices, int origin_x,
                                          int origin_y) const {
  if (config_.material.surface_style == TerrainSurfaceStyle::kSmooth ||
      config_.surface.texture_amount <= 0.0f) {
    return;
  }

  const int tile = config_.tile_size;
  const int step = config_.supersample;
  const float amount = config_.surface.texture_amount;
  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      const size_t index = static_cast<size_t>(y) * tile + x;
      if (indices[index] != kIndexSurface) continue;

      const int global_x = origin_x + x * step;
      const int global_y = origin_y + y * step;
      const float texture = surface_texture_.Value(global_x, global_y);
      float high_threshold = 1.0f - amount * 0.42f;
      float shade_threshold = amount * 0.28f;

      if (config_.material.surface_style == TerrainSurfaceStyle::kScalloped) {
        const float dx = surface_pattern_.Phase(global_x / step);
        const float dy = surface_pattern_.Phase(global_y / step);
        const float lobe = std::hypot(dx, dy) * 2.0f;
        if (lobe < 0.72f && texture > 0.35f) {
          indices[index] = kIndexSurfaceTextureHigh;
        } else if (lobe > 0.90f && texture < 0.65f) {
          indices[index] = kIndexSurfaceTextureShade;
        }
        continue;
      }

      if (config_.material.surface_style == TerrainSurfaceStyle::kTufted) {
        const int final_x = global_x / step;
        const int final_y = global_y / step;
        const float line_half_width = static_cast<float>(style_.surface_pattern_cells) /
                                      (2.0f * surface_pattern_.period_px());
        if (std::abs(surface_pattern_.Phase(final_x + 2 * final_y)) <= line_half_width &&
            texture > 0.35f) {
          high_threshold -= 0.18f;
        }
      }
      if (config_.material.surface_style == TerrainSurfaceStyle::kMossy) {
        shade_threshold += 0.10f;
      }

      if (texture > high_threshold) indices[index] = kIndexSurfaceTextureHigh;
      if (texture < shade_threshold) indices[index] = kIndexSurfaceTextureShade;
    }
  }
}

void TerrainRenderer::ApplyInteriorTexture(std::vector<uint8_t>& indices, int origin_x,
                                           int origin_y) const {
  if (config_.interior.base.style == TerrainInteriorStyle::kFlat) return;

  const int tile = config_.tile_size;
  const int step = config_.supersample;
  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      const size_t index = static_cast<size_t>(y) * tile + x;
      if (indices[index] != kIndexInterior) continue;

      const int global_x = origin_x + x * step;
      const int global_y = origin_y + y * step;
      const float mottle = mottle_.Value(global_x, global_y);
      if (config_.interior.base.style == TerrainInteriorStyle::kMottle) {
        if (mottle > 1.0f - config_.interior.base.mottle_coverage) {
          indices[index] = kIndexInteriorShade;
        }
        continue;
      }

      const float px = static_cast<float>(global_x / step);
      const float py = static_cast<float>(global_y / step);
      const float cell_size = static_cast<float>(tile * config_.variant_period) /
                              static_cast<float>(style_.interior_cells);
      const float seam_width =
          config_.interior.base.relief * cell_size *
          (config_.interior.base.style == TerrainInteriorStyle::kCobbles ? 1.25f : 0.85f);
      if (cellular_.BoundaryDistance(static_cast<int>(px), static_cast<int>(py)) < seam_width) {
        indices[index] = kIndexInteriorShade;
      } else if (mottle > 1.0f - config_.interior.base.relief * 0.12f) {
        indices[index] = kIndexInteriorHigh;
      }
    }
  }
}

void TerrainRenderer::PlaceSubstratePattern(std::vector<uint8_t>& indices, int origin_x,
                                            int origin_y) const {
  const MotifLayer layer{
      .stamps = TerrainSubstrateMotifsFor(config_.interior.pattern.family, config_.pixel_profile),
      .placements = pattern_placements_,
      .margin = style_.pattern_margin,
      .scale = style_.pattern_scale,
      .accent_mode = config_.interior.pattern.accent_mode,
      .substrate_layer = true};
  ApplyMotifs(indices, origin_x, origin_y, layer);
}

void TerrainRenderer::PlaceDetails(std::vector<uint8_t>& indices, int origin_x,
                                   int origin_y) const {
  const MotifLayer layer{
      .stamps = TerrainDetailMotifsFor(config_.interior.details.family, config_.pixel_profile),
      .placements = detail_placements_,
      .margin = style_.detail_margin,
      .scale = style_.detail_scale,
      .accent_mode = config_.interior.details.accent_mode,
      .substrate_layer = false};
  ApplyMotifs(indices, origin_x, origin_y, layer);
}

std::vector<uint8_t> TerrainRenderer::LegalMotifPixels(const std::vector<uint8_t>& indices,
                                                       int margin) const {
  const int tile = config_.tile_size;
  std::vector<uint8_t> legal(indices.size(), 0);
  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      const size_t index = static_cast<size_t>(y) * tile + x;
      if (!IsInteriorIndex(indices[index])) continue;
      bool clear = true;
      for (int oy = -margin; oy <= margin && clear; ++oy) {
        for (int ox = -margin; ox <= margin && clear; ++ox) {
          const int ny = y + oy;
          const int nx = x + ox;
          if (ny < 0 || nx < 0 || ny >= tile || nx >= tile) continue;
          clear = IsInteriorIndex(indices[static_cast<size_t>(ny) * tile + nx]);
        }
      }
      legal[index] = clear ? 1 : 0;
    }
  }
  return legal;
}

void TerrainRenderer::StampMotif(std::vector<uint8_t>& indices, const std::vector<uint8_t>& legal,
                                 const TerrainMotif& stamp, int x0, int y0,
                                 const MotifLayer& layer) const {
  const int tile = config_.tile_size;
  const int scale = layer.scale;
  const int drawn_width = stamp.width * scale;
  const int drawn_height = stamp.height * scale;
  if (x0 >= tile || y0 >= tile || x0 + drawn_width <= 0 || y0 + drawn_height <= 0) return;

  // Source coordinates, so magnifying a motif magnifies its shape rather than
  // resampling it.
  const auto source_at = [&stamp, scale](int sx, int sy) {
    return stamp.pixels[static_cast<size_t>(sy / scale) * stamp.width + sx / scale];
  };

  bool visible = false;
  for (int sy = 0; sy < drawn_height; ++sy) {
    for (int sx = 0; sx < drawn_width; ++sx) {
      if (source_at(sx, sy) == TerrainMotifPixel::kTransparent) continue;
      const int px = x0 + sx;
      const int py = y0 + sy;
      if (px < 0 || py < 0 || px >= tile || py >= tile) continue;
      if (legal[static_cast<size_t>(py) * tile + px] == 0) return;
      visible = true;
    }
  }
  if (!visible) return;

  const MotifPaint paint{.accent_mode = layer.accent_mode,
                         .substrate_layer = layer.substrate_layer};
  // The sweep runs along the light axis so the two accent modes agree about
  // which end of a motif is lit.
  const auto [gradient_low, gradient_high] = MotifGradientSpan(stamp);
  const float gradient_span = static_cast<float>(std::max(1, gradient_high - gradient_low));
  for (int sy = 0; sy < drawn_height; ++sy) {
    for (int sx = 0; sx < drawn_width; ++sx) {
      const TerrainMotifPixel stamp_pixel = source_at(sx, sy);
      if (stamp_pixel == TerrainMotifPixel::kTransparent) continue;
      const int px = x0 + sx;
      const int py = y0 + sy;
      if (px < 0 || py < 0 || px >= tile || py >= tile) continue;

      // Lighting is measured in source pixels, so a magnified motif gets a
      // shade edge scale pixels thick rather than a hairline lost on it.
      const int source_x = sx / scale;
      const int source_y = sy / scale;
      const int lx = source_x - kLightDx;
      const int ly = source_y - kLightDy;
      const bool lit = lx >= 0 && ly >= 0 && lx < stamp.width && ly < stamp.height &&
                       stamp.pixels[static_cast<size_t>(ly) * stamp.width + lx] !=
                           TerrainMotifPixel::kTransparent;
      const float gradient_t =
          static_cast<float>(source_x + source_y - gradient_low) / gradient_span;
      indices[static_cast<size_t>(py) * tile + px] =
          MotifPixelIndex(stamp_pixel, lit, std::clamp(gradient_t, 0.0f, 1.0f), paint);
    }
  }
}

void TerrainRenderer::ApplyMotifs(std::vector<uint8_t>& indices, int origin_x, int origin_y,
                                  const MotifLayer& layer) const {
  if (layer.placements.empty() || layer.stamps.empty()) return;

  const std::vector<uint8_t> legal = LegalMotifPixels(indices, layer.margin);

  const int tile = config_.tile_size;
  const int period = tile * config_.variant_period;
  const int tile_origin_x = origin_x / config_.supersample;
  const int tile_origin_y = origin_y / config_.supersample;
  for (const MotifPlacement& placement : layer.placements) {
    const TerrainMotif& stamp = layer.stamps[placement.motif];
    const int drawn_width = stamp.width * layer.scale;
    const int drawn_height = stamp.height * layer.scale;

    int centre_x = placement.x - tile_origin_x;
    int centre_y = placement.y - tile_origin_y;
    while (centre_x < -drawn_width) centre_x += period;
    while (centre_x >= tile + drawn_width) centre_x -= period;
    while (centre_y < -drawn_height) centre_y += period;
    while (centre_y >= tile + drawn_height) centre_y -= period;

    for (const int wrap_y : {-period, 0, period}) {
      for (const int wrap_x : {-period, 0, period}) {
        StampMotif(indices, legal, stamp, centre_x + wrap_x - drawn_width / 2,
                   centre_y + wrap_y - drawn_height / 2, layer);
      }
    }
  }
}

RgbaImage TerrainRenderer::Colorize(const std::vector<uint8_t>& indices) const {
  const std::array<Rgba, kIndexCount> palette =
      BuildPalette(config_.material, config_.interior.pattern.contrast,
                   config_.surface.wall_darkness, style_.compact_palette);

  RgbaImage image;
  image.width = config_.tile_size;
  image.height = config_.tile_size;
  image.pixels.resize(indices.size() * 4);
  for (size_t i = 0; i < indices.size(); ++i) {
    const Rgba& color = palette[indices[i]];
    image.pixels[i * 4 + 0] = color.r;
    image.pixels[i * 4 + 1] = color.g;
    image.pixels[i * 4 + 2] = color.b;
    image.pixels[i * 4 + 3] = color.a;
  }
  return image;
}

RgbaImage TerrainRenderer::RenderTile(absl::Span<const TilePoint> polygon,
                                      absl::Span<const absl::Span<const TilePoint>> neighbors,
                                      int variant) const {
  const int origin_x = (variant % config_.variant_period) * resolution_;
  const int origin_y = (variant / config_.variant_period) * resolution_;

  const std::vector<uint8_t> occupancy = Occupancy(polygon, neighbors);
  std::vector<float> depth = SquaredDistanceTransform(occupancy, canvas_, canvas_);
  for (float& value : depth) value = std::sqrt(value);

  const SurfaceField surface = MeasureSurface(depth, origin_x, origin_y);
  std::vector<uint8_t> indices = Classify(occupancy, depth, surface);
  ApplyEdgeDetails(indices, depth, surface, origin_x, origin_y);
  ApplySurfaceTexture(indices, origin_x, origin_y);
  ApplyInteriorTexture(indices, origin_x, origin_y);
  PlaceSubstratePattern(indices, origin_x, origin_y);
  PlaceDetails(indices, origin_x, origin_y);
  return Colorize(indices);
}

absl::StatusOr<RgbaImage> TerrainRenderer::RenderBlobTile(uint8_t mask, int variant) const {
  if (variant < 0 || variant >= variant_count()) {
    return absl::InvalidArgumentError(
        absl::StrCat("variant ", variant, " is outside the ", variant_count(), " this set holds"));
  }
  if (NormalizeNeighborMask(mask) != mask) {
    return absl::InvalidArgumentError(
        absl::StrCat("mask ", static_cast<int>(mask), " is not normalized"));
  }

  const absl::Span<const TilePoint> square = TileShapePolygon(TileShape::kFullBlock);
  std::vector<absl::Span<const TilePoint>> neighbors(kNeighborCount);
  for (int i = 0; i < kNeighborCount; ++i) {
    neighbors[i] = (mask & (1 << i)) != 0 ? square : absl::Span<const TilePoint>();
  }

  return RenderTile(square, neighbors, variant);
}

absl::StatusOr<RgbaImage> TerrainRenderer::RenderShapeTile(TileShape shape, int variant) const {
  if (variant < 0 || variant >= variant_count()) {
    return absl::InvalidArgumentError(
        absl::StrCat("variant ", variant, " is outside the ", variant_count(), " this set holds"));
  }
  const absl::Span<const TilePoint> polygon = TileShapePolygon(shape);
  if (polygon.empty()) {
    return absl::InvalidArgumentError("kNone has no artwork to render");
  }

  std::vector<absl::Span<const TilePoint>> neighbors(kNeighborCount);
  AutoContext(polygon, neighbors);
  ApplyPartner(shape, neighbors);

  return RenderTile(polygon, neighbors, variant);
}

absl::StatusOr<RgbaImage> RenderTerrainPreviewScene(const TerrainRenderer& renderer) {
  // Chosen to exercise every edge case the brush can produce: a long flat run
  // for the surface rhythm, a step, a one-cell pillar, an overhang, and a
  // closed pocket whose four corners are the concave ones a 16-tile set cannot
  // express.
  static constexpr const char* kScene[] = {
      "..........", "...##.....", "..####.##.", ".#########",
      "##########", "###..#####", "##########",
  };
  constexpr int kSceneHeight = sizeof(kScene) / sizeof(kScene[0]);
  const int scene_width = static_cast<int>(std::strlen(kScene[0]));

  const int tile = renderer.config().tile_size;
  const int period = renderer.config().variant_period;

  RgbaImage image;
  image.width = scene_width * tile;
  image.height = kSceneHeight * tile;
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 0);

  const auto solid = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= scene_width || y >= kSceneHeight) return false;
    return kScene[y][x] == '#';
  };

  for (int y = 0; y < kSceneHeight; ++y) {
    for (int x = 0; x < scene_width; ++x) {
      if (!solid(x, y)) continue;

      uint8_t mask = 0;
      for (int i = 0; i < kNeighborCount; ++i) {
        // kNeighborCellX/Y are 3x3 cell positions; recentre them on the tile.
        if (solid(x + kNeighborCellX[i] - 1, y + kNeighborCellY[i] - 1)) mask |= 1 << i;
      }
      mask = NormalizeNeighborMask(mask);

      const int variant = period > 0 ? (y % period) * period + (x % period) : 0;
      ASSIGN_OR_RETURN(const RgbaImage cell, renderer.RenderBlobTile(mask, variant));
      RETURN_IF_ERROR(CopyTile(cell, 0, 0, tile, image, x * tile, y * tile));
    }
  }
  return image;
}

absl::StatusOr<Blob47Atlas> GenerateBlob47Atlas(const TerrainGenConfig& config) {
  ASSIGN_OR_RETURN(TerrainRenderer renderer, TerrainRenderer::Create(config));

  const int tile = config.tile_size;
  const int variants = renderer.variant_count();
  const absl::Span<const uint8_t> masks = Blob47MaskTable();

  // Slope units are appended below the blob blocks, filling rows of the same
  // width, exactly as ComposeBlob47 places hand-drawn ones.
  const int slope_rows = (kSlopeShapeCount + kBlob47Columns - 1) / kBlob47Columns;

  Blob47Atlas atlas;
  atlas.tile_size = tile;
  // Generated variants are phases of one pattern, not interchangeable
  // drawings, so the terrain has to lay them back down in phase.
  atlas.variant_period = config.variant_period;
  atlas.image.width = kBlob47Columns * tile;
  atlas.image.height = (kBlob47Rows * variants + slope_rows) * tile;
  atlas.image.pixels.assign(static_cast<size_t>(atlas.image.width) * atlas.image.height * 4, 0);
  atlas.tiles.reserve(static_cast<size_t>(masks.size()) * variants);

  for (int variant = 0; variant < variants; ++variant) {
    for (int index = 0; index < static_cast<int>(masks.size()); ++index) {
      const int target_x = (index % kBlob47Columns) * tile;
      const int target_y = (variant * kBlob47Rows + index / kBlob47Columns) * tile;

      ASSIGN_OR_RETURN(const RgbaImage cell, renderer.RenderBlobTile(masks[index], variant));
      RETURN_IF_ERROR(CopyTile(cell, 0, 0, tile, atlas.image, target_x, target_y));

      atlas.tiles.push_back(ComposedTile{
          .index = index,
          .mask = masks[index],
          .variant = variant,
          .source_x = target_x,
          .source_y = target_y,
      });
    }
  }

  const int slope_origin_row = kBlob47Rows * variants;
  for (int i = 0; i < kSlopeShapeCount; ++i) {
    const TileShape shape = static_cast<TileShape>(kFirstSlopeShape + i);
    const int target_x = (i % kBlob47Columns) * tile;
    const int target_y = (slope_origin_row + i / kBlob47Columns) * tile;

    ASSIGN_OR_RETURN(const RgbaImage cell, renderer.RenderShapeTile(shape, /*variant=*/0));
    RETURN_IF_ERROR(CopyTile(cell, 0, 0, tile, atlas.image, target_x, target_y));

    atlas.slopes.push_back(ComposedSlope{
        .shape = shape,
        .source_x = target_x,
        .source_y = target_y,
    });
  }

  return atlas;
}

}  // namespace zebes
