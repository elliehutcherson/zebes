"""Vector output.

SVG is the artist-facing format: it opens in any drawing tool as an underlay,
stays crisp at whatever resolution the generator wants, and can be edited by
hand when a pose needs a nudge the parameter set cannot express.

Three modes serve three different readers. `construction` is the classic
artist's mannequin, with visible volumes and centrelines. `silhouette` is the
flat shape a generator should fill. `lineart` is the outline a control map or a
tracing pass wants.
"""

from __future__ import annotations

import math
from pathlib import Path

from .project import Layout, Projected, ProjectedCapsule, ProjectedEllipse

MODES = ("construction", "silhouette", "lineart")


def _capsule_body(c: ProjectedCapsule) -> str:
    """Circles at both ends plus the quad between their perpendicular offsets.

    The quad's edges are perpendicular offsets rather than true external
    tangents. On a taper as gentle as a limb the difference is well under a
    pixel at the resolutions used here.
    """
    dx, dy = c.bx - c.ax, c.by - c.ay
    length = math.hypot(dx, dy)
    if length < 1e-6:
        return f'<circle cx="{c.ax:.2f}" cy="{c.ay:.2f}" r="{max(c.ar, c.br):.2f}"/>'

    nx, ny = -dy / length, dx / length
    points = " ".join(
        f"{x:.2f},{y:.2f}"
        for x, y in (
            (c.ax + nx * c.ar, c.ay + ny * c.ar),
            (c.bx + nx * c.br, c.by + ny * c.br),
            (c.bx - nx * c.br, c.by - ny * c.br),
            (c.ax - nx * c.ar, c.ay - ny * c.ar),
        )
    )
    return (
        f'<circle cx="{c.ax:.2f}" cy="{c.ay:.2f}" r="{c.ar:.2f}"/>'
        f'<circle cx="{c.bx:.2f}" cy="{c.by:.2f}" r="{c.br:.2f}"/>'
        f'<polygon points="{points}"/>'
    )


def _ellipse_body(e: ProjectedEllipse) -> str:
    return (
        f'<ellipse cx="{e.cx:.2f}" cy="{e.cy:.2f}" '
        f'rx="{e.rx:.2f}" ry="{e.ry:.2f}"/>'
    )


def _shape(item: Projected) -> str:
    if isinstance(item, ProjectedCapsule):
        return _capsule_body(item)
    return _ellipse_body(item)


def _head_unit_grid(layout: Layout, heads_tall: float) -> str:
    lines = []
    for i in range(int(math.ceil(heads_tall)) + 1):
        y = layout.ground_y - i * layout.pixels_per_head
        if y < 0:
            break
        lines.append(
            f'<line x1="0" y1="{y:.2f}" x2="{layout.width}" y2="{y:.2f}"/>'
            f'<text x="4" y="{y - 3:.2f}">{i}</text>'
        )
    return (
        '<g class="grid">' + "".join(lines) + "</g>"
        if lines
        else ""
    )


def render(
    items: list[Projected],
    layout: Layout,
    heads_tall: float,
    mode: str = "construction",
    joints: dict[str, tuple[float, float, float]] | None = None,
    label: str = "",
    show_grid: bool = True,
) -> str:
    if mode not in MODES:
        raise ValueError(f"unknown mode {mode!r}; available: {list(MODES)}")

    body = "".join(_shape(item) for item in items)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{layout.width}" '
        f'height="{layout.height}" viewBox="0 0 {layout.width} {layout.height}">',
        "<style>"
        ".grid line{stroke:#c8d2e0;stroke-width:0.5;stroke-dasharray:3 3}"
        ".grid text{fill:#8f9bad;font:8px sans-serif}"
        ".ground{stroke:#d05a5a;stroke-width:1}"
        ".fill{fill:#111820}"
        ".shell{fill:#e8edf4;stroke:#26313f;stroke-width:1.1;"
        "stroke-linejoin:round;fill-opacity:0.72}"
        ".outline{fill:none;stroke:#111820;stroke-width:1.4}"
        ".bone{stroke:#c0392b;stroke-width:1.2}"
        ".joint{fill:#c0392b}"
        ".label{fill:#4a5567;font:10px sans-serif}"
        "</style>",
    ]

    if show_grid:
        parts.append(_head_unit_grid(layout, heads_tall))

    if mode == "silhouette":
        parts.append(f'<g class="fill">{body}</g>')
    elif mode == "lineart":
        parts.append(f'<g class="outline">{body}</g>')
    else:
        parts.append(f'<g class="shell">{body}</g>')

    if mode == "construction" and joints is not None:
        parts.append(_skeleton_overlay(joints))

    if show_grid:
        parts.append(
            f'<line class="ground" x1="0" y1="{layout.ground_y:.2f}" '
            f'x2="{layout.width}" y2="{layout.ground_y:.2f}"/>'
        )

    if label:
        parts.append(f'<text class="label" x="6" y="{layout.height - 6}">{label}</text>')

    parts.append("</svg>")
    return "".join(parts)


_BONES: tuple[tuple[str, str], ...] = (
    ("pelvis", "waist"),
    ("waist", "chest"),
    ("chest", "neck"),
    ("neck", "head"),
    ("chest", "shoulder_l"),
    ("shoulder_l", "elbow_l"),
    ("elbow_l", "wrist_l"),
    ("wrist_l", "hand_l"),
    ("chest", "shoulder_r"),
    ("shoulder_r", "elbow_r"),
    ("elbow_r", "wrist_r"),
    ("wrist_r", "hand_r"),
    ("pelvis", "hip_l"),
    ("hip_l", "knee_l"),
    ("knee_l", "ankle_l"),
    ("ankle_l", "toe_l"),
    ("pelvis", "hip_r"),
    ("hip_r", "knee_r"),
    ("knee_r", "ankle_r"),
    ("ankle_r", "toe_r"),
)


def _skeleton_overlay(joints: dict[str, tuple[float, float, float]]) -> str:
    segments = []
    for a, b in _BONES:
        if a not in joints or b not in joints:
            raise KeyError(f"skeleton overlay needs joints {a!r} and {b!r}")
        ax, ay, _ = joints[a]
        bx, by, _ = joints[b]
        segments.append(
            f'<line x1="{ax:.2f}" y1="{ay:.2f}" x2="{bx:.2f}" y2="{by:.2f}"/>'
        )
    dots = "".join(
        f'<circle cx="{x:.2f}" cy="{y:.2f}" r="1.9"/>' for x, y, _ in joints.values()
    )
    return (
        f'<g class="bone">{"".join(segments)}</g><g class="joint">{dots}</g>'
    )


def write(path: Path, svg: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(svg, encoding="utf-8")
