"""Transfer the traced twelve-pose sheet to the mouse, emitting puppet commands.

Bone directions come directly from the trace. The body stays rigid, and both
legs use one shared scale while retaining the drawing's projected proportions.
An optional second pass seats only annotated support boots on a shared ground.
"""

import argparse
import json
import math
from pathlib import Path


def angle(pose, start, end):
    a, b = pose[start], pose[end]
    if math.dist(a, b) <= 1e-6:
        raise ValueError(f"collapsed bone: {start} -> {end}")
    return math.atan2(b[1] - a[1], b[0] - a[0])


def rotate(vector, radians):
    x, y = vector
    return [x * math.cos(radians) - y * math.sin(radians),
            x * math.sin(radians) + y * math.cos(radians)]


def add(a, b):
    return [a[0] + b[0], a[1] + b[1]]


def offset(a, b):
    return [a[0] - b[0], a[1] - b[1]]


def transfer(rest, trace, reference):
    posed = {"hip_c": list(rest["hip_c"])}
    turn = angle(trace, "hip_c", "neck") - angle(rest, "hip_c", "neck")
    for name in ["chest", "neck", "shoulder_l", "shoulder_r", "tail_base", "tail_tip"]:
        posed[name] = add(posed["hip_c"], rotate(offset(rest[name], rest["hip_c"]), turn))
    head_turn = angle(trace, "neck", "head_top") - angle(rest, "neck", "head_top")
    for name in ["head_top", "ear_l", "ear_r", "nose"]:
        posed[name] = add(posed["neck"], rotate(offset(rest[name], rest["neck"]), head_turn))
    def total_leg_length(pose):
        return sum(math.dist(pose["hip_c"], pose["knee_" + side]) +
                   math.dist(pose["knee_" + side], pose["ankle_" + side])
                   for side in ["l", "r"])

    reference_length = total_leg_length(reference)
    if reference_length <= 1e-6:
        raise ValueError("reference legs are collapsed")
    leg_scale = total_leg_length(rest) / reference_length
    for side in ["l", "r"]:
        chains = [("shoulder_" + side, "elbow_" + side, None),
                  ("elbow_" + side, "wrist_" + side, None),
                  ("hip_c", "knee_" + side, math.dist(trace["hip_c"], trace["knee_" + side]) * leg_scale),
                  ("knee_" + side, "ankle_" + side, math.dist(trace["knee_" + side], trace["ankle_" + side]) * leg_scale),
                  ("ankle_" + side, "toe_" + side, None)]
        for start, end, length in chains:
            length = math.dist(rest[start], rest[end]) if length is None else length
            radians = angle(trace, start, end)
            posed[end] = add(posed[start], [length * math.cos(radians), length * math.sin(radians)])
        wrist, elbow, paw = "wrist_" + side, "elbow_" + side, "paw_" + side
        forearm_turn = angle(posed, elbow, wrist) - angle(rest, elbow, wrist)
        posed[paw] = add(posed[wrist], rotate(offset(rest[paw], rest[wrist]), forearm_turn))
    if set(posed) != set(rest):
        raise ValueError("source skeleton does not match this mouse transfer")
    return posed


def grounded_poses(document, trace, render_root, ground_y):
    from PIL import Image

    poses = [frame["pose"] for frame in document["frames"]]
    shifts = []
    for frame in trace["frames"]:
        if frame["support"] == "flight":
            shifts.append(None)
            continue
        with Image.open(render_root / "part-poses" / (frame["support"] + "_boot") /
                        (frame["name"] + ".png")) as image:
            bounds = image.getchannel("A").getbbox()
            if bounds is None:
                raise ValueError("support boot is empty")
            shifts.append(ground_y - (bounds[3] - 1))
    for index, frame in enumerate(trace["frames"]):
        if shifts[index] is not None:
            continue
        neighbors = [shifts[(index - 1) % 12], shifts[(index + 1) % 12]]
        if any(value is None for value in neighbors):
            raise ValueError("flight needs neighboring support poses")
        shift = min(neighbors) - 4
        for side in ["near", "far"]:
            with Image.open(render_root / "part-poses" / (side + "_boot") /
                            (frame["name"] + ".png")) as image:
                bounds = image.getchannel("A").getbbox()
                if bounds is None:
                    raise ValueError("flight boot is empty")
                shift = min(shift, ground_y - 6 - (bounds[3] - 1))
        shifts[index] = shift
    return [{name: [point[0], point[1] + shift] for name, point in pose.items()}
            for pose, shift in zip(poses, shifts, strict=True)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ground-from-render", type=Path)
    parser.add_argument("--ground-y", type=int, default=214)
    args = parser.parse_args()
    document = json.loads(args.document.read_text())
    trace = json.loads(args.trace.read_text())
    names = [frame["name"] for frame in trace["frames"]]
    if len(names) != 12 or names != [frame["name"] for frame in document["frames"]]:
        raise ValueError("source and trace must share twelve ordered frame names")
    commands = []
    if args.ground_from_render:
        poses = grounded_poses(document, trace, args.ground_from_render, args.ground_y)
    else:
        poses = [transfer(document["rest_pose"], frame["pose"], trace["frames"][0]["pose"])
                 for frame in trace["frames"]]
        for side in ["l", "r"]:
            for bone in ["hip_c-knee_" + side, "knee_" + side + "-ankle_" + side]:
                commands.append({"command": "set_bone_stretch", "name": bone, "may_stretch": True})
    for name, pose in zip(names, poses, strict=True):
        for joint, point in pose.items():
            commands.append({"command": "pose_joint", "frame": name, "joint": joint,
                             "point": point, "scope": "frame"})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(commands, indent=2) + "\n")
    print(f"Wrote {len(commands)} commands to {args.output}")


if __name__ == "__main__":
    main()
