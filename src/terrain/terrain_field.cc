#include "terrain/terrain_field.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

// Stands in for infinity in the distance transform. Squared distances on any
// canvas the generator builds are many orders of magnitude below this, and a
// finite sentinel keeps the parabola intersections free of NaN.
constexpr float kUnreachable = 1e18f;

// Diagonal frequencies read as weaker than axis-aligned ones at the same
// amplitude, so they are damped to keep the field from looking like a grid.
constexpr float kDiagonalWeight = 0.6f;

constexpr float kTwoPi = 6.283185307179586f;

int Wrap(int value, int period) { return ((value % period) + period) % period; }

uint64_t MixBits(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

float CellRandom(int x, int y, uint64_t seed) {
  const uint64_t mixed = MixBits(seed ^ (static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ULL) ^
                                 (static_cast<uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL));
  return static_cast<float>(mixed & 0xffffff) / static_cast<float>(0xffffff);
}

// How many whole repeats to fit across the period.
//
// The obvious answer -- density rounded to the nearest whole number of repeats
// per tile -- makes every frequency a multiple of the tile count, which means
// the field silently collapses back to a one-tile period no matter how large a
// period was asked for. Choosing a count coprime with the tile count instead
// keeps the requested density while guaranteeing the pattern really does take
// the full period to come back around.
int ChooseFrequency(float density, int tiles_per_period) {
  const int target =
      std::max(1, static_cast<int>(std::lround(density * static_cast<float>(tiles_per_period))));
  if (tiles_per_period <= 1) return target;

  for (int offset = 0; offset <= tiles_per_period; ++offset) {
    for (const int candidate : {target - offset, target + offset}) {
      if (candidate >= 1 && std::gcd(candidate, tiles_per_period) == 1) return candidate;
    }
  }
  return target + 1;
}

// One row of the squared distance transform: the lower envelope of the
// parabolas rooted at each sample. This is the whole Felzenszwalb algorithm;
// the 2D transform is this run over columns and then over rows.
void TransformRow(const std::vector<float>& source, std::vector<float>& target,
                  std::vector<int>& hull, std::vector<float>& boundary) {
  const int count = static_cast<int>(source.size());
  int k = 0;
  hull[0] = 0;
  boundary[0] = -kUnreachable;
  boundary[1] = kUnreachable;

  for (int q = 1; q < count; ++q) {
    float intersection = ((source[q] + static_cast<float>(q) * q) -
                          (source[hull[k]] + static_cast<float>(hull[k]) * hull[k])) /
                         (2.0f * static_cast<float>(q - hull[k]));
    while (k > 0 && intersection <= boundary[k]) {
      --k;
      intersection = ((source[q] + static_cast<float>(q) * q) -
                      (source[hull[k]] + static_cast<float>(hull[k]) * hull[k])) /
                     (2.0f * static_cast<float>(q - hull[k]));
    }
    ++k;
    hull[k] = q;
    boundary[k] = intersection;
    boundary[k + 1] = kUnreachable;
  }

  k = 0;
  for (int q = 0; q < count; ++q) {
    while (boundary[k + 1] < static_cast<float>(q)) ++k;
    const float offset = static_cast<float>(q - hull[k]);
    target[q] = offset * offset + source[hull[k]];
  }
}

}  // namespace

std::vector<float> SquaredDistanceTransform(const std::vector<uint8_t>& solid, int width,
                                            int height) {
  std::vector<float> distance(static_cast<size_t>(width) * height, 0.0f);
  if (width <= 0 || height <= 0) return distance;

  for (size_t i = 0; i < distance.size(); ++i) {
    distance[i] = solid[i] != 0 ? kUnreachable : 0.0f;
  }

  const int longest = std::max(width, height);
  std::vector<float> source(longest);
  std::vector<float> target(longest);
  std::vector<int> hull(longest);
  std::vector<float> boundary(longest + 1);

  for (int x = 0; x < width; ++x) {
    source.resize(height);
    target.resize(height);
    for (int y = 0; y < height; ++y) source[y] = distance[static_cast<size_t>(y) * width + x];
    TransformRow(source, target, hull, boundary);
    for (int y = 0; y < height; ++y) distance[static_cast<size_t>(y) * width + x] = target[y];
  }

  for (int y = 0; y < height; ++y) {
    source.resize(width);
    target.resize(width);
    float* row = distance.data() + static_cast<size_t>(y) * width;
    std::copy(row, row + width, source.begin());
    TransformRow(source, target, hull, boundary);
    std::copy(target.begin(), target.begin() + width, row);
  }

  return distance;
}

