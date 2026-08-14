#include "terrain/terrain_style.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

constexpr int kMaxFieldEdge = 4096;
// The placement builder uses exact pairwise spacing checks. This is generous
// beside the editor's current maximum of 192 placements, while keeping a
// programmatic configuration from turning renderer creation quadratic at an
// unbounded scale.
constexpr int kMaxMotifPlacements = 1024;
constexpr int kMaxTileSize = 256;
constexpr int kMaxSupersample = 8;
constexpr int kMaxVariantPeriod = 4;
constexpr float kMaxReferenceMeasurement = 4096.0f;
// Whether a magnified stamp actually fits its tile depends on the motif bank,
// which this layer cannot see; TerrainRenderer::Create rejects that case. This
// bound only keeps a programmatic configuration from asking for absurdities.
constexpr int kMaxMotifScale = 4;

int ScaleLayer(int value, float scale) {
  if (value <= 0) return 0;
  return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * scale)));
}

bool Finite(float value) { return std::isfinite(value); }

bool Known(TerrainSurfaceStyle value) {
  switch (value) {
    case TerrainSurfaceStyle::kSmooth:
    case TerrainSurfaceStyle::kTufted:
    case TerrainSurfaceStyle::kScalloped:
    case TerrainSurfaceStyle::kMossy:
      return true;
  }
  return false;
}

bool Known(TerrainInteriorStyle value) {
  switch (value) {
    case TerrainInteriorStyle::kFlat:
    case TerrainInteriorStyle::kMottle:
    case TerrainInteriorStyle::kSoilClods:
    case TerrainInteriorStyle::kCobbles:
      return true;
  }
  return false;
}

bool Known(TerrainEdgeDetailSet value) {
  switch (value) {
    case TerrainEdgeDetailSet::kNone:
    case TerrainEdgeDetailSet::kShortGrass:
    case TerrainEdgeDetailSet::kDryGrass:
    case TerrainEdgeDetailSet::kMossFringe:
    case TerrainEdgeDetailSet::kSnowLip:
      return true;
  }
  return false;
}

bool Known(TerrainSubstratePattern value) {
  switch (value) {
    case TerrainSubstratePattern::kNone:
    case TerrainSubstratePattern::kPebbles:
    case TerrainSubstratePattern::kFlecks:
    case TerrainSubstratePattern::kCrosses:
    case TerrainSubstratePattern::kDiamonds:
    case TerrainSubstratePattern::kMixedEarth:
      return true;
  }
  return false;
}

bool Known(TerrainDetailSet value) {
  switch (value) {
    case TerrainDetailSet::kNone:
    case TerrainDetailSet::kMeadow:
    case TerrainDetailSet::kForestFloor:
    case TerrainDetailSet::kSnow:
    case TerrainDetailSet::kCrystals:
      return true;
  }
  return false;
}

bool Known(TerrainAccentMode value) {
  switch (value) {
    case TerrainAccentMode::kMaterial:
    case TerrainAccentMode::kAccent:
    case TerrainAccentMode::kGradient:
      return true;
  }
  return false;
}

}  // namespace

TerrainAccentMode DefaultAccentModeFor(TerrainDetailSet details) {
  switch (details) {
    case TerrainDetailSet::kSnow:
    case TerrainDetailSet::kCrystals:
      return TerrainAccentMode::kAccent;
    case TerrainDetailSet::kNone:
    case TerrainDetailSet::kMeadow:
    case TerrainDetailSet::kForestFloor:
      break;
  }
  return TerrainAccentMode::kMaterial;
}

