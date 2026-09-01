"""OpenPose COCO-18 keypoint export.

The rig's own joint set is richer than COCO-18 and differently named, so this
module is the translation layer. It matters that the rendered skeleton uses the
canonical OpenPose colours and limb order: ControlNet pose models were trained
on that exact palette, and a skeleton drawn in arbitrary colours is a picture of
a skeleton rather than a control signal.

Facial keypoints (nose, eyes, ears) have no joints in the rig. They are derived
from the head transform, which keeps them consistent with wherever the head is
looking instead of pinned to the canvas.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

from .math3d import Mat4, rotation_y, transform_point
from .measurements import Proportions
from .png import write_rgba
from .project import VIEWS, Layout

KEYPOINT_NAMES: tuple[str, ...] = (
    "nose",
    "neck",
    "right_shoulder",
    "right_elbow",
    "right_wrist",
    "left_shoulder",
    "left_elbow",
    "left_wrist",
    "right_hip",
    "right_knee",
    "right_ankle",
    "left_hip",
    "left_knee",
    "left_ankle",
    "right_eye",
    "left_eye",
    "right_ear",
    "left_ear",
)

# Rig joints backing each COCO index. None means the point is derived from the
# head frame instead; see `_facial_points`.
_JOINT_FOR_INDEX: tuple[str | None, ...] = (
    None,
    "neck",
    "shoulder_r",
    "elbow_r",
    "wrist_r",
    "shoulder_l",
    "elbow_l",
    "wrist_l",
    "hip_r",
    "knee_r",
    "ankle_r",
    "hip_l",
    "knee_l",
    "ankle_l",
    None,
    None,
    None,
    None,
)

KEYPOINT_COLORS: tuple[tuple[int, int, int], ...] = (
    (255, 0, 0),
    (255, 85, 0),
    (255, 170, 0),
    (255, 255, 0),
    (170, 255, 0),
    (85, 255, 0),
    (0, 255, 0),
    (0, 255, 85),
    (0, 255, 170),
    (0, 255, 255),
    (0, 170, 255),
    (0, 85, 255),
    (0, 0, 255),
    (85, 0, 255),
    (170, 0, 255),
    (255, 0, 255),
    (255, 0, 170),
    (255, 0, 85),
)

LIMBS: tuple[tuple[int, int], ...] = (
    (1, 2),
    (1, 5),
    (2, 3),
    (3, 4),
    (5, 6),
    (6, 7),
    (1, 8),
    (8, 9),
    (9, 10),
    (1, 11),
    (11, 12),
    (12, 13),
    (1, 0),
    (0, 14),
    (14, 16),
    (0, 15),
    (15, 17),
)


@dataclass(frozen=True)
class Keypoint:
    name: str
    x: float
    y: float
    depth: float


def _facial_points(
    p: Proportions, head: Mat4, camera: Mat4
) -> dict[int, tuple[float, float, float]]:
    """Nose, eyes, and ears in camera space, derived from the head frame."""
    forward = p.head_depth / 2.0
    side = p.head_width / 2.0
    local = {
        0: (0.0, 0.52, forward),
        14: (-side * 0.42, 0.60, forward * 0.78),
        15: (side * 0.42, 0.60, forward * 0.78),
        16: (-side * 0.92, 0.56, 0.0),
        17: (side * 0.92, 0.56, 0.0),
    }
    return {
        index: transform_point(camera, transform_point(head, offset))
        for index, offset in local.items()
    }


def keypoints(
    p: Proportions, world: dict[str, Mat4], view: str, layout: Layout
) -> tuple[Keypoint, ...]:
    if view not in VIEWS:
        raise KeyError(f"unknown view {view!r}; available: {sorted(VIEWS)}")

    camera = rotation_y(VIEWS[view])
    facial = _facial_points(p, world["head"], camera)

    out: list[Keypoint] = []
    for index, joint_name in enumerate(_JOINT_FOR_INDEX):
        if joint_name is None:
            point = facial[index]
        else:
            if joint_name not in world:
                raise KeyError(f"rig has no joint {joint_name!r} for COCO index {index}")
            matrix = world[joint_name]
            point = transform_point(
                camera, (matrix[0][3], matrix[1][3], matrix[2][3])
            )
        sx, sy = layout.to_screen(point[0], point[1])
        out.append(Keypoint(name=KEYPOINT_NAMES[index], x=sx, y=sy, depth=point[2]))
    return tuple(out)


def to_json(points: tuple[Keypoint, ...], layout: Layout) -> str:
    """Serialise in the shape ControlNet pose tooling expects.

    Coordinates are normalised to the canvas so a consumer can rescale without
    knowing the render size.
    """
    flat: list[float] = []
    for point in points:
        flat.extend(
            (point.x / layout.width, point.y / layout.height, 1.0)
        )
    return json.dumps(
        {
            "canvas_width": layout.width,
            "canvas_height": layout.height,
            "people": [{"pose_keypoints_2d": flat}],
        },
        indent=2,
    )


def _blend(
    pixels: bytearray, index: int, color: tuple[int, int, int], alpha: float
) -> None:
    base = index * 4
    existing = pixels[base + 3] / 255.0
    covered = min(1.0, alpha + existing * (1.0 - alpha))
    for channel in range(3):
        pixels[base + channel] = int(
            round(color[channel] * alpha + pixels[base + channel] * (1.0 - alpha))
        )
    pixels[base + 3] = int(round(covered * 255))


def _draw_capsule(
    pixels: bytearray,
    width: int,
    height: int,
    ax: float,
    ay: float,
    bx: float,
    by: float,
    radius: float,
    color: tuple[int, int, int],
    alpha: float,
) -> None:
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy

    x0 = max(0, int(math.floor(min(ax, bx) - radius)))
    x1 = min(width - 1, int(math.ceil(max(ax, bx) + radius)))
    y0 = max(0, int(math.floor(min(ay, by) - radius)))
    y1 = min(height - 1, int(math.ceil(max(ay, by) + radius)))

    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            sx = px + 0.5 - ax
            sy = py + 0.5 - ay
            t = 0.0 if length_squared == 0.0 else (sx * dx + sy * dy) / length_squared
            t = min(1.0, max(0.0, t))
            if math.hypot(sx - dx * t, sy - dy * t) <= radius:
                _blend(pixels, py * width + px, color, alpha)


def write_skeleton_png(
    path: Path, points: tuple[Keypoint, ...], layout: Layout
) -> None:
    """Render the canonical OpenPose skeleton on a black ground.

    Limbs are drawn at partial opacity and keypoints opaque on top, matching the
    reference renderer closely enough for a pose ControlNet to read it.
    """
    # Opaque black, not transparent. Pose ControlNet models were trained on
    # skeletons over black; a transparent ground composites to whatever the
    # consumer happens to use, and white inverts the signal entirely.
    pixels = bytearray(b"\x00\x00\x00\xff" * (layout.width * layout.height))
    limb_radius = max(1.5, layout.pixels_per_head * 0.055)
    joint_radius = max(1.5, layout.pixels_per_head * 0.062)

    for limb_index, (a, b) in enumerate(LIMBS):
        start, end = points[a], points[b]
        _draw_capsule(
            pixels,
            layout.width,
            layout.height,
            start.x,
            start.y,
            end.x,
            end.y,
            limb_radius,
            KEYPOINT_COLORS[limb_index],
            0.6,
        )

    for index, point in enumerate(points):
        _draw_capsule(
            pixels,
            layout.width,
            layout.height,
            point.x,
            point.y,
            point.x,
            point.y,
            joint_radius,
            KEYPOINT_COLORS[index],
            1.0,
        )

    write_rgba(path, layout.width, layout.height, pixels)
