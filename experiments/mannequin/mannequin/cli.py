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

from . import (
    comfy_client,
    fit,
    isolate,
    measure,
    openpose,
    profile_bind,
    profile_proof,
    raster,
    render_svg,
    workflow,
)
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
MIN_HEAD_SILHOUETTE_IOU = 0.85
MIN_FULL_SILHOUETTE_IOU = 0.80



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
    extra_joints: tuple = ()
    if args.constraints is not None:
        if args.costume is not None:
            raise SystemExit("--constraints already owns fitted attachments; omit --costume")
        constraint_set = fit.load_constraints(Path(args.constraints))
        proportions = constraint_set.proportions
        attachments = fit.attachment_volumes(constraint_set.head_attachments)
        layout = Layout(
            width=args.width,
            height=args.height,
            pixels_per_head=args.height * 0.84 / constraint_set.frame_height_hu,
            origin_x=args.width / 2.0,
            ground_y=args.height * 0.92,
        )
    else:
        proportions = load_preset(args.preset)
        attachments: tuple = ()
        if args.costume is not None:
            extra_joints, attachments = load_costume(args.costume).build(proportions)
        layout = layout_for(proportions, args.width, args.height)

    rig = build_rig(proportions, extra_joints, attachments)
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
        "constraints": args.constraints,
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


def _is_edge(mask: bytes | bytearray, width: int, height: int, x: int, y: int) -> bool:
    if not mask[y * width + x]:
        return False
    for ny in range(max(0, y - 1), min(height, y + 2)):
        for nx in range(max(0, x - 1), min(width, x + 2)):
            if not mask[ny * width + nx]:
                return True
    return False


def _paint_pixel(
    pixels: bytearray,
    width: int,
    height: int,
    x: int,
    y: int,
    color: tuple[int, int, int, int],
) -> None:
    if not 0 <= x < width or not 0 <= y < height:
        return
    offset = (y * width + x) * 4
    pixels[offset : offset + 4] = bytes(color)


def _draw_line(
    pixels: bytearray,
    width: int,
    height: int,
    start: tuple[float, float],
    end: tuple[float, float],
    color: tuple[int, int, int, int],
) -> None:
    x0, y0 = round(start[0]), round(start[1])
    x1, y1 = round(end[0]), round(end[1])
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    step_x = 1 if x0 < x1 else -1
    step_y = 1 if y0 < y1 else -1
    error = dx + dy
    while True:
        _paint_pixel(pixels, width, height, x0, y0, color)
        if x0 == x1 and y0 == y1:
            return
        twice_error = 2 * error
        if twice_error >= dy:
            error += dy
            x0 += step_x
        if twice_error <= dx:
            error += dx
            y0 += step_y


def _write_fit_evidence(
    source: Path,
    out: Path,
    name: str,
    mask: bytearray,
    buffers: raster.Buffers,
    rig,
    world,
    layout: Layout,
    marks: fit.Landmarks,
) -> tuple[fit.SilhouetteScore, fit.SilhouetteScore]:
    width, height, pixels = read_rgba(source)
    isolated = bytearray(pixels)
    silhouette = bytearray(b"\x00\x00\x00\xff" * width * height)
    for index, covered in enumerate(mask):
        if covered:
            silhouette[index * 4 : index * 4 + 4] = b"\xff\xff\xff\xff"
            isolated[index * 4 + 3] = 255
        else:
            isolated[index * 4 : index * 4 + 4] = b"\x00\x00\x00\x00"
    write_rgba(out / f"{name}-isolated.png", width, height, isolated)
    write_rgba(out / f"{name}-silhouette.png", width, height, silhouette)
    wireframe = bytearray(pixels)
    for y in range(height):
        for x in range(width):
            source_edge = _is_edge(mask, width, height, x, y)
            fitted_edge = _is_edge(buffers.covered, width, height, x, y)
            if source_edge and fitted_edge:
                color = (255, 255, 255, 255)
            elif source_edge:
                color = (0, 220, 255, 255)
            elif fitted_edge:
                color = (255, 0, 220, 255)
            else:
                continue
            _paint_pixel(wireframe, width, height, x, y, color)

    joints = project_joints(world, "front", layout)
    for joint in rig.joints:
        if joint.parent is None:
            continue
        child = joints[joint.name]
        parent = joints[joint.parent]
        _draw_line(
            wireframe,
            width,
            height,
            (parent[0], parent[1]),
            (child[0], child[1]),
            (255, 220, 0, 255),
        )
    for x, y, _ in joints.values():
        for offset_y in range(-2, 3):
            for offset_x in range(-2, 3):
                _paint_pixel(
                    wireframe,
                    width,
                    height,
                    round(x) + offset_x,
                    round(y) + offset_y,
                    (255, 220, 0, 255),
                )
    write_rgba(out / f"{name}-wireframe.png", width, height, wireframe)

    difference = bytearray(b"\xff\xff\xff\xff" * width * height)
    for index, (reference, fitted) in enumerate(
        zip(mask, buffers.covered, strict=True)
    ):
        if reference and fitted:
            color = (40, 170, 70, 255)
        elif reference:
            color = (220, 55, 45, 255)
        elif fitted:
            color = (50, 95, 220, 255)
        else:
            continue
        difference[index * 4 : index * 4 + 4] = bytes(color)
    write_rgba(out / f"{name}-rig-diagnostic.png", width, height, difference)

    full_score = fit.compare_silhouettes(
        mask, buffers.covered, width, marks.top, marks.bottom
    )
    head_score = fit.compare_silhouettes(
        mask, buffers.covered, width, marks.top, marks.neck
    )
    return full_score, head_score


