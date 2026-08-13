#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "objects/tile_shape_geometry.h"
#include "objects/tileset.h"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_field.h"
#include "terrain/terrain_motifs.h"
#include "terrain/terrain_style.h"

namespace zebes {

// Procedural terrain artwork.
//
// The whole generator rests on two ideas, and both are worth stating because
// they are why slopes and inner corners cost nothing extra here:
//
// 1. THE SURFACE IS A DISTANCE BAND, NOT AN EDGE. Every tile is rendered inside
//    a 3x3 block of its own neighbours. For each solid pixel we measure the
//    distance to the nearest empty one and call everything within some band of
//    that "surface". A distance field has no preferred axis, so a flat top, a
//    45-degree hypotenuse and a concave notch all get a correct band from
//    identical code.
//
// 2. THE BAND WIDTH IS A PERIODIC 2D FIELD, NOT A 1D EDGE PROFILE. Two adjacent
//    tiles sample that field at the same phase and therefore agree along their
//    shared border for free. See RuffleField in terrain_field.h.
//
// The outer silhouette is never displaced -- only the interior surface/interior
// boundary ruffles -- which is what guarantees tiles butt together exactly no
// matter how the pattern is randomised.

// Pixel profiles are rasterisation policies, not materials. The same meadow can
// therefore render as deliberately sparse 16px art or as a more textured 32px
// tile without either being a scaled copy of the other.
// Draws single tiles from one configuration, holding the periodic fields the
// whole set shares. Rendering tiles through separate renderers would give each
// one its own phase and tear every seam, so the sharing is not an optimisation.
class TerrainRenderer {
 public:
  static absl::StatusOr<TerrainRenderer> Create(TerrainGenConfig config);

  // Artwork for one blob-47 neighbourhood. mask must be normalized; variant
  // selects which phase of the pattern the tile is drawn at and must be less
  // than variant_count().
  absl::StatusOr<RgbaImage> RenderBlobTile(uint8_t mask, int variant) const;

  // Artwork for one slope or half-block unit. The neighbourhood is inferred
  // from the shape's own polygon, and the two-cell slope families additionally
  // see their partner's polygon, so the band runs unbroken across the seam
  // between the halves of a ramp.
  absl::StatusOr<RgbaImage> RenderShapeTile(TileShape shape, int variant) const;

  int variant_count() const { return config_.variant_period * config_.variant_period; }
  const TerrainGenConfig& config() const { return config_; }

 private:
  struct MotifPlacement {
    int x = 0;
    int y = 0;
    size_t motif = 0;
  };

  // Everything one interior motif layer needs in order to stamp itself. The
  // substrate pattern and the semantic details differ only in these values, so
  // they share the whole placement and wrapping path.
  struct MotifLayer {
    absl::Span<const TerrainMotif> stamps;
    absl::Span<const MotifPlacement> placements;
    int margin = 0;
    // Integer magnification. Source pixels are read at sx / scale, so a stamp
    // stays pixel art rather than being resampled.
    int scale = 1;
    TerrainAccentMode accent_mode = TerrainAccentMode::kMaterial;
    bool substrate_layer = false;
  };

  TerrainRenderer(TerrainGenConfig config, ResolvedTerrainStyle style, RuffleField ruffle,
                  ValueNoiseField surface_texture, ValueNoiseField mottle,
                  PeriodicPatternGrid surface_pattern, CellularField cellular,
                  std::vector<MotifPlacement> pattern_placements,
                  std::vector<MotifPlacement> detail_placements);

  // Rasterises the tile and its eight neighbours at supersampled resolution.
  std::vector<uint8_t> Occupancy(absl::Span<const TilePoint> polygon,
                                 absl::Span<const absl::Span<const TilePoint>> neighbors) const;

  // Band width per supersampled pixel, ruffled and biased by surface facing.
  std::vector<float> SurfaceBand(const std::vector<float>& depth, int origin_x, int origin_y) const;

  // Turns depth and band width into semantic pixel indices for the centre tile.
  std::vector<uint8_t> Classify(const std::vector<uint8_t>& occupancy,
                                const std::vector<float>& depth,
                                const std::vector<float>& band) const;

  void ApplySurfaceTexture(std::vector<uint8_t>& indices, int origin_x, int origin_y) const;
  void ApplyInteriorTexture(std::vector<uint8_t>& indices, int origin_x, int origin_y) const;
  void PlaceSubstratePattern(std::vector<uint8_t>& indices, int origin_x, int origin_y) const;
  void PlaceDetails(std::vector<uint8_t>& indices, int origin_x, int origin_y) const;
  void ApplyMotifs(std::vector<uint8_t>& indices, int origin_x, int origin_y,
                   const MotifLayer& layer) const;

  // Marks the pixels a motif may cover: interior pixels whose whole margin
  // neighbourhood is also interior. This is what keeps details off the surface
  // band instead of letting them spill over the edge.
  std::vector<uint8_t> LegalMotifPixels(const std::vector<uint8_t>& indices, int margin) const;

  // Draws one motif with its top-left corner at (x0, y0), or draws nothing if
  // any pixel it would cover is not clear interior. Placement is all or
  // nothing: a partially drawn motif reads as damage rather than as detail.
  void StampMotif(std::vector<uint8_t>& indices, const std::vector<uint8_t>& legal,
                  const TerrainMotif& stamp, int x0, int y0, const MotifLayer& layer) const;

  RgbaImage Colorize(const std::vector<uint8_t>& indices) const;

  RgbaImage RenderTile(absl::Span<const TilePoint> polygon,
                       absl::Span<const absl::Span<const TilePoint>> neighbors, int variant) const;

  TerrainGenConfig config_;
  ResolvedTerrainStyle style_;
  RuffleField ruffle_;
  ValueNoiseField surface_texture_;
  ValueNoiseField mottle_;
  PeriodicPatternGrid surface_pattern_;
  CellularField cellular_;
  std::vector<MotifPlacement> pattern_placements_;
  std::vector<MotifPlacement> detail_placements_;
  // Supersampled pixels per tile, and the 3x3 canvas edge that implies.
  int resolution_ = 0;
  int canvas_ = 0;
};

// Renders a complete atlas: all 47 masks for every variant, then one unit per
// slope TileShape, laid out exactly as ComposeBlob47 lays out hand-drawn art so
// that generated and composed atlases are interchangeable everywhere
// downstream.
absl::StatusOr<Blob47Atlas> GenerateBlob47Atlas(const TerrainGenConfig& config);

// Draws a fixed scene of painted ground: flat runs, steps, an overhang and an
// enclosed pocket, so every kind of edge and both concave and convex corners
// are on screen at once.
//
// Masks and variants are resolved exactly the way the level editor's brush
// resolves them, which is the point: a tuning preview that used its own rules
// could look right while the painted result did not. Cells outside the scene
// count as air so the silhouette is visible.
absl::StatusOr<RgbaImage> RenderTerrainPreviewScene(const TerrainRenderer& renderer);

}  // namespace zebes
