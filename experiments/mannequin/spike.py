"""Rung-1 spike: does depth conditioning hold one character across a cycle?

Run from `experiments/mannequin/` with a ComfyUI tunnel open:

    python3 spike.py --out out/spike/run-01

The acceptance criteria below are fixed here, in code, so that they are decided
before the run rather than after it. The deprecated pose-conditioned experiment
was rejected by eye after six provider turns; the point of this file is that the
verdict is arithmetic.

**What is being measured.** Every frame in the set shares one scale, because
`layout_for` derives pixels-per-head from the measurement set rather than from a
frame's bounding box. So the character's head is the same width in pixels in
every guide, and it must come back the same width in every generated frame. Head
width variance across the cycle *is* identity drift, measured directly. That is
the failure the pilots hit: "frame 0 has a visibly larger helmet".

**What is not being measured.** Silhouette proportion is what ControlNet already
pins, so passing this is evidence that the conditioning worked, not that the
character is recognisably the same. Armour detail, palette, and helmet styling
can drift while every number here stays green. Judge those by eye, and reach for
IP-Adapter or a LoRA if they wander.
"""

from __future__ import annotations

import argparse
import json
import statistics
from dataclasses import asdict, dataclass
from pathlib import Path

from mannequin import isolate, measure, raster
from mannequin.comfy_client import ComfyClient
from mannequin.costume import costume
from mannequin.measurements import preset
from mannequin.pose import run_cycle
from mannequin.project import layout_for, project
from mannequin.skeleton import build_rig, seat_on_ground, solve
from mannequin import workflow

# --- Pre-registered acceptance criteria. Do not edit after a run. -------------

# Head and shoulder widths are measured in pixels and must not vary across the
# cycle by more than this fraction of the set's median.
MAX_HEAD_WIDTH_DRIFT = 0.10
MAX_SHOULDER_WIDTH_DRIFT = 0.12

# A frame touching the canvas edge is cropped, and every measurement taken from
# it is wrong in a way nothing downstream can detect.
ALLOW_BORDER_CONTACT = False

# -----------------------------------------------------------------------------

FRAMES = 12
VIEW = "right"
CANVAS = 1024
SEED = 42

# The grip is constant relative to the forearm, so the blade swings with the arm
# instead of being re-aimed per frame.
WRIST_GRIP = (-95.0, 0.0, 0.0)

PROMPT = (
    "2d game character art, armored knight with helmet and red cape, "
    "running while carrying a raised sword, side profile, full body, "
    "bold flat colors, thick clean outlines, high contrast, dark fantasy, "
    "plain solid background"
)


@dataclass
class FrameResult:
    index: int
    label: str
    guide_head_px: int
    head_px: int
    shoulder_px: int
    height_px: int
    touched_border: bool


def widest_run(mask: bytearray, width: int, rows: tuple[int, int]) -> int:
    """Widest *contiguous* run of subject pixels across a row range.

    Contiguous, not the span from leftmost to rightmost pixel. A raised sword or
    a trailing cape is a separate component on the same rows, and measuring
    min-to-max would silently fold the gap between them into the body's width.
    """
    widest = 0
    for y in range(rows[0], rows[1]):
        run = 0
        for x in range(width):
            if mask[y * width + x]:
                run += 1
                widest = max(widest, run)
            else:
                run = 0
    return widest


def head_rows(items, layout) -> tuple[int, int]:
    """Screen rows the skull or helmet occupies, taken from the rig itself.

    Deriving the head band from the silhouette's own extent is what broke the
    first run: with a raised sword the topmost pixel is the blade tip, so the
    band that should have crossed the head crossed the blade instead. The rig
    knows exactly where the head is, so ask it.
    """
    from mannequin.project import ProjectedEllipse

    candidates = [
        item
        for item in items
        if isinstance(item, ProjectedEllipse) and item.name in ("skull", "helmet")
    ]
    if not candidates:
        raise SystemExit("rig has no skull or helmet volume to anchor the head band")
    top = min(item.cy - item.ry for item in candidates)
    bottom = max(item.cy + item.ry for item in candidates)
    # Trim the crown and jaw, where the ellipse tapers and a millimetre of
    # styling changes the width a lot.
    inset = (bottom - top) * 0.25
    return (
        max(0, int(top + inset)),
        min(layout.height, int(bottom - inset)),
    )


def shoulder_rows(joints, layout) -> tuple[int, int]:
    """A band straddling the chest joint, again from the rig."""
    chest_y = joints["chest"][1]
    half = layout.pixels_per_head * 0.12
    return (
        max(0, int(chest_y - half)),
        min(layout.height, int(chest_y + half)),
    )


