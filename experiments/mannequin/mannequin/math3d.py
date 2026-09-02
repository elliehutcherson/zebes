"""Minimal 3D math for the mannequin rig.

Vectors are plain 3-tuples and transforms are row-major 4x4 tuples-of-tuples.
Nothing here depends on numpy: the experiment must run on a stdlib-only
interpreter.
"""

from __future__ import annotations

import math
from typing import Iterable

Vec3 = tuple[float, float, float]
Mat4 = tuple[
    tuple[float, float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float, float],
]

IDENTITY: Mat4 = (
    (1.0, 0.0, 0.0, 0.0),
    (0.0, 1.0, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
    (0.0, 0.0, 0.0, 1.0),
)


def translation(v: Vec3) -> Mat4:
    x, y, z = v
    return (
        (1.0, 0.0, 0.0, x),
        (0.0, 1.0, 0.0, y),
        (0.0, 0.0, 1.0, z),
        (0.0, 0.0, 0.0, 1.0),
    )


def rotation_x(degrees: float) -> Mat4:
    c = math.cos(math.radians(degrees))
    s = math.sin(math.radians(degrees))
    return (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, c, -s, 0.0),
        (0.0, s, c, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def rotation_y(degrees: float) -> Mat4:
    c = math.cos(math.radians(degrees))
    s = math.sin(math.radians(degrees))
    return (
        (c, 0.0, s, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (-s, 0.0, c, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def rotation_z(degrees: float) -> Mat4:
    c = math.cos(math.radians(degrees))
    s = math.sin(math.radians(degrees))
    return (
        (c, -s, 0.0, 0.0),
        (s, c, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def multiply(a: Mat4, b: Mat4) -> Mat4:
    return tuple(  # type: ignore[return-value]
        tuple(sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4))
        for r in range(4)
    )


def multiply_all(matrices: Iterable[Mat4]) -> Mat4:
    result = IDENTITY
    for m in matrices:
        result = multiply(result, m)
    return result


def transform_point(m: Mat4, p: Vec3) -> Vec3:
    x, y, z = p
    return (
        m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
        m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
        m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
    )


def origin_of(m: Mat4) -> Vec3:
    return (m[0][3], m[1][3], m[2][3])


def euler_xyz(rx: float, ry: float, rz: float) -> Mat4:
    """Intrinsic Z-then-X-then-Y is the convention artists expect for limbs.

    Swing (X) dominates limb motion, so it is applied before twist (Y) and the
    lateral spread (Z) that opens a shoulder or hip away from the body.
    """
    return multiply_all((rotation_y(ry), rotation_x(rx), rotation_z(rz)))
