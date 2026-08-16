#include "terrain/terrain_palette.h"

#include <algorithm>
#include <cmath>

#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr int kCompactAccentRampSteps = 4;

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
  hsv.h -= std::floor(hsv.h);
  return hsv;
}

RgbaColor ToRgba(const Hsv& hsv) {
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
  return RgbaColor{static_cast<uint8_t>(std::lround(r * 255.0f)),
                   static_cast<uint8_t>(std::lround(g * 255.0f)),
                   static_cast<uint8_t>(std::lround(b * 255.0f)), 255};
}

Hsv MixHsv(const Hsv& from, const Hsv& to, float amount) {
  float arc = to.h - from.h;
  arc -= std::floor(arc);
  if (arc > 0.5f) arc -= 1.0f;

  Hsv mixed;
  mixed.h = from.h + arc * amount;
  mixed.h -= std::floor(mixed.h);
  mixed.s = from.s + (to.s - from.s) * amount;
  mixed.v = from.v + (to.v - from.v) * amount;
  return mixed;
}

}  // namespace

namespace {

RgbaColor DeriveTerrainTone(uint32_t base, float step, float hue_shift,
                            float saturation_shift = 0.10f) {
  Hsv hsv = ToHsv(base);
  hsv.h += hue_shift * step;
  hsv.s = std::clamp(hsv.s + saturation_shift * step, 0.0f, 1.0f);
  hsv.v = std::clamp(hsv.v - 0.13f * step, 0.0f, 1.0f);
  return ToRgba(hsv);
}

}  // namespace

ResolvedTerrainPalette BuildTerrainPalette(const TerrainGenConfig& config,
                                           const ResolvedTerrainStyle& style) {
  ResolvedTerrainPalette resolved;
  auto& palette = resolved.colors;
  const TerrainMaterial& material = config.material;
  const float contrast = material.contrast;

  palette[static_cast<size_t>(TerrainPaletteRole::kEmpty)] = RgbaColor{0, 0, 0, 0};
  palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceHigh)] =
      DeriveTerrainTone(material.surface, -1.0f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kSurface)] =
      DeriveTerrainTone(material.surface, 0.0f, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceShade)] =
      DeriveTerrainTone(material.surface, 1.2f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kContact)] =
      DeriveTerrainTone(material.substrate, 1.9f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kInterior)] =
      DeriveTerrainTone(material.substrate, 0.0f, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kInteriorShade)] =
      DeriveTerrainTone(material.substrate, (style.compact_palette ? 0.9f : 0.5f) * contrast,
                        material.hue_shift * 0.3f);
  palette[static_cast<size_t>(TerrainPaletteRole::kInteriorHigh)] =
      DeriveTerrainTone(material.substrate, (style.compact_palette ? -0.65f : -0.35f) * contrast,
                        material.hue_shift * 0.2f);
  palette[static_cast<size_t>(TerrainPaletteRole::kOutline)] = ToRgba(ToHsv(material.outline));
  palette[static_cast<size_t>(TerrainPaletteRole::kPattern)] =
      DeriveTerrainTone(material.substrate, -0.85f * contrast * config.interior.pattern.contrast,
                        material.hue_shift * 0.3f);
  palette[static_cast<size_t>(TerrainPaletteRole::kPatternShade)] =
      DeriveTerrainTone(material.substrate, 0.75f * contrast * config.interior.pattern.contrast,
                        material.hue_shift * 0.3f);
  palette[static_cast<size_t>(TerrainPaletteRole::kDecor)] =
      DeriveTerrainTone(material.substrate, -0.85f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kDecorShade)] =
      DeriveTerrainTone(material.substrate, 0.75f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceTextureHigh)] =
      style.compact_palette
          ? palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceHigh)]
          : DeriveTerrainTone(material.surface, -0.55f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceTextureShade)] =
      style.compact_palette
          ? palette[static_cast<size_t>(TerrainPaletteRole::kSurfaceShade)]
          : DeriveTerrainTone(material.surface, 0.65f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kBotanical)] =
      DeriveTerrainTone(material.surface, -0.25f * contrast, material.hue_shift);
  palette[static_cast<size_t>(TerrainPaletteRole::kBotanicalShade)] =
      DeriveTerrainTone(material.surface, 0.9f * contrast, material.hue_shift);

  const Hsv accent_from = ToHsv(material.accent_primary);
  const Hsv accent_to = ToHsv(material.accent_secondary);
  const int distinct = style.compact_palette ? kCompactAccentRampSteps : kTerrainAccentRampSteps;
  const size_t accent_start = static_cast<size_t>(TerrainPaletteRole::kAccent0);
  for (int step = 0; step < kTerrainAccentRampSteps; ++step) {
    const int bucket = step * (distinct - 1) / (kTerrainAccentRampSteps - 1);
    const float amount = static_cast<float>(bucket) / static_cast<float>(distinct - 1);
    palette[accent_start + step] = ToRgba(MixHsv(accent_from, accent_to, amount));
  }

  const float wall_mix = 1.0f - std::exp(-config.surface.wall_darkness);
  palette[static_cast<size_t>(TerrainPaletteRole::kWall)] =
      ToRgba(MixHsv(ToHsv(material.substrate), ToHsv(material.outline), wall_mix));
  return resolved;
}

absl::StatusOr<ResolvedTerrainPalette> ResolveTerrainPalette(const TerrainGenConfig& config) {
  ASSIGN_OR_RETURN(const ResolvedTerrainStyle style, ResolveTerrainStyle(config));
  return BuildTerrainPalette(config, style);
}

std::vector<RgbaColor> ResolvedTerrainPalette::OpaqueColors() const {
  std::vector<RgbaColor> unique;
  for (const RgbaColor color : colors) {
    if (color.a == 0) continue;
    if (std::find(unique.begin(), unique.end(), color) == unique.end()) unique.push_back(color);
  }
  return unique;
}

}  // namespace zebes
