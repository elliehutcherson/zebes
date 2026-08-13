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
  kIndexAccentPrimary = 17,
  kIndexAccentSecondary = 18,
  kIndexCount = 19,
};

// Where the light comes from, as a pixel offset. Only decoration shading uses
// it; everything else takes its shading from surface orientation.
constexpr int kLightDx = -1;
constexpr int kLightDy = -1;

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
                                           bool compact_palette) {
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
  palette[kIndexAccentPrimary] = ToRgba(ToHsv(material.accent_primary));
  palette[kIndexAccentSecondary] = ToRgba(ToHsv(material.accent_secondary));
  return palette;
}

uint8_t MotifPixelIndex(TerrainMotifPixel pixel, bool lit, bool substrate_layer,
                        bool auto_as_accent) {
  switch (pixel) {
    case TerrainMotifPixel::kTransparent:
      return kIndexEmpty;
    case TerrainMotifPixel::kAutoShaded:
      if (auto_as_accent) {
        return lit ? kIndexAccentPrimary : kIndexAccentSecondary;
      }
      if (substrate_layer) return lit ? kIndexPattern : kIndexPatternShade;
      return lit ? kIndexDecor : kIndexDecorShade;
    case TerrainMotifPixel::kDecor:
      return substrate_layer ? kIndexPattern : kIndexDecor;
    case TerrainMotifPixel::kDecorShade:
      return substrate_layer ? kIndexPatternShade : kIndexDecorShade;
    case TerrainMotifPixel::kBotanical:
      return kIndexBotanical;
    case TerrainMotifPixel::kBotanicalShade:
      return kIndexBotanicalShade;
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
                                 CellularField cellular,
                                 std::vector<MotifPlacement> pattern_placements,
                                 std::vector<MotifPlacement> detail_placements)
    : config_(std::move(config)),
      style_(std::move(style)),
      ruffle_(std::move(ruffle)),
      surface_texture_(std::move(surface_texture)),
      mottle_(std::move(mottle)),
      surface_pattern_(std::move(surface_pattern)),
      cellular_(std::move(cellular)),
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
  RETURN_IF_ERROR(ValidateTerrainMotifs(pattern_motifs));
  RETURN_IF_ERROR(ValidateTerrainMotifs(detail_motifs));

  const int resolution = config.tile_size * config.supersample;
  const int period = resolution * config.variant_period;

  ASSIGN_OR_RETURN(
      RuffleField ruffle,
      RuffleField::Create(period, resolution, config.ruffle_density, config.ruffle_sharpness,
                          config.ruffle_octaves, config.seed));
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
                         std::move(cellular), std::move(pattern_placements),
                         std::move(detail_placements));
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

std::vector<float> TerrainRenderer::SurfaceBand(const std::vector<float>& depth, int origin_x,
                                                int origin_y) const {
  std::vector<float> band(depth.size(), 0.0f);
  const float bias = config_.grass_bottom_bias;

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
      const float facing = bias + (1.0f - bias) * (0.5f + 0.5f * upness);

      // Sampled in atlas-global coordinates so neighbouring tiles agree.
      const float ruffle = ruffle_.Value(origin_x + x - resolution_, origin_y + y - resolution_);
      band[index] = static_cast<float>(config_.supersample) * facing *
                    (style_.grass_band + style_.ruffle_amplitude * (ruffle * 2.0f - 1.0f));
    }
  }
  return band;
}

std::vector<uint8_t> TerrainRenderer::Classify(const std::vector<uint8_t>& occupancy,
                                               const std::vector<float>& depth,
                                               const std::vector<float>& band) const {
  const int tile = config_.tile_size;
  const int step = config_.supersample;
  const float samples = static_cast<float>(step) * step;
  std::vector<uint8_t> indices(static_cast<size_t>(tile) * tile, kIndexEmpty);

  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      float solid_sum = 0.0f;
      float depth_sum = 0.0f;
      float band_sum = 0.0f;
      for (int sy = 0; sy < step; ++sy) {
        for (int sx = 0; sx < step; ++sx) {
          const size_t at = static_cast<size_t>(resolution_ + y * step + sy) * canvas_ +
                            (resolution_ + x * step + sx);
          solid_sum += occupancy[at];
          depth_sum += depth[at];
          band_sum += band[at];
        }
      }
      if (solid_sum / samples < 0.5f) continue;

      // Depth and band are measured in supersampled pixels; the layer depths
      // the caller configured are in final ones.
      const float d = depth_sum / samples / static_cast<float>(step);
      const float b = band_sum / samples / static_cast<float>(step);
      const size_t index = static_cast<size_t>(y) * tile + x;

      indices[index] = kIndexInterior;
      if (d > b + static_cast<float>(style_.contact_depth)) continue;
      indices[index] = kIndexContact;
      if (d > b) continue;
      indices[index] = kIndexSurfaceShade;
      if (d <= b - static_cast<float>(style_.grass_shade_depth)) indices[index] = kIndexSurface;
      if (d <= static_cast<float>(style_.outline_width + style_.grass_hi_depth)) {
        indices[index] = kIndexSurfaceHigh;
      }
      if (d <= static_cast<float>(style_.outline_width)) indices[index] = kIndexOutline;
    }
  }
  return indices;
}

