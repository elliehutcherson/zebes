"""Thin orchestration for the character-binding experiment.

Deterministic isolation and neutral topology extraction run in C++. Python remains
only for the still-changing semantic-binding prototype and local ComfyUI proof.
Run with `PYTHONPATH=experiments python3 -m character_binding.cli <command>`.
"""

from __future__ import annotations

import subprocess
import argparse
import sys
from pathlib import Path

from . import comfy_client, profile_bind, profile_proof, workflow
from .comfy_client import ComfyClient
from .png import read_rgba


def command_bind_profile(args: argparse.Namespace) -> int:
    source = Path(args.isolated)
    topology = Path(args.topology)
    if not source.is_file():
        raise SystemExit(f"no such isolated profile: {source}")
    if not topology.is_file():
        raise SystemExit(f"no such C++ topology evidence: {topology}")
    width, height, pixels = read_rgba(source)
    source_mask = bytearray(
        1 if pixels[index * 4 + 3] > args.alpha_threshold else 0
        for index in range(width * height)
    )
    topology_width, topology_height, topology_pixels = read_rgba(topology)
    if (
        topology_width != topology_height
        or width % topology_width != 0
        or height % topology_height != 0
        or width // topology_width != height // topology_height
    ):
        raise SystemExit("isolated profile and C++ topology use incompatible scales")
    topology_mask = bytearray(topology_width * topology_height)
    skeleton = bytearray(topology_width * topology_height)
    for index in range(topology_width * topology_height):
        offset = index * 4
        red, green, blue = topology_pixels[offset : offset + 3]
        topology_mask[index] = red != 0 or green != 0 or blue != 0
        skeleton[index] = red > 200 and green < 128 and blue < 128
    binding = profile_bind.make_binding_from_topology(
        topology_mask,
        skeleton,
        topology_width,
        topology_height,
        width // topology_width,
    )
    print(binding.report())
    out = Path(args.out)
    profile_bind.write_evidence(
        out,
        binding,
        source_pixels=pixels,
        source_mask=source_mask,
        source_width=width,
        source_height=height,
    )
    if not args.skip_control_render:
        control_tool = Path(args.control_tool)
        if not control_tool.is_file():
            raise SystemExit(
                f"no C++ posed-control tool: {control_tool}; "
                "build target profile_pose_control"
            )
        for pose in profile_bind.POSES:
            completed = subprocess.run(
                [
                    str(control_tool),
                    f"--mask={out / f'pose-{pose}.png'}",
                    f"--binding={out / 'binding.json'}",
                    f"--pose={pose}",
                    f"--output={out / f'pose-{pose}-control.png'}",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            if completed.returncode != 0:
                detail = completed.stderr.strip() or completed.stdout.strip()
                raise SystemExit(f"C++ posed-control rendering failed: {detail}")
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
    parser = argparse.ArgumentParser(prog="character-binding", description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    bind_profile = subcommands.add_parser(
        "bind-profile",
        help="prototype semantic binding from C++ isolated/topology PNGs",
    )
    bind_profile.add_argument("isolated")
    bind_profile.add_argument("topology")
    bind_profile.add_argument("--alpha-threshold", type=int, default=16)
    bind_profile.add_argument(
        "--control-tool", default="build/dev/bin/profile_pose_control"
    )
    bind_profile.add_argument("--skip-control-render", action="store_true")
    bind_profile.add_argument("--out", default="out/profile-binding")
    bind_profile.set_defaults(func=command_bind_profile)

    generate_profile = subcommands.add_parser(
        "generate-profile-proof",
        help="generate a bounded four-pose identity proof from profile controls",
    )
    generate_profile.add_argument("--binding", required=True)
    generate_profile.add_argument("--identity", required=True)
    generate_profile.add_argument("--workflow", required=True)
    generate_profile.add_argument("--prompt", required=True)
    generate_profile.add_argument("--out", required=True)
    generate_profile.add_argument("--url", default=None)
    generate_profile.add_argument("--seed", type=int, default=42)
    generate_profile.add_argument("--control-strength", type=float, default=0.5)
    generate_profile.add_argument("--control-end-percent", type=float, default=0.6)
    generate_profile.add_argument("--identity-weight", type=float, default=0.7)
    generate_profile.set_defaults(func=command_generate_profile_proof)

    comfy = subcommands.add_parser("comfy", help="check the ComfyUI box")
    comfy.add_argument("--url", default=None)
    comfy.set_defaults(func=command_comfy)

    template = subcommands.add_parser(
        "template", help="list the handles exposed by a ComfyUI workflow"
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
        raise SystemExit(str(error)) from None


if __name__ == "__main__":
    sys.exit(main())
