"""Proportion drift measurement.

This is the part the failed pose-conditioned pilots did not have. Their only
gate was a person looking at two frames and writing that one was "visibly
chunkier and more stout" than the other. That verdict was correct but it cost
six provider turns to reach, and it cannot be applied to twelve frames without
someone staring at all of them.

A signature reduces a figure's coverage mask to numbers that survive a change of
resolution: every width is divided by the figure's own height, so a mannequin
rendered at 384 px and a generated frame at 1024 px compare directly. What the
signature deliberately does not carry is absolute size, position on the canvas,
or colour — none of which is proportion drift.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path

from .png import read_rgba

DEFAULT_BANDS = 24

# Alpha at or below this counts as background. Generated frames often carry a
# few stray low-alpha pixels around the subject after matting.
DEFAULT_ALPHA_THRESHOLD = 8


class MeasurementError(ValueError):
    """Raised when a figure cannot be measured."""


@dataclass(frozen=True)
class Signature:
    """A scale-invariant description of one figure's proportions."""

    bands: int
    height_px: int
    width_px: int
    aspect: float
    centroid_x: float
    centroid_y: float
    area_fraction: float
    band_widths: tuple[float, ...]

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2, sort_keys=True)

    @staticmethod
    def from_dict(data: dict) -> "Signature":
        return Signature(
            bands=int(data["bands"]),
            height_px=int(data["height_px"]),
            width_px=int(data["width_px"]),
            aspect=float(data["aspect"]),
            centroid_x=float(data["centroid_x"]),
            centroid_y=float(data["centroid_y"]),
            area_fraction=float(data["area_fraction"]),
            band_widths=tuple(float(v) for v in data["band_widths"]),
        )


def signature_from_mask(
    covered: bytes | bytearray, width: int, height: int, bands: int = DEFAULT_BANDS
) -> Signature:
    """Reduce a coverage mask to a signature.

    `covered` is one byte per pixel, non-zero where the figure is.
    """
    if bands < 4:
        raise MeasurementError(f"bands must be at least 4, got {bands}")
    if len(covered) != width * height:
        raise MeasurementError(
            f"mask is {len(covered)} bytes, expected {width * height} "
            f"for {width}x{height}"
        )

    rows: list[tuple[int, int]] = []
    min_x, max_x = width, -1
    min_y, max_y = height, -1
    total = 0
    sum_x = 0
    sum_y = 0

    for y in range(height):
        row_start = y * width
        row_min, row_max = width, -1
        for x in range(width):
            if not covered[row_start + x]:
                continue
            total += 1
            sum_x += x
            sum_y += y
            if x < row_min:
                row_min = x
            if x > row_max:
                row_max = x
        rows.append((row_min, row_max))
        if row_max < 0:
            continue
        min_x = min(min_x, row_min)
        max_x = max(max_x, row_max)
        min_y = min(min_y, y)
        max_y = max(max_y, y)

    if total == 0:
        raise MeasurementError("figure is empty; nothing to measure")

    figure_height = max_y - min_y + 1
    figure_width = max_x - min_x + 1

    band_widths: list[float] = []
    for band in range(bands):
        top = min_y + (figure_height * band) // bands
        bottom = min_y + (figure_height * (band + 1)) // bands
        if bottom <= top:
            bottom = top + 1
        widest = 0
        for y in range(top, min(bottom, height)):
            row_min, row_max = rows[y]
            if row_max >= 0:
                widest = max(widest, row_max - row_min + 1)
        band_widths.append(widest / figure_height)

    return Signature(
        bands=bands,
        height_px=figure_height,
        width_px=figure_width,
        aspect=figure_width / figure_height,
        centroid_x=(sum_x / total - min_x) / figure_width,
        centroid_y=(sum_y / total - min_y) / figure_height,
        area_fraction=total / (figure_width * figure_height),
        band_widths=tuple(band_widths),
    )