absl::Span<const TerrainPreset> BuiltInTerrainPresets() {
  static const std::vector<TerrainPreset> kPresets = [] {
    std::vector<TerrainPreset> presets;

    TerrainGenConfig grass;
    grass.material.name = "Classic Grass";
    presets.push_back({grass.material.name, grass});

    TerrainGenConfig meadow;
    meadow.variant_period = 3;
    meadow.surface.top_depth = 10.0f;
    meadow.surface.side_depth = 6.0f;
    meadow.surface.underside_depth = 2.0f;
    meadow.surface.ruffle_amplitude = 3.2f;
    meadow.surface.ruffle_density = 2.5f;
    meadow.surface.highlight_depth = 2;
    meadow.surface.shade_depth = 2;
    meadow.surface.texture_size = 5.0f;
    meadow.surface.texture_amount = 0.65f;
    meadow.surface.edge_detail = TerrainEdgeDetailConfig{
        .family = TerrainEdgeDetailSet::kShortGrass,
        .amount = 0.55f,
        .length = 3,
        .clump_size = 5,
        .lean = 0.05f,
        .highlight = 0.45f,
    };
    meadow.interior.base = TerrainInteriorBaseConfig{
        .style = TerrainInteriorStyle::kSoilClods,
        .mottle_coverage = 0.18f,
        .feature_size = 6.0f,
        .relief = 0.65f,
    };
    meadow.interior.pattern = TerrainSubstratePatternConfig{
        .family = TerrainSubstratePattern::kMixedEarth,
        .density = 5,
        .spacing = 6,
        .contrast = 0.52f,
    };
    meadow.interior.details = TerrainSemanticDetailConfig{
        .family = TerrainDetailSet::kMeadow,
        .density = 1,
        .spacing = 12,
    };
    meadow.material = TerrainMaterial{
        .name = "Cozy Meadow",
        .surface = 0xa8d84f,
        .substrate = 0xb96942,
        .outline = 0x4a3028,
        .accent_primary = 0xffdf70,
        .accent_secondary = 0x8ed2f4,
        .hue_shift = 0.035f,
        .contrast = 0.86f,
        .surface_style = TerrainSurfaceStyle::kScalloped,
    };
    presets.push_back({meadow.material.name, meadow});

    // A thin, light-catching cap over a dark earthen body. Sparse leaning dry
    // grass adds the ragged harvest edge without changing the collision line.
    TerrainGenConfig autumn;
    autumn.variant_period = 3;
    autumn.surface.top_depth = 6.0f;
    autumn.surface.side_depth = 2.0f;
    autumn.surface.underside_depth = 0.5f;
    autumn.surface.ruffle_amplitude = 2.5f;
    autumn.surface.ruffle_density = 1.8f;
    autumn.surface.ruffle_sharpness = 0.75f;
    autumn.surface.highlight_depth = 1;
    autumn.surface.shade_depth = 2;
    autumn.surface.contact_depth = 2;
    autumn.surface.wall_depth = 7;
    autumn.surface.wall_darkness = 1.2f;
    autumn.surface.texture_size = 3.5f;
    autumn.surface.texture_amount = 0.60f;
    autumn.surface.edge_detail = TerrainEdgeDetailConfig{
        .family = TerrainEdgeDetailSet::kDryGrass,
        .amount = 0.72f,
        .length = 5,
        .clump_size = 5,
        .lean = 0.28f,
        .highlight = 0.30f,
    };
    autumn.interior.base = TerrainInteriorBaseConfig{
        .style = TerrainInteriorStyle::kMottle,
        .mottle_density = 2.0f,
        .mottle_coverage = 0.10f,
        .feature_size = 7.0f,
        .relief = 0.25f,
    };
    autumn.interior.pattern = TerrainSubstratePatternConfig{
        .family = TerrainSubstratePattern::kFlecks,
        .density = 1,
        .spacing = 15,
        .margin = 3,
        .contrast = 0.30f,
    };
    autumn.interior.details = TerrainSemanticDetailConfig{
        .family = TerrainDetailSet::kForestFloor,
        .density = 1,
        .spacing = 16,
        .margin = 3,
    };
    autumn.material = TerrainMaterial{
        .name = "Autumn Forest",
        .surface = 0xc99a3b,
        .substrate = 0x2d2922,
        .outline = 0x17150f,
        .accent_primary = 0xe06b32,
        .accent_secondary = 0xf0c34f,
        .hue_shift = 0.025f,
        .contrast = 1.10f,
        .surface_style = TerrainSurfaceStyle::kTufted,
    };
    presets.push_back({autumn.material.name, autumn});

    TerrainGenConfig chunky;
    chunky.tile_size = 16;
    chunky.variant_period = 2;
    chunky.pixel_profile = TerrainPixelProfile::kChunky16;
    chunky.surface.top_depth = 5.0f;
    chunky.surface.side_depth = 4.0f;
    chunky.surface.underside_depth = 3.0f;
    chunky.surface.ruffle_amplitude = 1.3f;
    chunky.surface.ruffle_density = 1.5f;
    chunky.surface.outline_depth = 1;
    chunky.surface.highlight_depth = 1;
    chunky.surface.shade_depth = 1;
    chunky.surface.contact_depth = 1;
    chunky.surface.texture_size = 3.0f;
    chunky.surface.texture_amount = 0.35f;
    chunky.surface.edge_detail = TerrainEdgeDetailConfig{
        .family = TerrainEdgeDetailSet::kShortGrass,
        .amount = 0.42f,
        .length = 2,
        .clump_size = 4,
        .highlight = 0.25f,
    };
    chunky.interior.base = TerrainInteriorBaseConfig{
        .style = TerrainInteriorStyle::kMottle,
        .mottle_density = 2.0f,
        .mottle_coverage = 0.16f,
        .feature_size = 4.0f,
        .relief = 0.35f,
    };
    chunky.interior.pattern = TerrainSubstratePatternConfig{
        .family = TerrainSubstratePattern::kNone,
        .density = 0,
        .spacing = 10,
        .margin = 1,
    };
    chunky.interior.details = TerrainSemanticDetailConfig{
        .family = TerrainDetailSet::kMeadow,
        .density = 1,
        .spacing = 10,
        .margin = 1,
    };
    chunky.material = TerrainMaterial{
        .name = "Chunky Grass 16",
        .surface = 0x82c94b,
        .substrate = 0xb86f42,
        .outline = 0x49352d,
        .accent_primary = 0xf4cf62,
        .accent_secondary = 0xe98a9d,
        .hue_shift = 0.025f,
        .contrast = 1.05f,
        .surface_style = TerrainSurfaceStyle::kTufted,
    };
    presets.push_back({chunky.material.name, chunky});

    struct MaterialPreset {
      TerrainMaterial material;
      TerrainInteriorStyle base = TerrainInteriorStyle::kMottle;
      TerrainSubstratePattern pattern = TerrainSubstratePattern::kPebbles;
      TerrainDetailSet details = TerrainDetailSet::kNone;
    };
    for (const MaterialPreset preset :
         {MaterialPreset{.material =
                             TerrainMaterial{.name = "Sand",
                                             .surface = 0xf2d98a,
                                             .substrate = 0xc99a5b,
                                             .outline = 0x5c4430,
                                             .accent_primary = 0xffefad,
                                             .accent_secondary = 0xd9875f,
                                             .hue_shift = 0.04f,
                                             .surface_style = TerrainSurfaceStyle::kSmooth}},
          MaterialPreset{
              .material = TerrainMaterial{.name = "Snow",
                                          .surface = 0xf4f8ff,
                                          .substrate = 0x8fa3c4,
                                          .outline = 0x3d435c,
                                          .accent_primary = 0xffffff,
                                          .accent_secondary = 0xc7d2ff,
                                          .hue_shift = -0.04f,
                                          .surface_style = TerrainSurfaceStyle::kScalloped},
              .pattern = TerrainSubstratePattern::kNone,
              .details = TerrainDetailSet::kSnow},
          MaterialPreset{.material = TerrainMaterial{.name = "Cave",
                                                     .surface = 0x7d8ea6,
                                                     .substrate = 0x4a4458,
                                                     .outline = 0x252332,
                                                     .accent_primary = 0x9ad8e8,
                                                     .accent_secondary = 0xc39bea,
                                                     .hue_shift = 0.06f,
                                                     .surface_style = TerrainSurfaceStyle::kMossy},
                         .base = TerrainInteriorStyle::kCobbles,
                         .pattern = TerrainSubstratePattern::kNone,
                         .details = TerrainDetailSet::kCrystals},
          MaterialPreset{.material = TerrainMaterial{.name = "Lava",
                                                     .surface = 0xff9d4a,
                                                     .substrate = 0x4b2230,
                                                     .outline = 0x24141d,
                                                     .accent_primary = 0xffe36c,
                                                     .accent_secondary = 0xff5b3d,
                                                     .hue_shift = 0.03f,
                                                     .surface_style = TerrainSurfaceStyle::kTufted},
                         .base = TerrainInteriorStyle::kSoilClods,
                         .pattern = TerrainSubstratePattern::kNone,
                         .details = TerrainDetailSet::kCrystals}}) {
      TerrainGenConfig config;
      config.material = preset.material;
      config.interior.base.style = preset.base;
      config.interior.pattern.family = preset.pattern;
      config.interior.pattern.density = preset.pattern == TerrainSubstratePattern::kNone ? 0 : 2;
      config.interior.details.family = preset.details;
      config.interior.details.density = preset.details == TerrainDetailSet::kNone ? 0 : 2;
      config.interior.details.accent_mode = DefaultAccentModeFor(preset.details);
      if (preset.material.name == "Snow") {
        config.surface.edge_detail = TerrainEdgeDetailConfig{
            .family = TerrainEdgeDetailSet::kSnowLip,
            .amount = 0.92f,
            .length = 4,
            .clump_size = 7,
            .highlight = 0.65f,
        };
      } else if (preset.material.name == "Cave") {
        config.surface.edge_detail = TerrainEdgeDetailConfig{
            .family = TerrainEdgeDetailSet::kMossFringe,
            .amount = 0.58f,
            .length = 5,
            .clump_size = 6,
            .lean = -0.08f,
            .highlight = 0.25f,
        };
      }
      presets.push_back({preset.material.name, config});
    }
    return presets;
  }();
  return kPresets;
}

