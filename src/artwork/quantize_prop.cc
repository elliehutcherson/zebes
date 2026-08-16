#include "artwork/quantize_prop.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

struct Oklab {
  double lightness = 0.0;
  double green_red = 0.0;
  double blue_yellow = 0.0;
};

double SrgbToLinear(uint8_t channel) {
  const double value = channel / 255.0;
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

Oklab ToOklab(const RgbaColor& color) {
  const double red = SrgbToLinear(color.r);
  const double green = SrgbToLinear(color.g);
  const double blue = SrgbToLinear(color.b);
  const double l = std::cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue);
  const double m = std::cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue);
  const double s = std::cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue);
  return Oklab{
      .lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
      .green_red = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
      .blue_yellow = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
  };
}

double DistanceSquared(const Oklab& left, const Oklab& right) {
  const double lightness = left.lightness - right.lightness;
  const double green_red = left.green_red - right.green_red;
  const double blue_yellow = left.blue_yellow - right.blue_yellow;
  return lightness * lightness + green_red * green_red + blue_yellow * blue_yellow;
}

void AppendUnique(std::vector<RgbaColor>& colors, RgbaColor candidate) {
  candidate.a = 255;
  for (const RgbaColor& color : colors) {
    if (color == candidate) return;
  }
  colors.push_back(candidate);
}

std::vector<RgbaColor> SemanticSubset(const ResolvedTerrainPalette& terrain) {
  constexpr std::array<TerrainPaletteRole, 9> kRoles = {
      TerrainPaletteRole::kOutline,        TerrainPaletteRole::kInteriorHigh,
      TerrainPaletteRole::kInterior,       TerrainPaletteRole::kInteriorShade,
      TerrainPaletteRole::kWall,           TerrainPaletteRole::kBotanical,
      TerrainPaletteRole::kBotanicalShade, TerrainPaletteRole::kAccent0,
      TerrainPaletteRole::kAccent7,
  };
  std::vector<RgbaColor> colors;
  for (const TerrainPaletteRole role : kRoles) AppendUnique(colors, terrain.at(role));
  return colors;
}

}  // namespace

absl::StatusOr<PropPalette> BuildPropPalette(const ResolvedTerrainPalette& terrain,
                                             const TerrainMaterial& material,
                                             PropPalettePolicy policy) {
  PropPalette palette{
      .policy = policy,
      .outline = terrain.at(TerrainPaletteRole::kOutline),
  };
  switch (policy) {
    case PropPalettePolicy::kFullTerrain:
      palette.colors = terrain.OpaqueColors();
      break;
    case PropPalettePolicy::kSemanticSubset:
      palette.colors = SemanticSubset(terrain);
      break;
    case PropPalettePolicy::kDerivedRamps: {
      palette.colors = SemanticSubset(terrain);
      constexpr std::array<float, 4> kSteps = {-1.5f, -0.75f, 0.75f, 1.5f};
      constexpr std::array<uint32_t TerrainMaterial::*, 4> kBases = {
          &TerrainMaterial::substrate,
          &TerrainMaterial::surface,
          &TerrainMaterial::accent_primary,
          &TerrainMaterial::accent_secondary,
      };
      for (const uint32_t TerrainMaterial::* base : kBases) {
        for (const float step : kSteps) {
          AppendUnique(palette.colors, DeriveTerrainTone(material.*base, step * material.contrast,
                                                         material.hue_shift));
        }
      }
      break;
    }
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("unknown prop palette policy ", static_cast<int>(policy)));
  }
  if (palette.colors.empty()) return absl::FailedPreconditionError("prop palette is empty");
  return palette;
}

absl::StatusOr<PropArtwork> QuantizeProp(const PropArtwork& artwork, const PropPalette& palette) {
  if (!artwork.IsValid()) return absl::InvalidArgumentError("prop artwork is invalid");
  if (palette.colors.empty()) return absl::InvalidArgumentError("prop palette is empty");

  std::vector<Oklab> resolved;
  resolved.reserve(palette.colors.size());
  for (const RgbaColor& color : palette.colors) {
    if (color.a != 255) return absl::InvalidArgumentError("prop palette must be opaque");
    resolved.push_back(ToOklab(color));
  }

  PropArtwork quantized = artwork;
  const size_t pixel_count = static_cast<size_t>(quantized.image.width) * quantized.image.height;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * 4;
    if (quantized.image.pixels[offset + 3] == 0) {
      quantized.image.pixels[offset + 0] = 0;
      quantized.image.pixels[offset + 1] = 0;
      quantized.image.pixels[offset + 2] = 0;
      continue;
    }

    const Oklab source = ToOklab(RgbaColor{
        quantized.image.pixels[offset + 0],
        quantized.image.pixels[offset + 1],
        quantized.image.pixels[offset + 2],
        255,
    });
    size_t best = 0;
    double best_distance = std::numeric_limits<double>::infinity();
    for (size_t candidate = 0; candidate < resolved.size(); ++candidate) {
      const double distance = DistanceSquared(source, resolved[candidate]);
      if (distance < best_distance) {
        best = candidate;
        best_distance = distance;
      }
    }
    quantized.image.pixels[offset + 0] = palette.colors[best].r;
    quantized.image.pixels[offset + 1] = palette.colors[best].g;
    quantized.image.pixels[offset + 2] = palette.colors[best].b;
  }
  return quantized;
}

}  // namespace zebes
