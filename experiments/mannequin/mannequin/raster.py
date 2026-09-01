"""Depth-buffered rasteriser for projected mannequin volumes.

One pass fills a coverage mask, a depth buffer, and a region index per pixel.
Every conditioning image the generator might want is a cheap read of those three
buffers, so a silhouette, a depth map, and a part map are guaranteed to describe
the same figure rather than three separately drawn approximations.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from .png import write_rgba
from .project import Projected, ProjectedCapsule, ProjectedEllipse

REGION_COLORS: dict[str, tuple[int, int, int]] = {
    "head": (232, 106, 96),
    "hair": (150, 84, 168),
    "torso": (72, 132, 214),
    "arm": (86, 178, 124),
    "hand": (196, 214, 96),
    "leg": (232, 158, 74),
    "foot": (140, 108, 84),
    "helmet": (222, 88, 140),
    "armor": (108, 116, 138),
    "cape": (176, 62, 62),
    "weapon": (96, 200, 208),
}


@dataclass
class Buffers:
    width: int
    height: int
    covered: bytearray
    depth: list[float]
    region: list[str | None]

    def index(self, x: int, y: int) -> int:
        return y * self.width + x


def new_buffers(width: int, height: int) -> Buffers:
    count = width * height
    return Buffers(
        width=width,
        height=height,
        covered=bytearray(count),
        depth=[float("-inf")] * count,
        region=[None] * count,
    )


def _plot(buffers: Buffers, x: int, y: int, depth: float, region: str) -> None:
    i = buffers.index(x, y)
    buffers.covered[i] = 1
    if depth > buffers.depth[i]:
        buffers.depth[i] = depth
        buffers.region[i] = region


def _rasterize_capsule(buffers: Buffers, c: ProjectedCapsule) -> None:
    dx = c.bx - c.ax
    dy = c.by - c.ay
    length_squared = dx * dx + dy * dy
    max_r = max(c.ar, c.br)

    x0 = max(0, int(math.floor(min(c.ax, c.bx) - max_r)))
    x1 = min(buffers.width - 1, int(math.ceil(max(c.ax, c.bx) + max_r)))
    y0 = max(0, int(math.floor(min(c.ay, c.by) - max_r)))
    y1 = min(buffers.height - 1, int(math.ceil(max(c.ay, c.by) + max_r)))

    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            sx = px + 0.5 - c.ax
            sy = py + 0.5 - c.ay
            t = 0.0 if length_squared == 0.0 else (sx * dx + sy * dy) / length_squared
            t = min(1.0, max(0.0, t))
            radius = c.ar + (c.br - c.ar) * t
            if radius <= 0.0:
                continue
            distance = math.hypot(sx - dx * t, sy - dy * t)
            if distance > radius:
                continue
            bulge = math.sqrt(max(0.0, 1.0 - (distance / radius) ** 2))
            depth_r = c.a_depth_r + (c.b_depth_r - c.a_depth_r) * t
            axis_depth = c.a_depth + (c.b_depth - c.a_depth) * t
            _plot(buffers, px, py, axis_depth + depth_r * bulge, c.region)


def _rasterize_ellipse(buffers: Buffers, e: ProjectedEllipse) -> None:
    if e.rx <= 0.0 or e.ry <= 0.0:
        return

    x0 = max(0, int(math.floor(e.cx - e.rx)))
    x1 = min(buffers.width - 1, int(math.ceil(e.cx + e.rx)))
    y0 = max(0, int(math.floor(e.cy - e.ry)))
    y1 = min(buffers.height - 1, int(math.ceil(e.cy + e.ry)))

    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            nx = (px + 0.5 - e.cx) / e.rx
            ny = (py + 0.5 - e.cy) / e.ry
            falloff = nx * nx + ny * ny
            if falloff > 1.0:
                continue
            bulge = math.sqrt(max(0.0, 1.0 - falloff))
            _plot(buffers, px, py, e.depth + e.depth_r * bulge, e.region)


def rasterize(width: int, height: int, items: list[Projected]) -> Buffers:
    buffers = new_buffers(width, height)
    for item in items:
        if isinstance(item, ProjectedCapsule):
            _rasterize_capsule(buffers, item)
        else:
            _rasterize_ellipse(buffers, item)
    return buffers


def clipped_edges(buffers: Buffers) -> tuple[str, ...]:
    """Canvas edges the figure touches, and so may have been cut off by.

    Scale is fixed by the measurement set and shared across a whole set, so a
    wide pose cannot be shrunk to fit — a running stride simply needs a wider
    canvas. Silently cropping a foot would corrupt both the conditioning map and
    the drift signature that gets measured from it.
    """
    width, height = buffers.width, buffers.height
    touched: list[str] = []

    if any(buffers.covered[y * width] for y in range(height)):
        touched.append("left")
    if any(buffers.covered[y * width + width - 1] for y in range(height)):
        touched.append("right")
    if any(buffers.covered[x] for x in range(width)):
        touched.append("top")
    if any(buffers.covered[(height - 1) * width + x] for x in range(width)):
        touched.append("bottom")

    return tuple(touched)


def write_silhouette(path: Path, buffers: Buffers) -> None:
    """Flat black figure on transparent ground."""
    pixels = bytearray(buffers.width * buffers.height * 4)
    for i, covered in enumerate(buffers.covered):
        if covered:
            pixels[i * 4 + 3] = 255
    write_rgba(path, buffers.width, buffers.height, pixels)


def write_depth(path: Path, buffers: Buffers) -> None:
    """Grayscale depth, near white and far black, transparent off-figure.

    The range is normalised per image. That is right for a single reference and
    wrong for a frame set, where a shared range keeps a limb's brightness
    comparable between frames; `normalize_depth_range` supplies that.
    """
    covered_depths = [
        d for d, c in zip(buffers.depth, buffers.covered, strict=True) if c
    ]
    if not covered_depths:
        raise ValueError("cannot write a depth map for an empty figure")
    write_depth_with_range(path, buffers, min(covered_depths), max(covered_depths))


def write_depth_with_range(
    path: Path, buffers: Buffers, near: float, far: float
) -> None:
    span = far - near
    pixels = bytearray(buffers.width * buffers.height * 4)
    for i, covered in enumerate(buffers.covered):
        if not covered:
            continue
        t = 0.5 if span <= 0.0 else (buffers.depth[i] - near) / span
        value = int(round(min(1.0, max(0.0, t)) * 255))
        pixels[i * 4 : i * 4 + 4] = bytes((value, value, value, 255))
    write_rgba(path, buffers.width, buffers.height, pixels)


def depth_range(buffers: Buffers) -> tuple[float, float]:
    covered = [d for d, c in zip(buffers.depth, buffers.covered, strict=True) if c]
    if not covered:
        raise ValueError("figure is empty; nothing was rasterized")
    return min(covered), max(covered)


def write_regions(path: Path, buffers: Buffers) -> None:
    """Flat per-part colours, for telling a generator which band is which."""
    pixels = bytearray(buffers.width * buffers.height * 4)
    for i, region in enumerate(buffers.region):
        if region is None:
            continue
        if region not in REGION_COLORS:
            raise KeyError(f"no colour assigned to region {region!r}")
        r, g, b = REGION_COLORS[region]
        pixels[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))
    write_rgba(path, buffers.width, buffers.height, pixels)


def write_outline(path: Path, buffers: Buffers, thickness: int = 1) -> None:
    """Silhouette boundary only, the closest thing here to a line drawing."""
    width, height = buffers.width, buffers.height
    edge = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            i = y * width + x
            if not buffers.covered[i]:
                continue
            for ny in range(max(0, y - thickness), min(height, y + thickness + 1)):
                for nx in range(max(0, x - thickness), min(width, x + thickness + 1)):
                    if not buffers.covered[ny * width + nx]:
                        edge[i] = 1
                        break
                if edge[i]:
                    break

    pixels = bytearray(width * height * 4)
    for i, on in enumerate(edge):
        if on:
            pixels[i * 4 + 3] = 255
    write_rgba(path, width, height, pixels)