absl::StatusOr<ResolvedTerrainStyle> ResolveTerrainStyle(const TerrainGenConfig& config) {
  if (config.tile_size <= 0 || config.supersample < 1 || config.variant_period < 1) {
    return absl::InvalidArgumentError("tile size, quality and repeat period must be positive");
  }
  if (config.tile_size > kMaxTileSize || config.supersample > kMaxSupersample ||
      config.variant_period > kMaxVariantPeriod) {
    return absl::InvalidArgumentError(absl::StrCat("terrain exceeds authoring limits (tile ",
                                                   kMaxTileSize, ", quality ", kMaxSupersample,
                                                   ", repeat ", kMaxVariantPeriod, ")"));
  }
  const int64_t resolution = static_cast<int64_t>(config.tile_size) * config.supersample;
  const int64_t field_edge = resolution * config.variant_period;
  if (field_edge > kMaxFieldEdge) {
    return absl::InvalidArgumentError(absl::StrCat("terrain repeat field edge exceeds ",
                                                   kMaxFieldEdge, " pixels; got ", field_edge));
  }
  const TerrainSurfaceConfig& surface = config.surface;
  if (!Finite(surface.top_depth) || surface.top_depth <= 0.0f || !Finite(surface.side_depth) ||
      surface.side_depth < 0.0f || !Finite(surface.underside_depth) ||
      surface.underside_depth < 0.0f || surface.top_depth > kMaxReferenceMeasurement ||
      surface.side_depth > kMaxReferenceMeasurement ||
      surface.underside_depth > kMaxReferenceMeasurement || !Finite(surface.ruffle_amplitude) ||
      surface.ruffle_amplitude < 0.0f || surface.ruffle_amplitude > kMaxReferenceMeasurement ||
      !Finite(surface.ruffle_density) || surface.ruffle_density <= 0.0f ||
      surface.ruffle_density > 64.0f || !Finite(surface.ruffle_sharpness) ||
      surface.ruffle_sharpness <= 0.0f || surface.ruffle_sharpness > 16.0f ||
      surface.ruffle_octaves < 1 || surface.ruffle_octaves > 8) {
    return absl::InvalidArgumentError("terrain surface-band settings are invalid");
  }
  if (surface.outline_depth < 0 || surface.highlight_depth < 0 || surface.shade_depth < 0 ||
      surface.contact_depth < 0 || surface.wall_depth < 0 ||
      surface.outline_depth > kMaxReferenceMeasurement ||
      surface.highlight_depth > kMaxReferenceMeasurement ||
      surface.shade_depth > kMaxReferenceMeasurement ||
      surface.contact_depth > kMaxReferenceMeasurement ||
      surface.wall_depth > kMaxReferenceMeasurement) {
    return absl::InvalidArgumentError("terrain layer depths cannot be negative");
  }
  if (!Finite(surface.wall_darkness) || surface.wall_darkness < 0.0f ||
      surface.wall_darkness > 4.0f) {
    return absl::InvalidArgumentError("terrain wall darkness must be finite and in [0, 4]");
  }
  if (!Finite(surface.texture_size) || surface.texture_size <= 0.0f ||
      !Finite(config.interior.base.feature_size) || config.interior.base.feature_size <= 0.0f ||
      surface.texture_size > kMaxReferenceMeasurement ||
      config.interior.base.feature_size > kMaxReferenceMeasurement ||
      !Finite(config.interior.base.mottle_density) || config.interior.base.mottle_density <= 0.0f ||
      config.interior.base.mottle_density > 64.0f) {
    return absl::InvalidArgumentError("terrain texture feature sizes must be positive and finite");
  }
  if (!Finite(surface.texture_amount) || surface.texture_amount < 0.0f ||
      surface.texture_amount > 1.0f || !Finite(config.interior.base.mottle_coverage) ||
      config.interior.base.mottle_coverage < 0.0f || config.interior.base.mottle_coverage > 1.0f ||
      !Finite(config.interior.base.relief) || config.interior.base.relief < 0.0f ||
      config.interior.base.relief > 1.0f || !Finite(config.interior.pattern.contrast) ||
      config.interior.pattern.contrast < 0.0f || config.interior.pattern.contrast > 1.0f) {
    return absl::InvalidArgumentError("terrain texture amounts must be finite and in [0, 1]");
  }
  const TerrainEdgeDetailConfig& edge = surface.edge_detail;
  if (!Finite(edge.amount) || edge.amount < 0.0f || edge.amount > 1.0f || edge.length < 0 ||
      edge.length > kMaxReferenceMeasurement || edge.clump_size < 1 ||
      edge.clump_size > kMaxReferenceMeasurement || !Finite(edge.lean) || edge.lean < -1.0f ||
      edge.lean > 1.0f || !Finite(edge.highlight) || edge.highlight < 0.0f ||
      edge.highlight > 1.0f) {
    return absl::InvalidArgumentError("terrain edge-detail settings are invalid");
  }
  if (!Finite(config.material.hue_shift) || std::abs(config.material.hue_shift) > 1.0f ||
      !Finite(config.material.contrast) || config.material.contrast <= 0.0f ||
      config.material.contrast > 8.0f) {
    return absl::InvalidArgumentError("terrain material colour adjustments are invalid");
  }
  const auto placement_settings_valid = [](int density, int spacing, int margin) {
    return density >= 0 && spacing >= 1 && margin >= 0;
  };
  if (!placement_settings_valid(config.interior.pattern.density, config.interior.pattern.spacing,
                                config.interior.pattern.margin) ||
      !placement_settings_valid(config.interior.details.density, config.interior.details.spacing,
                                config.interior.details.margin)) {
    return absl::InvalidArgumentError("terrain pattern/detail placement settings are invalid");
  }
  if (config.interior.pattern.scale < 1 || config.interior.pattern.scale > kMaxMotifScale ||
      config.interior.details.scale < 1 || config.interior.details.scale > kMaxMotifScale) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain motif size must be between 1 and ", kMaxMotifScale));
  }
  const int64_t phase_count = static_cast<int64_t>(config.variant_period) * config.variant_period;
  const int64_t pattern_count = config.interior.pattern.density * phase_count;
  const int64_t detail_count = config.interior.details.density * phase_count;
  if (pattern_count > kMaxMotifPlacements || detail_count > kMaxMotifPlacements) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain pattern/detail placement count exceeds ", kMaxMotifPlacements));
  }
  if (!Known(config.material.surface_style) || !Known(surface.edge_detail.family) ||
      !Known(config.interior.base.style) || !Known(config.interior.pattern.family) ||
      !Known(config.interior.details.family) || !Known(config.interior.pattern.accent_mode) ||
      !Known(config.interior.details.accent_mode)) {
    return absl::InvalidArgumentError("terrain configuration contains an unknown style");
  }

  ResolvedTerrainStyle style;
  switch (config.pixel_profile) {
    case TerrainPixelProfile::kChunky16:
      style.reference_tile_size = 16;
      style.compact_palette = true;
      break;
    case TerrainPixelProfile::kBalanced32:
      style.reference_tile_size = 32;
      break;
    case TerrainPixelProfile::kDetailed64:
      style.reference_tile_size = 64;
      break;
    default:
      return absl::InvalidArgumentError("unknown terrain pixel profile");
  }

  style.scale = static_cast<float>(config.tile_size) / style.reference_tile_size;
  style.surface_top_depth = surface.top_depth * style.scale;
  style.surface_side_depth = surface.side_depth * style.scale;
  style.surface_underside_depth = surface.underside_depth * style.scale;
  style.ruffle_amplitude = surface.ruffle_amplitude * style.scale;
  style.outline_depth = ScaleLayer(surface.outline_depth, style.scale);
  style.highlight_depth = ScaleLayer(surface.highlight_depth, style.scale);
  style.shade_depth = ScaleLayer(surface.shade_depth, style.scale);
  style.contact_depth = ScaleLayer(surface.contact_depth, style.scale);
  style.wall_depth = ScaleLayer(surface.wall_depth, style.scale);
  style.surface_texture_size =
      std::max(1, static_cast<int>(std::lround(surface.texture_size * style.scale)));
  const int final_period = config.tile_size * config.variant_period;
  style.surface_pattern_cells =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(final_period) /
                                               static_cast<float>(style.surface_texture_size))));
  style.edge_detail_length = ScaleLayer(edge.length, style.scale);
  const int edge_clump_size = ScaleLayer(edge.clump_size, style.scale);
  style.edge_pattern_cells =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(final_period) /
                                               static_cast<float>(edge_clump_size))));
  style.interior_feature_size =
      std::max(2, static_cast<int>(std::lround(config.interior.base.feature_size * style.scale)));
  style.interior_cells =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(final_period) /
                                               static_cast<float>(style.interior_feature_size))));
  style.pattern_scale = config.interior.pattern.scale;
  style.detail_scale = config.interior.details.scale;
  // Spacing is centre to centre, so magnified stamps have to spread out by the
  // same factor to keep the gaps between them. Without this, raising the size
  // silently collapses a pattern into a blob and leaves the author to discover
  // the coupling. At the default size of 1 this is the identity.
  style.pattern_spacing =
      ScaleLayer(config.interior.pattern.spacing, style.scale) * style.pattern_scale;
  style.pattern_margin = ScaleLayer(config.interior.pattern.margin, style.scale);
  style.detail_spacing =
      ScaleLayer(config.interior.details.spacing, style.scale) * style.detail_scale;
  style.detail_margin = ScaleLayer(config.interior.details.margin, style.scale);
  return style;
}

}  // namespace zebes