def mask_from_png(
    path: Path, alpha_threshold: int = DEFAULT_ALPHA_THRESHOLD
) -> tuple[bytearray, int, int]:
    """Build a coverage mask from a PNG's alpha channel.

    A fully opaque image is rejected rather than guessed at. Measuring an
    un-isolated frame would compare the background's bounding box, not the
    character's, and quietly report that nothing drifted.
    """
    width, height, pixels = read_rgba(path)
    covered = bytearray(width * height)
    opaque = 0
    for i in range(width * height):
        alpha = pixels[i * 4 + 3]
        if alpha > alpha_threshold:
            covered[i] = 1
        if alpha == 255:
            opaque += 1

    if opaque == width * height:
        raise MeasurementError(
            f"{path} is fully opaque, so its alpha carries no subject. "
            "Isolate the subject before measuring."
        )
    return covered, width, height


def signature_from_png(
    path: Path,
    bands: int = DEFAULT_BANDS,
    alpha_threshold: int = DEFAULT_ALPHA_THRESHOLD,
) -> Signature:
    covered, width, height = mask_from_png(path, alpha_threshold)
    return signature_from_mask(covered, width, height, bands)


@dataclass(frozen=True)
class BandDeviation:
    band: int
    reference: float
    candidate: float

    @property
    def delta(self) -> float:
        return abs(self.candidate - self.reference)

    @property
    def relative(self) -> float:
        """Deviation as a fraction of the reference width.

        Falls back to absolute deviation where the reference band is empty, so
        a limb appearing out of nowhere still registers.
        """
        if self.reference <= 1e-9:
            return self.delta
        return self.delta / self.reference


@dataclass(frozen=True)
class Comparison:
    tolerance: float
    aspect_delta: float
    band_deviations: tuple[BandDeviation, ...]

    @property
    def failing(self) -> tuple[BandDeviation, ...]:
        return tuple(d for d in self.band_deviations if d.relative > self.tolerance)

    @property
    def worst(self) -> BandDeviation:
        return max(self.band_deviations, key=lambda d: d.relative)

    @property
    def passed(self) -> bool:
        return not self.failing and self.aspect_delta <= self.tolerance

    def report(self) -> str:
        lines = [
            f"{'PASS' if self.passed else 'FAIL'} "
            f"at tolerance {self.tolerance:.3f}",
            f"  aspect deviation {self.aspect_delta:.4f}",
            f"  worst band {self.worst.band} "
            f"({self.worst.reference:.4f} -> {self.worst.candidate:.4f}, "
            f"{self.worst.relative:.1%})",
        ]
        for deviation in self.failing:
            lines.append(
                f"  band {deviation.band:2d} FAIL "
                f"{deviation.reference:.4f} -> {deviation.candidate:.4f} "
                f"({deviation.relative:.1%})"
            )
        return "\n".join(lines)


def compare(reference: Signature, candidate: Signature, tolerance: float) -> Comparison:
    """Compare two signatures band by band.

    Both must use the same band count; comparing a 24-band profile against a
    12-band one would silently average away the difference being looked for.
    """
    if reference.bands != candidate.bands:
        raise MeasurementError(
            f"band counts differ: reference {reference.bands}, "
            f"candidate {candidate.bands}"
        )
    if tolerance <= 0.0:
        raise MeasurementError(f"tolerance must be positive, got {tolerance}")

    deviations = tuple(
        BandDeviation(band=i, reference=r, candidate=c)
        for i, (r, c) in enumerate(
            zip(reference.band_widths, candidate.band_widths, strict=True)
        )
    )
    return Comparison(
        tolerance=tolerance,
        aspect_delta=abs(candidate.aspect - reference.aspect) / reference.aspect,
        band_deviations=deviations,
    )


def band_landmarks(heads_tall: float, bands: int = DEFAULT_BANDS) -> dict[str, int]:
    """Band indices for the landmarks that failed review before.

    Band 0 is the crown. The helmet band is where a helmet's width shows, and
    the shoulder band is the widest point of an upright figure; those two plus
    the waist are where the rejected pilots diverged.
    """
    def band_at(head_units_from_crown: float) -> int:
        fraction = head_units_from_crown / heads_tall
        return min(bands - 1, max(0, int(fraction * bands)))

    return {
        "crown": 0,
        "helmet": band_at(0.4),
        "chin": band_at(1.0),
        "shoulder": band_at(1.35),
        "waist": band_at(2.2),
        "hip": band_at(2.8),
    }