absl::StatusOr<RuffleField> RuffleField::Create(int period_px, int tile_px, float density,
                                                float sharpness, int octaves, uint64_t seed) {
  if (period_px <= 0 || tile_px <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "ruffle field needs positive dimensions; got period ", period_px, " and tile ", tile_px));
  }
  if (period_px % tile_px != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "ruffle period ", period_px, " must be a whole number of ", tile_px, "px tiles"));
  }
  if (!std::isfinite(density) || density <= 0.0f || !std::isfinite(sharpness) ||
      sharpness <= 0.0f) {
    return absl::InvalidArgumentError("ruffle density and sharpness must be positive and finite");
  }
  if (octaves < 1 || octaves > 16) {
    return absl::InvalidArgumentError(
        absl::StrCat("ruffle field needs at least one octave; got ", octaves));
  }

  RuffleField field;
  field.period_px_ = period_px;
  field.values_.assign(static_cast<size_t>(period_px) * period_px, 0.0f);

  // Frequencies count whole repeats across the period, so every term below is
  // exactly periodic and the field wraps seamlessly by construction.
  const int tiles_per_period = period_px / tile_px;
  const int base = ChooseFrequency(density, tiles_per_period);

  std::mt19937_64 generator(seed);
  std::uniform_real_distribution<float> phases(0.0f, kTwoPi);

  float weight = 0.0f;
  for (int octave = 0; octave < octaves; ++octave) {
    const int frequency = base * (1 << octave);
    const float amplitude = 1.0f / static_cast<float>(1 << octave);
    const int directions[4][2] = {
        {frequency, 0}, {0, frequency}, {frequency, frequency}, {frequency, -frequency}};

    for (const int* direction : directions) {
      const float phase = phases(generator);
      const float weighting = (direction[0] == 0 || direction[1] == 0) ? 1.0f : kDiagonalWeight;
      weight += amplitude * weighting;

      for (int y = 0; y < period_px; ++y) {
        for (int x = 0; x < period_px; ++x) {
          const float u = static_cast<float>(x) / static_cast<float>(period_px);
          const float v = static_cast<float>(y) / static_cast<float>(period_px);
          field.values_[static_cast<size_t>(y) * period_px + x] +=
              amplitude * weighting *
              std::sin(kTwoPi * (direction[0] * u + direction[1] * v) + phase);
        }
      }
    }
  }

  for (float& value : field.values_) value /= weight;

  // Summing several directions pulls everything toward the mean, which washes
  // the ruffle out entirely. Stretch the middle of the distribution back to
  // full contrast, ignoring the tails so a single outlier cannot flatten it.
  std::vector<float> ordered = field.values_;
  std::sort(ordered.begin(), ordered.end());
  const size_t low_index = ordered.size() / 50;
  const size_t high_index = ordered.size() - 1 - low_index;
  const float low = ordered[low_index];
  const float span = std::max(1e-6f, ordered[high_index] - low);

  for (float& value : field.values_) {
    float stretched = std::clamp((value - low) / span, 0.0f, 1.0f) * 2.0f - 1.0f;
    if (sharpness != 1.0f) {
      stretched = std::copysign(std::pow(std::abs(stretched), 1.0f / std::max(1e-6f, sharpness)),
                                stretched);
    }
    value = 0.5f + 0.5f * stretched;
  }

  return field;
}

float RuffleField::Value(int x, int y) const {
  const int wrapped_x = Wrap(x, period_px_);
  const int wrapped_y = Wrap(y, period_px_);
  return values_[static_cast<size_t>(wrapped_y) * period_px_ + wrapped_x];
}

absl::StatusOr<ValueNoiseField> ValueNoiseField::Create(int period_px, int cells_per_period,
                                                        int octaves, uint64_t seed) {
  if (period_px <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("value noise needs a positive period; got ", period_px));
  }
  if (cells_per_period < 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("value noise needs at least one cell per period; got ", cells_per_period));
  }
  if (cells_per_period > period_px) {
    return absl::InvalidArgumentError("value noise cannot have more cells than pixels");
  }
  if (octaves < 1 || octaves > 16) {
    return absl::InvalidArgumentError(
        absl::StrCat("value noise needs at least one octave; got ", octaves));
  }

  ValueNoiseField field;
  field.period_px_ = period_px;
  field.values_.assign(static_cast<size_t>(period_px) * period_px, 0.0f);

  std::mt19937_64 generator(seed);
  std::uniform_real_distribution<float> amplitudes(0.0f, 1.0f);
  float weight = 0.0f;

  for (int octave = 0; octave < octaves; ++octave) {
    const int cells = cells_per_period * (1 << octave);
    const float amplitude = 1.0f / static_cast<float>(1 << octave);
    weight += amplitude;

    std::vector<float> lattice(static_cast<size_t>(cells) * cells);
    for (float& value : lattice) value = amplitudes(generator);

    for (int y = 0; y < period_px; ++y) {
      for (int x = 0; x < period_px; ++x) {
        const float u = static_cast<float>(x) * cells / static_cast<float>(period_px);
        const float v = static_cast<float>(y) * cells / static_cast<float>(period_px);
        const int x0 = static_cast<int>(std::floor(u));
        const int y0 = static_cast<int>(std::floor(v));
        // Indices wrap, which is the whole reason the field tiles.
        const int x1 = (x0 + 1) % cells;
        const int y1 = (y0 + 1) % cells;

        // Smoothstep rather than linear: linear interpolation leaves visible
        // creases along every lattice line.
        const float fx = u - static_cast<float>(x0);
        const float fy = v - static_cast<float>(y0);
        const float sx = fx * fx * (3.0f - 2.0f * fx);
        const float sy = fy * fy * (3.0f - 2.0f * fy);

        const float top =
            lattice[static_cast<size_t>(y0 % cells) * cells + (x0 % cells)] * (1.0f - sx) +
            lattice[static_cast<size_t>(y0 % cells) * cells + x1] * sx;
        const float bottom = lattice[static_cast<size_t>(y1) * cells + (x0 % cells)] * (1.0f - sx) +
                             lattice[static_cast<size_t>(y1) * cells + x1] * sx;

        field.values_[static_cast<size_t>(y) * period_px + x] +=
            amplitude * (top * (1.0f - sy) + bottom * sy);
      }
    }
  }

  for (float& value : field.values_) value /= weight;

  const auto [low, high] = std::minmax_element(field.values_.begin(), field.values_.end());
  const float span = std::max(1e-6f, *high - *low);
  const float floor_value = *low;
  for (float& value : field.values_) value = (value - floor_value) / span;

  return field;
}