def command_fit(args: argparse.Namespace) -> int:
    source = Path(args.image)
    if not source.is_file():
        raise SystemExit(f"no such image: {source}")

    mask, width, height = isolate.subject_mask_from_png(source)
    result = fit.fit_proportions(mask, width, height, load_preset(args.base), args.name)
    print(result.report())

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    constraint_path = out / f"{args.name}-constraints.json"

    # The central skull defines one head unit. Ears, horns, or hair lobes remain
    # separate head-parented volumes, so their silhouette survives later poses.
    marks = result.landmarks
    layout = Layout(
        width=width,
        height=height,
        pixels_per_head=float(result.head_height),
        origin_x=result.center_x_px,
        ground_y=float(marks.bottom + 1),
    )
    attachments = fit.attachment_volumes(result.head_attachments)
    rig = build_rig(result.proportions, (), attachments)
    world = seat_on_ground(rig, solve(rig, static_pose("a-pose").pose), "both")
    buffers = raster.rasterize(width, height, project(rig, world, "front", layout))
    full_score, head_score = _write_fit_evidence(
        source, out, args.name, mask, buffers, rig, world, layout, marks
    )

    print(f"\nhead silhouette IoU: {head_score.iou:.1%}")
    print(f"full silhouette IoU: {full_score.iou:.1%}")
    print(
        f"wrote {out / (args.name + '-wireframe.png')} — "
        "cyan is the reference, magenta is the fit, yellow is the wireframe"
    )
    print(
        f"wrote {out / (args.name + '-silhouette.png')} — "
        "this exact isolated mask is the reference silhouette"
    )
    print(
        f"wrote {out / (args.name + '-rig-diagnostic.png')} — "
        "green overlaps, red is missing from the rig, blue is extra"
    )
    failures = []
    if head_score.iou < MIN_HEAD_SILHOUETTE_IOU:
        failures.append(
            f"head silhouette IoU {head_score.iou:.1%} is below "
            f"{MIN_HEAD_SILHOUETTE_IOU:.0%}"
        )
    if full_score.iou < MIN_FULL_SILHOUETTE_IOU:
        failures.append(
            f"full silhouette IoU {full_score.iou:.1%} is below "
            f"{MIN_FULL_SILHOUETTE_IOU:.0%}"
        )
    if failures:
        constraint_path.unlink(missing_ok=True)
        print("REJECTED: " + "; ".join(failures))
        print(f"did not publish {constraint_path}")
        return 1
    fit.write_constraints(constraint_path, result.constraints())
    print("ACCEPTED: fitted rig matches the isolated reference")
    print(f"wrote {constraint_path}")
    return 0


def command_bind_profile(args: argparse.Namespace) -> int:
    source = Path(args.image)
    if not source.is_file():
        raise SystemExit(f"no such image: {source}")
    mask, width, height = isolate.subject_mask_from_png(source)
    decoded_width, decoded_height, pixels = read_rgba(source)
    if (decoded_width, decoded_height) != (width, height):
        raise SystemExit("isolated mask and decoded profile dimensions differ")
    binding = profile_bind.make_binding(mask, width, height, args.work_size)
    print(binding.report())
    out = Path(args.out)
    profile_bind.write_evidence(
        out,
        binding,
        source_pixels=pixels,
        source_mask=mask,
        source_width=width,
        source_height=height,
    )
    print(f"\nwrote profile binding and recognizable pose previews to {out}")
    return 0


def command_generate_profile_proof(args: argparse.Namespace) -> int:
    config = profile_proof.ProfileProofConfig(
        prompt=args.prompt,
        seed=args.seed,
        control_strength=args.control_strength,
        control_end_percent=args.control_end_percent,
        identity_weight=args.identity_weight,
    )
    out = Path(args.out)
    profile_proof.generate(
        ComfyClient(args.url),
        Path(args.binding),
        Path(args.identity),
        Path(args.workflow),
        out,
        config,
    )
    print(
        f"wrote four-pose identity proof to {out}; "
        "this tests identity retention, not animation timing"
    )
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
    render.add_argument(
        "--constraints",
        default=None,
        help="reference-first constraint JSON from the fit command; replaces --preset",
    )
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

    bind_profile = sub.add_parser(
        "bind-profile",
        help="bind an isolated side-profile silhouette to a medial-axis skeleton",
    )
    bind_profile.add_argument("image")
    bind_profile.add_argument("--work-size", type=int, default=profile_bind.WORK_SIZE)
    bind_profile.add_argument("--out", default="out/profile-binding")
    bind_profile.set_defaults(func=command_bind_profile)

    generate_profile = sub.add_parser(
        "generate-profile-proof",
        help="generate a bounded four-pose identity proof from profile guides",
    )
    generate_profile.add_argument("--binding", required=True)
    generate_profile.add_argument("--identity", required=True)
    generate_profile.add_argument("--workflow", required=True)
    generate_profile.add_argument("--prompt", required=True)
    generate_profile.add_argument("--out", required=True)
    generate_profile.add_argument("--url", default=None)
    generate_profile.add_argument("--seed", type=int, default=42)
    generate_profile.add_argument("--control-strength", type=float, default=0.5)
    generate_profile.add_argument(
        "--control-end-percent", type=float, default=0.6
    )
    generate_profile.add_argument("--identity-weight", type=float, default=0.7)
    generate_profile.set_defaults(func=command_generate_profile_proof)

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
    except (
        comfy_client.ComfyError,
        profile_bind.BindingError,
        profile_proof.ProfileProofError,
        workflow.WorkflowError,
    ) as error:
        # These carry an actionable message and a stack trace adds nothing;
        # every other exception keeps its traceback, because an unexpected one
        # is a bug worth seeing in full.
        raise SystemExit(str(error)) from None


if __name__ == "__main__":
    sys.exit(main())
