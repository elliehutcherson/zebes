#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"

namespace zebes {

// The two scalar fields terrain generation is built on. They are separated from
// the generator because both are numerical routines with exact properties worth
// asserting on their own: one is an exact transform with a brute-force
// reference, the other is periodic by construction and the whole seamlessness
// of a generated tileset rests on that periodicity holding.

// Euclidean distance from every solid pixel to the nearest empty one, squared.
//
// Felzenszwalb & Huttenlocher's two-pass algorithm: exact, and linear in the
// number of pixels rather than the naive quadratic. Empty pixels get 0. A grid
// with no empty pixel at all yields a saturated value everywhere, which is the
// answer callers want anyway -- such a cell is entirely interior.
//
// solid is row-major with width * height entries; nonzero means solid.
std::vector<float> SquaredDistanceTransform(const std::vector<uint8_t>& solid, int width,
                                            int height);

// A scalar field in [0,1] that is exactly periodic on both axes.
//
// The generator uses it to modulate how deep the surface band cuts into a tile.
// Periodicity is the point: two adjacent tiles sample the same field at the
// same phase, so their surface bands arrive at the shared border at the same
// depth and the seam disappears without any endpoint pinning or bookkeeping.
// That only works because every frequency summed here is an integer multiple of
// the period, so a hand-tuned "density" is rounded to one rather than used
// directly.
class RuffleField {
 public:
  // period_px is the edge length of one full repeat. density is roughly how
  // many bumps span one tile edge; sharpness below 1 rounds the bumps off,
  // above 1 makes them spiky; octaves adds detail at doubling frequencies.
  static absl::StatusOr<RuffleField> Create(int period_px, int tile_px, float density,
                                            float sharpness, int octaves, uint64_t seed);

  // Samples the field, wrapping both coordinates into the period. Callers pass
  // atlas-global pixel coordinates, which is what makes neighbouring tiles
  // agree.
  float Value(int x, int y) const;

  int period_px() const { return period_px_; }

 private:
  RuffleField() = default;

  int period_px_ = 0;
  std::vector<float> values_;
};

// A wrapping value-noise field in [0,1], summed over octaves.
//
// RuffleField is the wrong tool for texturing a broad area: a handful of pure
// sinusoids interfere into a visible lattice, which at close spacing reads as
// polka dots and at wide spacing as repeating blobs. Interpolating random
// lattice values has no harmonic structure to line up, so it reads as material
// instead of as a pattern.
//
// It wraps on period_px, so like RuffleField it can be sampled in atlas-global
// coordinates and neighbouring tiles will agree.
class ValueNoiseField {
 public:
  // cells_per_period is how many random values span the period on each axis;
  // each further octave doubles that and halves its contribution.
  static absl::StatusOr<ValueNoiseField> Create(int period_px, int cells_per_period, int octaves,
                                                uint64_t seed);

  float Value(int x, int y) const;

 private:
  ValueNoiseField() = default;

  int period_px_ = 0;
  std::vector<float> values_;
};

// A one-dimensional grid fitted exactly into a wrapping period. Feature sizes
// rarely divide the requested terrain period; fitting a whole number of cells
// prevents the final variant from resetting a motif at the wrong phase.
class PeriodicPatternGrid {
 public:
  static absl::StatusOr<PeriodicPatternGrid> Create(int period_px, int cells_per_period);

  // Cell is the containing cell index. Phase is the centred position within it
  // in [-0.5, 0.5), useful for drawing lobes and thin pixel lines.
  int Cell(int coordinate) const;
  float Phase(int coordinate) const;

  int period_px() const { return period_px_; }
  int cells_per_period() const { return cells_per_period_; }

 private:
  int period_px_ = 0;
  int cells_per_period_ = 0;
};

// A wrapping cellular field. BoundaryDistance is the difference between the
// squared distances to the two nearest jittered centres: small values are cell
// seams. It is precomputed because a Blob47 atlas samples the same material
// field hundreds of times through different masks.
class CellularField {
 public:
  static absl::StatusOr<CellularField> Create(int period_px, int cells_per_period, uint64_t seed);

  float BoundaryDistance(int x, int y) const;

 private:
  int period_px_ = 0;
  std::vector<float> boundary_distance_;
};

}  // namespace zebes