def render_guides(out: Path, write: bool = True):
    from mannequin.project import project_joints

    p = preset("heroic-6h")
    joints, volumes = costume("knight").build(p)
    rig = build_rig(p, joints, volumes)
    layout = layout_for(p, CANVAS, CANVAS)

    rendered, near, far = [], float("inf"), float("-inf")
    for frame in run_cycle(FRAMES):
        pose = dict(frame.pose)
        pose["wrist_l"] = WRIST_GRIP
        world = seat_on_ground(rig, solve(rig, pose), frame.planted_foot)
        items = project(rig, world, VIEW, layout)
        buffers = raster.rasterize(CANVAS, CANVAS, items)
        clipped = raster.clipped_edges(buffers)
        if clipped:
            raise SystemExit(f"frame {frame.index} clips the {clipped} edge")
        lo, hi = raster.depth_range(buffers)
        near, far = min(near, lo), max(far, hi)
        rendered.append(
            (frame, buffers, head_rows(items, layout),
             shoulder_rows(project_joints(world, VIEW, layout), layout))
        )

    if write:
        for frame, buffers, _, _ in rendered:
            raster.write_depth_with_range(
                out / "guide" / f"{frame.index:02d}-depth.png", buffers, near, far
            )
            raster.write_silhouette(
                out / "guide" / f"{frame.index:02d}-sil.png", buffers
            )
    return rendered, p, layout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--url", default="http://127.0.0.1:8188")
    parser.add_argument(
        "--remeasure",
        action="store_true",
        help="re-score frames already generated in --out, without touching the GPU",
    )
    parser.add_argument("--strength", type=float, default=None)
    parser.add_argument("--end-percent", type=float, default=None)
    args = parser.parse_args()

    out = Path(args.out)
    if out.exists() and not args.remeasure:
        raise SystemExit(f"{out} already exists; evidence directories are immutable")

    rendered, proportions, layout = render_guides(out, write=not args.remeasure)
    print(f"{'measured' if args.remeasure else 'rendered'} {len(rendered)} guides")

    client = None if args.remeasure else ComfyClient(args.url)
    template = None if args.remeasure else workflow.load(
        Path("workflows/depth-controlnet.json")
    )

    results: list[FrameResult] = []
    for frame, guide_buffers, head_band, shoulder_band in rendered:
        final = out / "generated" / f"{frame.index:02d}.png"
        if not args.remeasure:
            uploaded = client.upload_image(
                out / "guide" / f"{frame.index:02d}-depth.png"
            )
            patches = {
                workflow.CONTROL_IMAGE: {"image": uploaded.reference},
                workflow.POSITIVE_PROMPT: {"text": PROMPT},
                workflow.SEED: {"seed": SEED},
            }
            control = {}
            if args.strength is not None:
                control["strength"] = args.strength
            if args.end_percent is not None:
                control["end_percent"] = args.end_percent
            if control:
                patches["ZEBES_CONTROL_STRENGTH"] = control
            patched = workflow.apply(template, patches)
            client.run(patched, out / "generated")[0].rename(final)

        mask, width, height = isolate.subject_mask_from_png(final)
        signature = measure.signature_from_mask(mask, width, height)
        results.append(
            FrameResult(
                index=frame.index,
                label=frame.label,
                guide_head_px=widest_run(guide_buffers.covered, CANVAS, head_band),
                head_px=widest_run(mask, width, head_band),
                shoulder_px=widest_run(mask, width, shoulder_band),
                height_px=signature.height_px,
                touched_border=isolate.touches_border(mask, width, height),
            )
        )
        r = results[-1]
        print(f"  frame {r.index:2d} {r.label:16s} guide_head={r.guide_head_px:4d}px "
              f"head={r.head_px:4d}px shoulder={r.shoulder_px:4d}px")

    heads = [r.head_px for r in results]
    shoulders = [r.shoulder_px for r in results]
    head_median = statistics.median(heads)
    shoulder_median = statistics.median(shoulders)
    head_drift = max(abs(v - head_median) for v in heads) / head_median
    shoulder_drift = max(abs(v - shoulder_median) for v in shoulders) / shoulder_median
    cropped = [r.index for r in results if r.touched_border]

    failures = []
    if head_drift > MAX_HEAD_WIDTH_DRIFT:
        failures.append(
            f"head width drifts {head_drift:.1%} (limit {MAX_HEAD_WIDTH_DRIFT:.0%})"
        )
    if shoulder_drift > MAX_SHOULDER_WIDTH_DRIFT:
        failures.append(
            f"shoulder width drifts {shoulder_drift:.1%} "
            f"(limit {MAX_SHOULDER_WIDTH_DRIFT:.0%})"
        )
    if cropped and not ALLOW_BORDER_CONTACT:
        failures.append(f"frames {cropped} touch the canvas border")

    verdict = {
        "seed": SEED,
        "frames": FRAMES,
        "prompt": PROMPT,
        "criteria": {
            "max_head_width_drift": MAX_HEAD_WIDTH_DRIFT,
            "max_shoulder_width_drift": MAX_SHOULDER_WIDTH_DRIFT,
        },
        "measured": {
            "head_px": heads,
            "head_median": head_median,
            "head_drift": head_drift,
            "shoulder_px": shoulders,
            "shoulder_median": shoulder_median,
            "shoulder_drift": shoulder_drift,
        },
        "per_frame": [asdict(r) for r in results],
        "accepted": not failures,
        "failures": failures,
    }
    (out / "verdict.json").write_text(json.dumps(verdict, indent=2), encoding="utf-8")

    print()
    print(f"head width    : median {head_median:.0f}px, max drift {head_drift:.1%} "
          f"(limit {MAX_HEAD_WIDTH_DRIFT:.0%})")
    print(f"shoulder width: median {shoulder_median:.0f}px, max drift {shoulder_drift:.1%} "
          f"(limit {MAX_SHOULDER_WIDTH_DRIFT:.0%})")
    print()
    print("ACCEPTED" if not failures else "REJECTED: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
