"""Command line entry points.

Three commands, matching the three jobs this tool has: `render` produces the
conditioning maps and reference views, `report` shows which measurements a given
render size can actually carry, and `gate` measures drift between frames.

Run as `python3 -m mannequin.cli <command>` from `experiments/mannequin/`.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

from . import comfy_client, fit, isolate, measure, openpose, raster, render_svg, workflow
from .comfy_client import ComfyClient
from .costume import COSTUMES, costume as load_costume
from .measurements import (
    GATE_TESTABLE_FIELDS,
    PRESETS,
    preset as load_preset,
    resolution_report,
)
from .pose import Frame, STATIC_POSES, idle_cycle, run_cycle, static_pose
from .png import read_rgba, write_rgba
from .project import (
    VIEWS,
    Layout,
    layout_for,
    project,
    project_joints,
)
from .skeleton import build_rig, seat_on_ground, shell_height, skeletal_height, solve

CYCLES = ("run", "idle")


def _frames_for(args: argparse.Namespace) -> tuple[Frame, ...]:
    if args.cycle is None:
        return (static_pose(args.pose),)
    if args.cycle == "run":
        return run_cycle(args.frames)
    return idle_cycle(args.frames)


def _views_for(spec: str) -> tuple[str, ...]:
    views = tuple(v.strip() for v in spec.split(",") if v.strip())
    if not views:
        raise SystemExit("no views requested")
    unknown = [v for v in views if v not in VIEWS]
    if unknown:
        raise SystemExit(f"unknown views {unknown}; available: {sorted(VIEWS)}")
    return views


def command_render(args: argparse.Namespace) -> int:
    proportions = load_preset(args.preset)
    extra_joints: tuple = ()
    attachments: tuple = ()
    if args.costume is not None:
        extra_joints, attachments = load_costume(args.costume).build(proportions)

    rig = build_rig(proportions, extra_joints, attachments)
    layout = layout_for(proportions, args.width, args.height)
    frames = _frames_for(args)
    views = _views_for(args.views)
    out = Path(args.out)

    # Rasterise everything before writing any depth map. Depth is normalised
    # across every frame of a view, so a limb's brightness means the same thing
    # in each frame of a cycle. The range is per view rather than global,
    # because the camera axis is a different axis of the body in each one:
    # normalising a front view against a side view's much deeper range would
    # crush the front view into a flat grey slab.
    rendered = []
    ranges: dict[str, tuple[float, float]] = {}
    for frame in frames:
        world = seat_on_ground(rig, solve(rig, frame.pose), frame.planted_foot)
        for view in views:
            items = project(rig, world, view, layout)
            buffers = raster.rasterize(args.width, args.height, items)
            clipped = raster.clipped_edges(buffers)
            if clipped:
                raise SystemExit(
                    f"frame {frame.index} ({frame.label}) in view {view} runs off "
                    f"the {', '.join(clipped)} edge of a {args.width}x{args.height} "
                    "canvas. Scale is fixed by the measurement set and shared "
                    "across the set, so widen --width/--height rather than "
                    "expecting the figure to shrink."
                )
            frame_near, frame_far = raster.depth_range(buffers)
            near, far = ranges.get(view, (frame_near, frame_far))
            ranges[view] = (min(near, frame_near), max(far, frame_far))
            rendered.append((frame, view, world, items, buffers))

    entries = []
    for frame, view, world, items, buffers in rendered:
        stem = f"{frame.index:02d}-{view}"
        joints = project_joints(world, view, layout)
        points = openpose.keypoints(proportions, world, view, layout)

        near, far = ranges[view]
        raster.write_silhouette(out / "silhouette" / f"{stem}.png", buffers)
        raster.write_depth_with_range(out / "depth" / f"{stem}.png", buffers, near, far)
        raster.write_regions(out / "regions" / f"{stem}.png", buffers)
        raster.write_outline(out / "outline" / f"{stem}.png", buffers)
        openpose.write_skeleton_png(out / "openpose" / f"{stem}.png", points, layout)
        (out / "openpose" / f"{stem}.json").write_text(
            openpose.to_json(points, layout), encoding="utf-8"
        )

        for mode in render_svg.MODES:
            render_svg.write(
                out / "svg" / mode / f"{stem}.svg",
                render_svg.render(
                    items,
                    layout,
                    proportions.heads_tall,
                    mode=mode,
                    joints=joints,
                    label=f"{frame.label} / {view}",
                ),
            )

        signature = measure.signature_from_mask(
            buffers.covered, args.width, args.height, args.bands
        )
        entries.append(
            {
                "index": frame.index,
                "label": frame.label,
                "view": view,
                "planted_foot": frame.planted_foot,
                "skeletal_height_heads": round(skeletal_height(rig, world), 6),
                "shell_height_heads": round(shell_height(rig, world), 6),
                "signature": asdict(signature),
            }
        )

    manifest = {
        "preset": proportions.name,
        "costume": args.costume,
        "heads_tall": proportions.heads_tall,
        "canvas": {"width": args.width, "height": args.height},
        "pixels_per_head": layout.pixels_per_head,
        "origin_x": layout.origin_x,
        "contact_line_y": layout.ground_y,
        "depth_range_by_view": {
            view: {"near": near, "far": far} for view, (near, far) in ranges.items()
        },
        "bands": args.bands,
        "frames": entries,
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )

    print(f"wrote {len(entries)} frame renders to {out}")
    return 0


def command_report(args: argparse.Namespace) -> int:
    proportions = load_preset(args.preset)
    report = resolution_report(proportions, args.height_px)
    print(f"{proportions.name} at {args.height_px:g} px character height")
    print(f"{'measurement':<20}{'head units':>12}{'pixels':>9}  {'tier':<10}resolved")
    for name, (head_units, pixels, clears) in report.items():
        tier = "gate" if name in GATE_TESTABLE_FIELDS else "authoring"
        print(
            f"{name:<20}{head_units:>12.3f}{pixels:>9.2f}  {tier:<10}"
            f"{'yes' if clears else 'sub-pixel'}"
        )
    print(
        "\ntier: 'gate' measurements may be used as acceptance evidence; "
        "'authoring' ones\nsteer generation but are too small to fail a frame "
        "over at this size."
    )
    return 0


def command_gate(args: argparse.Namespace) -> int:
    paths = [Path(args.reference), Path(args.candidate)]
    for path in paths:
        if not path.is_file():
            raise SystemExit(f"no such file: {path}")

    reference = measure.signature_from_png(paths[0], args.bands)
    candidate = measure.signature_from_png(paths[1], args.bands)
    comparison = measure.compare(reference, candidate, args.tolerance)
    print(comparison.report())
    return 0 if comparison.passed else 1


def command_fit(args: argparse.Namespace) -> int:
    source = Path(args.image)
    if not source.is_file():
        raise SystemExit(f"no such image: {source}")

    mask, width, height = isolate.subject_mask_from_png(source)
    result = fit.fit_proportions(mask, width, height, load_preset(args.base), args.name)
    print(result.report())

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / f"{args.name}.json").write_text(
        json.dumps(asdict(result.proportions), indent=2, sort_keys=True),
        encoding="utf-8",
    )

    # Align the rig to the source exactly: one head unit is the measured head
    # height, the ground is the figure's own feet, and the origin is its centre.
    # Anything else and the overlay would show a scale error rather than a fit
    # error, which is the one thing it exists to rule out.
    marks = result.landmarks
    spans = fit.row_spans(mask, width, height)
    # Centre on the torso, not on the whole silhouette. A tail, a held weapon or
    # one outflung arm drags a full-bounding-box centre sideways, and the overlay
    # would then show an offset that is not a fitting error.
    torso = [s for s in spans[marks.shoulder : marks.crotch] if s is not None]
    if not torso:
        raise SystemExit("no torso rows between the shoulder and crotch landmarks")
    centres = sorted((s.left + s.right) / 2.0 for s in torso)
    layout = Layout(
        width=width,
        height=height,
        pixels_per_head=float(marks.head_height),
        origin_x=centres[len(centres) // 2],
        ground_y=float(marks.bottom + 1),
    )
    rig = build_rig(result.proportions)
    world = seat_on_ground(rig, solve(rig, static_pose("a-pose").pose), "both")
    buffers = raster.rasterize(width, height, project(rig, world, "front", layout))

    _, _, pixels = read_rgba(source)
    overlay = bytearray(pixels)
    for i, on in enumerate(buffers.covered):
        if on:
            # Half-strength magenta: the source stays readable underneath.
            for channel, value in ((0, 255), (1, 0), (2, 255)):
                overlay[i * 4 + channel] = (overlay[i * 4 + channel] + value) // 2
            overlay[i * 4 + 3] = 255
    write_rgba(out / f"{args.name}-overlay.png", width, height, overlay)

    print(f"\nwrote {out / (args.name + '.json')}")
    print(f"wrote {out / (args.name + '-overlay.png')} — check the fit before using it")
    return 0


def command_comfy(args: argparse.Namespace) -> int:
    client = ComfyClient(args.url)
    stats = client.system_stats()
    system = stats.get("system", {})
    print(f"reached ComfyUI at {client.base_url}")
    print(f"  version : {system.get('comfyui_version', 'unknown')}")
    print(f"  python  : {system.get('python_version', 'unknown').split()[0]}")
    for device in stats.get("devices", []):
        free = device.get("vram_free")
        total = device.get("vram_total")
        detail = (
            f"{free / 1024**3:.1f} of {total / 1024**3:.1f} GiB free"
            if isinstance(free, (int, float)) and isinstance(total, (int, float))
            else "unknown VRAM"
        )
        print(f"  device  : {device.get('name', 'unnamed')} ({detail})")
    return 0


def command_template(args: argparse.Namespace) -> int:
    print(workflow.describe(workflow.load(Path(args.path))))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mannequin", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    render = sub.add_parser("render", help="render reference views or a pose cycle")
    render.add_argument("--preset", default="heroic-6h", choices=sorted(PRESETS))
    render.add_argument("--costume", default=None, choices=sorted(COSTUMES))
    render.add_argument("--pose", default="a-pose", choices=sorted(STATIC_POSES))
    render.add_argument(
        "--cycle", default=None, choices=CYCLES, help="render a cycle instead of --pose"
    )
    render.add_argument("--frames", type=int, default=12)
    render.add_argument("--views", default="front,right,back")
    # 1024 square is the natural generation size, and it is wide enough for a
    # side-view running stride. The origin is fixed at the canvas centre so the
    # whole set shares one registration, which means a wide pose needs a wider
    # canvas rather than a smaller figure.
    render.add_argument("--width", type=int, default=1024)
    render.add_argument("--height", type=int, default=1024)
    render.add_argument("--bands", type=int, default=measure.DEFAULT_BANDS)
    render.add_argument("--out", default="out")
    render.set_defaults(func=command_render)

    report = sub.add_parser(
        "report", help="show which measurements survive a render height"
    )
    report.add_argument("--preset", default="heroic-6h", choices=sorted(PRESETS))
    report.add_argument("--height-px", type=float, default=44.0)
    report.set_defaults(func=command_report)

    gate = sub.add_parser("gate", help="compare two isolated frames for drift")
    gate.add_argument("reference")
    gate.add_argument("candidate")
    gate.add_argument("--tolerance", type=float, default=0.08)
    gate.add_argument("--bands", type=int, default=measure.DEFAULT_BANDS)
    gate.set_defaults(func=command_gate)

    fit_parser = sub.add_parser(
        "fit", help="derive a measurement set from a generated character image"
    )
    fit_parser.add_argument("image")
    fit_parser.add_argument("--base", default="trickster-3h", choices=sorted(PRESETS))
    fit_parser.add_argument("--name", default="fitted")
    fit_parser.add_argument("--out", default="out/fitted")
    fit_parser.set_defaults(func=command_fit)

    comfy = sub.add_parser("comfy", help="check the ComfyUI box is reachable")
    comfy.add_argument(
        "--url",
        default=None,
        help=f"defaults to ${comfy_client.BASE_URL_ENV} or "
        f"{comfy_client.DEFAULT_BASE_URL}",
    )
    comfy.set_defaults(func=command_comfy)

    template = sub.add_parser(
        "template", help="list the ZEBES_ handles an API-format workflow exposes"
    )
    template.add_argument("path")
    template.set_defaults(func=command_template)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except (comfy_client.ComfyError, workflow.WorkflowError) as error:
        # These carry an actionable message and a stack trace adds nothing;
        # every other exception keeps its traceback, because an unexpected one
        # is a bug worth seeing in full.
        raise SystemExit(str(error)) from None


if __name__ == "__main__":
    sys.exit(main())