float ValueNoiseField::Value(int x, int y) const {
  const int wrapped_x = ((x % period_px_) + period_px_) % period_px_;
  const int wrapped_y = ((y % period_px_) + period_px_) % period_px_;
  return values_[static_cast<size_t>(wrapped_y) * period_px_ + wrapped_x];
}

absl::StatusOr<PeriodicPatternGrid> PeriodicPatternGrid::Create(int period_px,
                                                                int cells_per_period) {
  if (period_px <= 0 || cells_per_period <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("periodic pattern grid needs positive period and cell count; got ", period_px,
                     " and ", cells_per_period));
  }
  if (cells_per_period > period_px) {
    return absl::InvalidArgumentError("periodic pattern cannot have more cells than pixels");
  }
  PeriodicPatternGrid grid;
  grid.period_px_ = period_px;
  grid.cells_per_period_ = cells_per_period;
  return grid;
}

int PeriodicPatternGrid::Cell(int coordinate) const {
  return Wrap(coordinate, period_px_) * cells_per_period_ / period_px_;
}

float PeriodicPatternGrid::Phase(int coordinate) const {
  const float position = static_cast<float>(Wrap(coordinate, period_px_)) * cells_per_period_ /
                         static_cast<float>(period_px_);
  return position - std::floor(position) - 0.5f;
}

absl::StatusOr<CellularField> CellularField::Create(int period_px, int cells_per_period,
                                                    uint64_t seed) {
  if (period_px <= 0 || cells_per_period <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("cellular field needs positive period and cell count; got ", period_px,
                     " and ", cells_per_period));
  }
  if (cells_per_period > period_px) {
    return absl::InvalidArgumentError("cellular field cannot have more cells than pixels");
  }

  CellularField field;
  field.period_px_ = period_px;
  field.boundary_distance_.resize(static_cast<size_t>(period_px) * period_px);
  const float cell_size = static_cast<float>(period_px) / cells_per_period;
  for (int y = 0; y < period_px; ++y) {
    for (int x = 0; x < period_px; ++x) {
      const int base_x = static_cast<int>(std::floor(static_cast<float>(x) / cell_size));
      const int base_y = static_cast<int>(std::floor(static_cast<float>(y) / cell_size));
      float nearest = kUnreachable;
      float second = kUnreachable;
      for (int cy = base_y - 1; cy <= base_y + 1; ++cy) {
        for (int cx = base_x - 1; cx <= base_x + 1; ++cx) {
          const int wrapped_x = Wrap(cx, cells_per_period);
          const int wrapped_y = Wrap(cy, cells_per_period);
          const float jitter_x = CellRandom(wrapped_x, wrapped_y, seed ^ 0x243f6a88);
          const float jitter_y = CellRandom(wrapped_x, wrapped_y, seed ^ 0xb7e15162);
          const float centre_x = (static_cast<float>(cx) + 0.25f + jitter_x * 0.5f) * cell_size;
          const float centre_y = (static_cast<float>(cy) + 0.25f + jitter_y * 0.5f) * cell_size;
          const float dx = static_cast<float>(x) - centre_x;
          const float dy = static_cast<float>(y) - centre_y;
          const float distance = dx * dx + dy * dy;
          if (distance < nearest) {
            second = nearest;
            nearest = distance;
          } else if (distance < second) {
            second = distance;
          }
        }
      }
      field.boundary_distance_[static_cast<size_t>(y) * period_px + x] = second - nearest;
    }
  }
  return field;
}

float CellularField::BoundaryDistance(int x, int y) const {
  return boundary_distance_[static_cast<size_t>(Wrap(y, period_px_)) * period_px_ +
                            Wrap(x, period_px_)];
}

}  // namespace zebes