void TerrainRenderer::ApplySurfaceTexture(std::vector<uint8_t>& indices, int origin_x,
                                          int origin_y) const {
  if (config_.material.surface_style == TerrainSurfaceStyle::kSmooth ||
      config_.surface_texture_amount <= 0.0f) {
    return;
  }

  const int tile = config_.tile_size;
  const int step = config_.supersample;
  const float amount = config_.surface_texture_amount;
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
  ApplyMotifs(indices, origin_x, origin_y,
              TerrainSubstrateMotifsFor(config_.interior.pattern.family, config_.pixel_profile),
              pattern_placements_, style_.pattern_margin, /*substrate_layer=*/true,
              /*auto_as_accent=*/false);
}

void TerrainRenderer::PlaceDetails(std::vector<uint8_t>& indices, int origin_x,
                                   int origin_y) const {
  const TerrainDetailSet family = config_.interior.details.family;
  ApplyMotifs(indices, origin_x, origin_y, TerrainDetailMotifsFor(family, config_.pixel_profile),
              detail_placements_, style_.detail_margin, /*substrate_layer=*/false,
              /*auto_as_accent=*/family == TerrainDetailSet::kSnow ||
                  family == TerrainDetailSet::kCrystals);
}

void TerrainRenderer::ApplyMotifs(std::vector<uint8_t>& indices, int origin_x, int origin_y,
                                  absl::Span<const TerrainMotif> stamps,
                                  absl::Span<const MotifPlacement> placements, int margin,
                                  bool substrate_layer, bool auto_as_accent) const {
  if (placements.empty() || stamps.empty()) return;

  const int tile = config_.tile_size;
  const auto is_interior = [](uint8_t value) {
    return value == kIndexInterior || value == kIndexInteriorShade || value == kIndexInteriorHigh;
  };

  std::vector<uint8_t> legal(indices.size(), 0);
  for (int y = 0; y < tile; ++y) {
    for (int x = 0; x < tile; ++x) {
      const size_t index = static_cast<size_t>(y) * tile + x;
      if (!is_interior(indices[index])) continue;
      bool clear = true;
      for (int oy = -margin; oy <= margin && clear; ++oy) {
        for (int ox = -margin; ox <= margin && clear; ++ox) {
          const int ny = y + oy;
          const int nx = x + ox;
          if (ny < 0 || nx < 0 || ny >= tile || nx >= tile) continue;
          clear = is_interior(indices[static_cast<size_t>(ny) * tile + nx]);
        }
      }
      legal[index] = clear ? 1 : 0;
    }
  }

  const int period = tile * config_.variant_period;
  const int tile_origin_x = origin_x / config_.supersample;
  const int tile_origin_y = origin_y / config_.supersample;
  for (const MotifPlacement& placement : placements) {
    const TerrainMotif& stamp = stamps[placement.motif];
    int centre_x = placement.x - tile_origin_x;
    int centre_y = placement.y - tile_origin_y;
    while (centre_x < -stamp.width) centre_x += period;
    while (centre_x >= tile + stamp.width) centre_x -= period;
    while (centre_y < -stamp.height) centre_y += period;
    while (centre_y >= tile + stamp.height) centre_y -= period;

    for (const int wrap_y : {-period, 0, period}) {
      for (const int wrap_x : {-period, 0, period}) {
        const int x0 = centre_x + wrap_x - stamp.width / 2;
        const int y0 = centre_y + wrap_y - stamp.height / 2;
        if (x0 >= tile || y0 >= tile || x0 + stamp.width <= 0 || y0 + stamp.height <= 0) continue;

        bool fits = true;
        bool visible = false;
        for (int sy = 0; sy < stamp.height && fits; ++sy) {
          for (int sx = 0; sx < stamp.width && fits; ++sx) {
            const TerrainMotifPixel stamp_pixel =
                stamp.pixels[static_cast<size_t>(sy) * stamp.width + sx];
            if (stamp_pixel == TerrainMotifPixel::kTransparent) continue;
            const int px = x0 + sx;
            const int py = y0 + sy;
            if (px < 0 || py < 0 || px >= tile || py >= tile) continue;
            visible = true;
            fits = legal[static_cast<size_t>(py) * tile + px] != 0;
          }
        }
        if (!fits || !visible) continue;

        for (int sy = 0; sy < stamp.height; ++sy) {
          for (int sx = 0; sx < stamp.width; ++sx) {
            const TerrainMotifPixel stamp_pixel =
                stamp.pixels[static_cast<size_t>(sy) * stamp.width + sx];
            if (stamp_pixel == TerrainMotifPixel::kTransparent) continue;
            const int px = x0 + sx;
            const int py = y0 + sy;
            if (px < 0 || py < 0 || px >= tile || py >= tile) continue;

            const int lx = sx - kLightDx;
            const int ly = sy - kLightDy;
            const bool lit = lx >= 0 && ly >= 0 && lx < stamp.width && ly < stamp.height &&
                             stamp.pixels[static_cast<size_t>(ly) * stamp.width + lx] !=
                                 TerrainMotifPixel::kTransparent;
            indices[static_cast<size_t>(py) * tile + px] =
                MotifPixelIndex(stamp_pixel, lit, substrate_layer, auto_as_accent);
          }
        }
      }
    }
  }
}

RgbaImage TerrainRenderer::Colorize(const std::vector<uint8_t>& indices) const {
  const std::array<Rgba, kIndexCount> palette =
      BuildPalette(config_.material, config_.interior.pattern.contrast, style_.compact_palette);

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

  const std::vector<float> band = SurfaceBand(depth, origin_x, origin_y);
  std::vector<uint8_t> indices = Classify(occupancy, depth, band);
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
