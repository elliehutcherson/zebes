#!/usr/bin/env python3
"""Validate sprite-sheet joint traces without changing the trace or its artwork.

Joints must carry observed/estimated confidence, stay inside their source cells,
and identify support consistently. This is an offline replacement for the
retired browser tracing server.
"""

import argparse
import json
from pathlib import Path

JOINTS = ["hip_c", "neck", "head_top",
          "shoulder_l", "elbow_l", "wrist_l", "paw_l",
          "shoulder_r", "elbow_r", "wrist_r", "paw_r",
          "knee_l", "ankle_l", "toe_l", "knee_r", "ankle_r", "toe_r"]
SUPPORTS = {"near", "far", "flight"}
CONFIDENCE = {"observed", "estimated"}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(path):
    """Read dimensions from the PNG header without loading the image payload."""
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) < 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG")
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    if width <= 0 or height <= 0:
        raise ValueError(f"{path} reports an empty image")
    return width, height


def validate(trace, sheet_size):
    """Reject anything a later stage would have to guess about."""
    if not isinstance(trace, dict):
        raise ValueError("the trace must be an object")
    frames = trace.get("frames")
    if not isinstance(frames, list) or not frames:
        raise ValueError("the trace needs at least one frame")
    sheet_width, sheet_height = sheet_size
    names = set()
    for frame in frames:
        name = frame.get("name")
        if not name or name in names:
            raise ValueError(f"frames need unique names, saw {name!r} twice")
        names.add(name)
        if frame.get("support") not in SUPPORTS:
            raise ValueError(f"{name}: support must be one of {sorted(SUPPORTS)}")
        cell = frame.get("cell")
        if not (isinstance(cell, list) and len(cell) == 4):
            raise ValueError(f"{name}: cell must be [x, y, width, height]")
        x, y, width, height = cell
        if width <= 0 or height <= 0:
            raise ValueError(f"{name}: cell must have a positive size")
        if x < 0 or y < 0 or x + width > sheet_width or y + height > sheet_height:
            raise ValueError(f"{name}: cell {cell} falls outside the {sheet_width}x{sheet_height} sheet")
        pose, confidence = frame.get("pose", {}), frame.get("confidence", {})
        missing = [joint for joint in JOINTS if joint not in pose]
        if missing:
            raise ValueError(f"{name}: still unplaced: {', '.join(missing)}")
        extra = sorted(set(pose) - set(JOINTS))
        if extra:
            raise ValueError(f"{name}: unknown joints: {', '.join(extra)}")
        for joint in JOINTS:
            point = pose[joint]
            if not (isinstance(point, list) and len(point) == 2):
                raise ValueError(f"{name}/{joint}: expected [x, y]")
            if not all(isinstance(value, (int, float)) for value in point):
                raise ValueError(f"{name}/{joint}: coordinates must be numbers")
            if not (0 <= point[0] < width and 0 <= point[1] < height):
                raise ValueError(f"{name}/{joint}: {point} falls outside its own cell")
            if confidence.get(joint) not in CONFIDENCE:
                raise ValueError(f"{name}/{joint}: mark it observed or estimated")
    supports = [frame["support"] for frame in frames]
    if all(support == "flight" for support in supports):
        raise ValueError("no frame has a foot on the ground, so nothing can be grounded")
    for frame in frames:
        conflict = support_conflict(frame)
        if conflict:
            raise ValueError(conflict)
    return trace


def lowest_point(pose, side):
    """How far down a leg reaches, measured at whichever of ankle or toe is lower.

    Comparing toes alone would call a heel strike an error, because a planted foot
    can hold its toe above the swinging foot's.
    """
    return max(pose["ankle_" + side][1], pose["toe_" + side][1])


def support_conflict(frame):
    """The named support foot must be the one actually reaching furthest down.

    Grounding seats the named foot on the ground row and moves the whole body to
    do it. Name the raised foot and the other leg is driven through the floor:
    reference_01 labelled far while its near foot sat ten source pixels lower,
    and the transferred mouse put its near toe fourteen pixels below ground.
    """
    support = frame["support"]
    if support == "flight":
        return None
    pose = frame["pose"]
    named, other = ("l", "r") if support == "near" else ("r", "l")
    reach, rival = lowest_point(pose, named), lowest_point(pose, other)
    if reach >= rival:
        return None
    return (f"{frame['name']}: support is {support}, but that foot reaches row {reach:g} "
            f"while the other reaches {rival:g}. Either flip the support or swap the legs.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--sheet", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-10.png")
    parser.add_argument("--trace", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-trace-v1.json")
    args = parser.parse_args()
    try:
        trace = validate(json.loads(args.trace.read_text()), png_size(args.sheet))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    estimated = sum(value == "estimated" for frame in trace["frames"]
                    for value in frame["confidence"].values())
    print(f"Validated {len(trace['frames'])} frames; {estimated} estimated joints.")


if __name__ == "__main__":
    main()
