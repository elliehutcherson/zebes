"""Orthographic projection from the solved rig to screen-space primitives.

Orthographic, not perspective: a sprite atlas needs a limb to measure the same
whether it is beside the body or reaching toward the camera, and perspective
foreshortening would make the proportion check meaningless.

The scale and ground line come from the measurement set, never from a frame's
own bounding box. Every frame in a set therefore shares one origin and one
contact line, which is the registration contract the atlas processor expects.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from .math3d import Mat4, multiply, origin_of, rotation_y, transform_point, translation
from .measurements import Proportions
from .skeleton import Capsule, Ellipsoid, Rig

# Named camera yaws, in degrees of world rotation about +Y. The character faces
# +Z at yaw 0, so positive yaw swings their right side toward the camera.
VIEWS: dict[str, float] = {
    "front": 0.0,
    "back": 180.0,
    "right": 90.0,
    "left": -90.0,
    "three-quarter-right": 35.0,
    "three-quarter-left": -35.0,
}


@dataclass(frozen=True)
class Layout:
    """Shared framing for a whole render set."""

    width: int
    height: int
    pixels_per_head: float
    origin_x: float
    ground_y: float

    def to_screen(self, x: float, y: float) -> tuple[float, float]:
        return (
            self.origin_x + x * self.pixels_per_head,
            self.ground_y - y * self.pixels_per_head,
        )


def layout_for(
    p: Proportions, width: int, height: int, headroom: float = 0.08
) -> Layout:
    """Frame the figure by its stated height with `headroom` above and below.

    A pose that lifts the figure above its standing height, a jump for example,
    is allowed to use the headroom; it is not allowed to change the scale.
    """
    usable = height * (1.0 - 2.0 * headroom)
    return Layout(
        width=width,
        height=height,
        pixels_per_head=usable / p.heads_tall,
        origin_x=width / 2.0,
        ground_y=height * (1.0 - headroom),
    )


# Every projected field, depths included, is in pixels. Mixing head units into
# the depth while radii were in pixels once made the depth buffer encode limb
# thickness instead of front-to-back position, which is silently wrong rather
# than visibly broken: the map still looks like a figure.
@dataclass(frozen=True)
class ProjectedCapsule:
    ax: float
    ay: float
    ar: float
    a_depth: float
    a_depth_r: float
    bx: float
    by: float
    br: float
    b_depth: float
    b_depth_r: float
    region: str
    name: str

    @property
    def sort_depth(self) -> float:
        return (self.a_depth + self.b_depth) / 2.0


@dataclass(frozen=True)
class ProjectedEllipse:
    cx: float
    cy: float
    rx: float
    ry: float
    depth: float
    depth_r: float
    region: str
    name: str

    @property
    def sort_depth(self) -> float:
        return self.depth


Projected = ProjectedCapsule | ProjectedEllipse


def _cross_section(rx: float, rz: float, yaw_degrees: float) -> tuple[float, float]:
    """Screen half-width and depth half-extent of an elliptical cross-section.

    The ellipse is treated as axis-aligned in the body frame. That is exact for
    the torso, which stays upright, and irrelevant for limbs, whose sections are
    circular.
    """
    c = math.cos(math.radians(yaw_degrees))
    s = math.sin(math.radians(yaw_degrees))
    screen = math.hypot(rx * c, rz * s)
    depth = math.hypot(rx * s, rz * c)
    return screen, depth


def project(
    rig: Rig,
    world: dict[str, Mat4],
    view: str,
    layout: Layout,
) -> list[Projected]:
    """Project every volume, sorted back to front for painter's-order drawing."""
    if view not in VIEWS:
        raise KeyError(f"unknown view {view!r}; available: {sorted(VIEWS)}")

    yaw = VIEWS[view]
    camera = rotation_y(yaw)
    ppu = layout.pixels_per_head

    out: list[Projected] = []
    for volume in rig.volumes:
        if isinstance(volume, Capsule):
            a = transform_point(camera, origin_of(world[volume.joint_a]))
            b = transform_point(camera, origin_of(world[volume.joint_b]))
            ar, a_depth_r = _cross_section(volume.rx_a, volume.rz_a, yaw)
            br, b_depth_r = _cross_section(volume.rx_b, volume.rz_b, yaw)
            ax, ay = layout.to_screen(a[0], a[1])
            bx, by = layout.to_screen(b[0], b[1])
            out.append(
                ProjectedCapsule(
                    ax=ax,
                    ay=ay,
                    ar=ar * ppu,
                    a_depth=a[2] * ppu,
                    a_depth_r=a_depth_r * ppu,
                    bx=bx,
                    by=by,
                    br=br * ppu,
                    b_depth=b[2] * ppu,
                    b_depth_r=b_depth_r * ppu,
                    region=volume.region,
                    name=volume.name,
                )
            )
            continue

        center_world = origin_of(
            multiply(world[volume.joint], translation(volume.center))
        )
        c = transform_point(camera, center_world)
        rx, depth_r = _cross_section(volume.rx, volume.rz, yaw)
        cx, cy = layout.to_screen(c[0], c[1])
        out.append(
            ProjectedEllipse(
                cx=cx,
                cy=cy,
                rx=rx * ppu,
                ry=volume.ry * ppu,
                depth=c[2] * ppu,
                depth_r=depth_r * ppu,
                region=volume.region,
                name=volume.name,
            )
        )

    out.sort(key=lambda item: item.sort_depth)
    return out


def project_joints(
    world: dict[str, Mat4], view: str, layout: Layout
) -> dict[str, tuple[float, float, float]]:
    """Screen x, screen y, and camera depth for every joint."""
    camera = rotation_y(VIEWS[view])
    result: dict[str, tuple[float, float, float]] = {}
    for name, matrix in world.items():
        p = transform_point(camera, origin_of(matrix))
        sx, sy = layout.to_screen(p[0], p[1])
        result[name] = (sx, sy, p[2])
    return result
