"""Build the pose-analogy review package: reference and character panels sharing
one body-normalised grid, with a near/far skeleton drawn over each.

Each card states a single visual analogy. The top row shows the reference
character standing and then holding one run pose; the bottom row shows the mouse
standing and then the same pose carried onto the mouse's own proportions.

The grid is deliberately not a shared absolute pixel lattice. Each character gets
a square grid whose cell is a third of that character's own hip-to-ground
distance, with its origin on that character's hip column and ground row. Both
panels then show the same window measured in cells, so a joint in cell (3, -2)
sits at the same place in both pictures while the reference's human proportions
never dictate the mouse's. An absolute lattice would dictate them, and that is
the failure already recorded for proportions-v3.

Leg scale is normalised on the most extended frame of the cycle rather than the
first. Reference frame one is the most folded pose in this particular sheet, and
normalising there stretches every other frame past the mouse's own leg length,
lifting the hip clear of its hip row.

Nothing here contacts an image generator. The output is for review only.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

if __package__:
    from .retarget_run_reference import transfer
else:
    from retarget_run_reference import transfer

CELL = 80
MOUSE_GROUND_Y = 189

COLUMNS = 8
ROWS_UP = 14
ROWS_DOWN = 1
CELL_PIXELS = 26
BODY_CELLS = 12

NEAR = (247, 147, 42)
FAR = (58, 110, 219)
SPINE = (56, 48, 72)
GRID = (150, 205, 232)
ANATOMY = (222, 60, 130)
SUPPORT = (24, 170, 104)
PAPER = (250, 249, 252)

ANATOMY_ROWS = ["ground", "hip_c", "neck", "head_top"]

NEAR_BONES = [("shoulder_l", "elbow_l"), ("elbow_l", "wrist_l"), ("wrist_l", "paw_l"),
              ("hip_c", "knee_l"), ("knee_l", "ankle_l"), ("ankle_l", "toe_l")]
FAR_BONES = [("shoulder_r", "elbow_r"), ("elbow_r", "wrist_r"), ("wrist_r", "paw_r"),
             ("hip_c", "knee_r"), ("knee_r", "ankle_r"), ("ankle_r", "toe_r")]
SPINE_BONES = [("hip_c", "neck"), ("neck", "head_top"),
               ("neck", "shoulder_l"), ("neck", "shoulder_r")]
MOUSE_BONES = [("neck", "nose"), ("head_top", "ear_l"), ("head_top", "ear_r"),
               ("hip_c", "tail_base"), ("tail_base", "tail_tip")]
TAIL_BONES = [("hip_c", "tail_base"), ("tail_base", "tail_tip")]
HEAD_JOINTS = ["head_top", "ear_l", "ear_r", "nose"]
TAIL = (150, 116, 92)

PANEL = ((2 * COLUMNS) * CELL_PIXELS, (ROWS_UP + ROWS_DOWN) * CELL_PIXELS)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def total_leg_length(pose):
    return sum(math.dist(pose["hip_c"], pose["knee_" + side]) +
               math.dist(pose["knee_" + side], pose["ankle_" + side])
               for side in ["l", "r"])


def derive_arms(pose, confidence=None):
    """Fill in guessed elbows and wrists with a bow along shoulder-to-paw.

    At 42px of body height an arm segment is two or three pixels, so a guessed
    elbow carries roughly 20 degrees of noise and the transferred near arm ends up
    folded against the hip. The two ends of the chain are the readable features:
    the shoulder sits on the torso edge and the paw is the cannon muzzle, six or
    seven pixels of unambiguous direction.

    A joint someone marked observed is left exactly where they put it. This helper
    exists to replace guesses, and overriding a hand-placed elbow with a synthetic
    one would throw away the better measurement to keep the worse rule.
    """
    marks = confidence or {}
    resolved = dict(pose)
    for side in ["l", "r"]:
        interior = ["elbow_" + side, "wrist_" + side]
        if all(marks.get(joint, "estimated") == "observed" for joint in interior):
            continue
        shoulder = pose["shoulder_" + side]
        paw = pose["paw_" + side]
        span = [paw[0] - shoulder[0], paw[1] - shoulder[1]]
        length = math.hypot(*span)
        if length <= 1e-6:
            raise ValueError(f"collapsed arm on side {side}")
        bow = [-span[1] / length, span[0] / length]
        if bow[1] < 0:
            bow = [-bow[0], -bow[1]]
        for name, along, out in [("elbow_", 0.45, 0.15), ("wrist_", 0.78, 0.06)]:
            if marks.get(name + side, "estimated") == "observed":
                continue
            resolved[name + side] = [shoulder[0] + span[0] * along + bow[0] * out * length,
                                     shoulder[1] + span[1] * along + bow[1] * out * length]
    return resolved


def grid_frame(pose, ground_y):
    """Origin, cell size and anatomy rows for one character's normalised grid.

    The cell is a twelfth of this character's own standing height, so both
    characters occupy the same number of cells and the analogy reads at a glance.
    Normalising on leg length instead was tried first: it makes a stride of three
    cells mean the same distance on both bodies, but the mouse's short legs and
    large head then make it twice as many cells tall as the human reference, and
    the two rows of the card no longer look comparable. The proportion difference
    is not lost by this choice, it moves onto the hip and neck lines, which sit at
    visibly different rows on the two characters.
    """
    hip_x, hip_y = pose["hip_c"]
    top = min(pose[name][1] for name in ["head_top", "ear_l", "ear_r"] if name in pose)
    height = ground_y - top
    if height <= 0 or ground_y <= hip_y:
        raise ValueError("the character must stand above its ground row")
    return {
        "origin": [float(hip_x), float(ground_y)],
        "cell": height / BODY_CELLS,
        "rows": {"ground": float(ground_y), "hip_c": float(hip_y),
                 "neck": float(pose["neck"][1]), "head_top": float(top)},
    }


def cycle_anchor(poses):
    """A standing-equivalent pose measured from the run itself.

    The grid needs one hip row, neck row and body top per character. Taking them
    from a separate standing sprite meant the grid depended on a frame nobody
    chose, and on this sheet that frame stands in a wide idle stance unlike
    anything in the run. Medians over the cycle describe the running body, which
    is the body being drawn.
    """
    if not poses:
        raise ValueError("a cycle needs at least one pose")

    def median(values):
        ordered = sorted(values)
        middle = len(ordered) // 2
        if len(ordered) % 2:
            return float(ordered[middle])
        return (ordered[middle - 1] + ordered[middle]) / 2.0

    anchor = {"hip_c": [median([pose["hip_c"][0] for pose in poses]),
                        median([pose["hip_c"][1] for pose in poses])],
              "neck": [median([pose["neck"][0] for pose in poses]),
                       median([pose["neck"][1] for pose in poses])]}
    tops = ["head_top", "ear_l", "ear_r"]
    anchor["head_top"] = [anchor["neck"][0],
                          min(pose[name][1] for pose in poses
                              for name in tops if name in pose)]
    return anchor


def viewport(frame):
    """Source-pixel origin and scale placing this character's grid on PANEL."""
    cell = frame["cell"]
    if cell <= 0:
        raise ValueError("grid cell collapsed")
    origin_x, origin_y = frame["origin"]
    return (origin_x - COLUMNS * cell, origin_y - ROWS_UP * cell, CELL_PIXELS / cell)


def draw_grid(draw, frame, view):
    """Cell lines land on exact panel multiples, so both panels register."""
    width, height = PANEL
    for column in range(0, width + 1, CELL_PIXELS):
        draw.line([(column, 0), (column, height)], fill=GRID)
    for row in range(0, height + 1, CELL_PIXELS):
        draw.line([(0, row), (width, row)], fill=GRID)
    source_x, source_y, scale = view
    for name in ANATOMY_ROWS:
        y = (frame["rows"][name] - source_y) * scale
        draw.line([(0, y), (width, y)], fill=ANATOMY)
    draw.line([(COLUMNS * CELL_PIXELS, 0), (COLUMNS * CELL_PIXELS, height)], fill=ANATOMY)


def confidence_of(frame):
    """Per-joint confidence, defaulting to estimated for anything unmarked.

    Absence means estimated on purpose. A joint nobody has claimed to have seen
    is a guess, and the trace this replaces proved how easily a guess passes for
    a measurement once it is drawn as a solid line.
    """
    marked = frame.get("confidence", {})
    return {joint: marked.get(joint, "estimated") for joint in frame["pose"]}


def dashed(draw, start, end, colour, width):
    """A bone with at least one guessed end, drawn so it cannot pass for traced."""
    length = math.dist(start, end)
    if length <= 1e-6:
        return
    step = max(6.0, width * 2.5)
    steps = max(1, int(length / step))
    for index in range(steps):
        if index % 2:
            continue
        head = index / steps
        tail = min(1.0, (index + 1) / steps)
        draw.line([(start[0] + (end[0] - start[0]) * head,
                    start[1] + (end[1] - start[1]) * head),
                   (start[0] + (end[0] - start[0]) * tail,
                    start[1] + (end[1] - start[1]) * tail)], fill=colour, width=width)


def skeleton_layer(pose, view, support, half_width, extra_bones=(), confidence=None):
    """Far limbs, then a translucent torso, then near limbs.

    The torso sits between the two limb sets so the far limb reads as passing
    behind the body. Depth is stated here rather than left to the model, because
    the isolated-leg trial showed outline control alone does not settle which way
    a limb faces.
    """
    source_x, source_y, scale = view
    layer = Image.new("RGBA", PANEL, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    def point(name):
        x, y = pose[name]
        return ((x - source_x) * scale, (y - source_y) * scale)

    marks = confidence or {}

    def guessed(name):
        return marks.get(name, "observed") == "estimated"

    def limb(bones, colour, width):
        for start, end in bones:
            if guessed(start) or guessed(end):
                dashed(draw, point(start), point(end), colour + (190,), width)
            else:
                draw.line([point(start), point(end)], fill=colour + (255,), width=width)
        for _, end in bones:
            x, y = point(end)
            box = [x - width, y - width, x + width, y + width]
            if guessed(end):
                draw.ellipse(box, fill=(255, 255, 255, 235), outline=colour + (255,), width=2)
            else:
                draw.ellipse(box, fill=colour + (255,))

    thickness = max(3, CELL_PIXELS // 7)
    limb(FAR_BONES, FAR, thickness)
    hip, neck = point("hip_c"), point("neck")
    half = half_width * scale
    torso = Image.new("RGBA", PANEL, (0, 0, 0, 0))
    ImageDraw.Draw(torso).polygon(
        [(hip[0] - half, hip[1]), (hip[0] + half, hip[1]),
         (neck[0] + half, neck[1]), (neck[0] - half, neck[1])],
        fill=(255, 255, 255, 96), outline=SPINE + (255,))
    head = [point(name) for name in HEAD_JOINTS if name in pose]
    if len(head) > 1:
        centre = (sum(x for x, _ in head) / len(head), sum(y for _, y in head) / len(head))
        radius = max(math.dist(centre, corner) for corner in head)
        disc = Image.new("RGBA", PANEL, (0, 0, 0, 0))
        ImageDraw.Draw(disc).ellipse(
            [centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius],
            fill=(255, 255, 255, 96), outline=SPINE + (255,))
        torso = Image.alpha_composite(torso, disc)
    layer = Image.alpha_composite(layer, torso)
    draw = ImageDraw.Draw(layer)
    tail = [bone for bone in extra_bones if bone in TAIL_BONES and bone[1] in pose]
    for start, end in tail:
        draw.line([point(start), point(end)], fill=TAIL + (255,), width=thickness)
    others = [bone for bone in extra_bones if bone not in TAIL_BONES and bone[1] in pose]
    for start, end in list(SPINE_BONES) + others:
        if guessed(start) or guessed(end):
            dashed(draw, point(start), point(end), SPINE + (190,), thickness)
        else:
            draw.line([point(start), point(end)], fill=SPINE + (255,), width=thickness)
    limb(NEAR_BONES, NEAR, thickness)
    if support in ("near", "far"):
        x, y = point("toe_" + ("l" if support == "near" else "r"))
        radius = CELL_PIXELS // 2
        draw.ellipse([x - radius, y - radius, x + radius, y + radius],
                     outline=SUPPORT + (255,), width=3)
    return layer


def fit_leg_scale(rest, normaliser, ground_y):
    """One cycle-wide leg scale, set so the most extended pose stands on its own hip row.

    transfer() derives its leg scale from whichever trace pose it is handed, and
    the mouse's bind drawing already has bent legs: its hip-to-ground distance of
    36px understates a leg chain that measures 46.6px. Handing over the raw trace
    therefore straightens the legs past the mouse's own stature and the grounded
    hip floats about 30px above its hip row. Scaling the handed-over pose by a
    single factor pins the extended frame to the hip row instead, and the same
    factor serves every frame so relative foreshortening survives.
    """
    if normaliser["support"] == "flight":
        raise ValueError("the normalising frame must have a support foot")
    target = rest["hip_c"][1]
    side = "l" if normaliser["support"] == "near" else "r"
    low, high = 0.25, 4.0

    def grounded_hip(factor):
        scaled = {name: [x * factor, y * factor] for name, (x, y) in normaliser["pose"].items()}
        posed = transfer(rest, normaliser["pose"], scaled)
        return posed["hip_c"][1] + (ground_y - posed["toe_" + side][1])

    if not grounded_hip(low) < target < grounded_hip(high):
        raise ValueError("the mouse's hip row is unreachable by any single leg scale")
    for _ in range(64):
        middle = (low + high) / 2.0
        if grounded_hip(middle) < target:
            low = middle
        else:
            high = middle
    return (low + high) / 2.0


def foot_contact(pose, side):
    """Whichever of ankle or toe reaches lower, which is what meets the ground.

    Seating the toe alone buries the heel of a heel-down foot: one traced support
    pose put its ankle five pixels under the floor that way.
    """
    return max(pose["ankle_" + side][1], pose["toe_" + side][1])


def grounded(poses, supports, ground_y):
    """Seat every support foot on one ground row; lift the flight frames clear."""
    shifts = []
    for pose, support in zip(poses, supports, strict=True):
        if support == "flight":
            shifts.append(None)
            continue
        shifts.append(ground_y - foot_contact(pose, "l" if support == "near" else "r"))
    count = len(shifts)
    for index, shift in enumerate(shifts):
        if shift is not None:
            continue
        neighbours = [shifts[(index - 1) % count], shifts[(index + 1) % count]]
        if any(value is None for value in neighbours):
            raise ValueError("a flight frame needs supported neighbours")
        shifts[index] = min(neighbours) - 4
    return [{name: [x, y + shift] for name, (x, y) in pose.items()}
            for pose, shift in zip(poses, shifts, strict=True)]


def carry_confidence(marks):
    """Confidence for a transferred mouse pose.

    A transferred joint is only as sound as the reference joints whose directions
    produced it, so names shared with the reference inherit directly. The mouse's
    own head, tail and chest joints ride the torso and head turns, which come from
    hip_c and neck, so they inherit from those rather than claiming to be observed.
    """
    carried = dict(marks)
    for joint, source in [("chest", "hip_c"), ("tail_base", "hip_c"), ("tail_tip", "hip_c"),
                          ("ear_l", "neck"), ("ear_r", "neck"), ("nose", "neck")]:
        carried[joint] = marks.get(source, "estimated")
    return carried


def nearest_to_rest(posed, rest, candidates):
    """Which requested frame sits closest to the mouse's own drawing.

    Exactly one mouse card carries the character artwork; the rest are skeleton
    and grid alone. Putting the artwork on the pose least unlike the bind drawing
    makes the link between the two easiest to read, so the other four cards are
    understood as the same character in a different pose rather than as four
    unrelated requests.
    """
    shared = [joint for joint in rest if joint in posed[candidates[0]]]
    return min(candidates,
               key=lambda index: sum(math.dist(posed[index][joint], rest[joint])
                                     for joint in shared))


def hip_bob(posed, rest, ground_y, trace):
    """How far the hip rises and falls, against the reference's own figure.

    Both are divided by that character's standing hip height so the two are
    comparable. They do not currently match: transferring scaled joint distances
    preserves the reference's angles, not its hip height, and the mouse's bind
    legs are far more bent than the reference's, so straightening them lifts its
    hip proportionally further. No single leg scale closes this — a sweep from
    0.8 to 3.0 bottoms out at 0.467 against the reference's 0.316. Closing it
    needs leg IK: take the hip height from the reference, plant the foot, and
    solve the knee. Recorded here so the gap is a tracked number.
    """
    heights = [ground_y - pose["hip_c"][1] for pose in posed]
    standing = ground_y - rest["hip_c"][1]
    reference_hips = [frame["pose"]["hip_c"][1] for frame in trace["frames"]]
    reference_standing = trace["ground_y"] - trace["neutral"]["pose"]["hip_c"][1]
    return {
        "ratio": (max(heights) - min(heights)) / standing,
        "reference_ratio": (max(reference_hips) - min(reference_hips)) / reference_standing,
        "range": [min(heights), max(heights)],
        "standing": standing,
    }


def panel(art, pose, frame, support, label, half_width, extra_bones=(), confidence=None):
    view = viewport(frame)
    source_x, source_y, scale = view
    tile = Image.new("RGBA", PANEL, PAPER + (255,))
    if art is not None:
        scaled = art.resize((max(1, round(art.width * scale)), max(1, round(art.height * scale))),
                            Image.NEAREST)
        tile.alpha_composite(scaled, (round(-source_x * scale), round(-source_y * scale)))
    draw_grid(ImageDraw.Draw(tile), frame, view)
    tile = Image.alpha_composite(
        tile, skeleton_layer(pose, view, support, half_width, extra_bones, confidence))
    return titled(tile.convert("RGB"), label)


def shoulder_half_width(pose):
    """Torso half-width from the character's own shoulder span."""
    return abs(pose["shoulder_l"][0] - pose["shoulder_r"][0]) / 2.0


def titled(tile, label):
    out = Image.new("RGB", (tile.width + 8, tile.height + 26), (255, 255, 255))
    out.paste(tile, (4, 4))
    ImageDraw.Draw(out).text((6, tile.height + 10), label, fill=(20, 20, 20))
    return out


def card(top_left, top_right, bottom_left, bottom_right, caption):
    width = top_left.width + top_right.width
    height = top_left.height + bottom_left.height
    sheet = Image.new("RGB", (width + 16, height + 40), (255, 255, 255))
    sheet.paste(top_left, (8, 8))
    sheet.paste(top_right, (8 + top_left.width, 8))
    sheet.paste(bottom_left, (8, 8 + top_left.height))
    sheet.paste(bottom_right, (8 + bottom_left.width, 8 + top_left.height))
    draw = ImageDraw.Draw(sheet)
    draw.line([(8, 6 + top_left.height), (width + 8, 6 + top_left.height)], fill=(175, 175, 188))
    draw.line([(6 + top_left.width, 8), (6 + top_left.width, height + 8)], fill=(175, 175, 188))
    draw.text((8, height + 18), caption, fill=(20, 20, 20))
    return sheet


def contact_sheet(tiles):
    sheet = Image.new("RGB", (tiles[0].width * len(tiles), tiles[0].height), (255, 255, 255))
    for position, tile in enumerate(tiles):
        sheet.paste(tile, (tile.width * position, 0))
    return sheet


def build(args):
    trace = json.loads(args.trace.read_text())
    document = json.loads(args.document.read_text())
    rest = document["rest_pose"]
    strip = Image.open(args.reference_strip).convert("RGBA")
    neutral_cell = Image.open(args.reference_neutral).convert("RGBA")
    art = Image.open(args.character_art).convert("RGBA")

    frames = trace["frames"]
    supports = [frame["support"] for frame in frames]
    marks = [confidence_of(frame) for frame in frames]
    poses = [derive_arms(frame["pose"], mark) for frame, mark in zip(frames, marks, strict=True)]
    reference_grid = grid_frame(cycle_anchor(poses), trace["ground_y"])
    mouse_grid = grid_frame(rest, MOUSE_GROUND_Y)
    normaliser_index = max((index for index, frame in enumerate(frames)
                            if frame["support"] != "flight"),
                           key=lambda index: total_leg_length(poses[index]))
    normaliser = {"name": frames[normaliser_index]["name"],
                  "support": supports[normaliser_index],
                  "pose": poses[normaliser_index]}
    factor = fit_leg_scale(rest, normaliser, MOUSE_GROUND_Y)
    scaled = {name: [x * factor, y * factor] for name, (x, y) in normaliser["pose"].items()}
    posed = [transfer(rest, pose, scaled) for pose in poses]
    posed = grounded(posed, supports, MOUSE_GROUND_Y)

    reference_half = max(shoulder_half_width(pose) for pose in poses)
    mouse_half = shoulder_half_width(rest)

    args.output.mkdir(parents=True, exist_ok=True)
    request = args.output / "request"
    request.mkdir(parents=True, exist_ok=True)

    written = {}
    reference_tiles, target_tiles = [], []
    for index, frame in enumerate(frames):
        cell = strip.crop((CELL * index, 0, CELL * (index + 1), CELL))
        reference_tiles.append(panel(cell, poses[index], reference_grid, frame["support"],
                                     f"reference {frame['name']}  ({frame['support']})",
                                     reference_half, confidence=marks[index]))
        target_tiles.append(panel(None, posed[index], mouse_grid, frame["support"],
                                  f"mouse {frame['name']}  ({frame['support']})",
                                  mouse_half, MOUSE_BONES,
                                  confidence=carry_confidence(marks[index])))

    for name, tiles in [("trace-check.png", reference_tiles),
                        ("target-skeletons.png", target_tiles)]:
        contact_sheet(tiles).save(args.output / name)
        written[name] = sha((args.output / name).read_bytes())

    chosen = [index for index in args.request_frames if 0 <= index < len(frames)]
    if len(chosen) != len(args.request_frames):
        raise ValueError("a requested frame index falls outside the cycle")
    anchor_index = nearest_to_rest(posed, rest, chosen)
    for index in chosen:
        frame = frames[index]
        cell = strip.crop((CELL * index, 0, CELL * (index + 1), CELL))
        reference_path = request / f"reference-{frame['name']}.png"
        panel(cell, poses[index], reference_grid, frame["support"],
              f"reference {frame['name']}  ({frame['support']})",
              reference_half, confidence=marks[index]).save(reference_path)
        written[f"request/{reference_path.name}"] = sha(reference_path.read_bytes())

        carries_art = index == anchor_index
        label = (f"mouse {frame['name']} - artwork shows the character, "
                 "the skeleton shows the pose" if carries_art
                 else f"mouse {frame['name']}  ({frame['support']})")
        mouse_path = request / f"mouse-{frame['name']}.png"
        panel(art if carries_art else None, posed[index], mouse_grid, frame["support"],
              label, mouse_half, MOUSE_BONES,
              confidence=carry_confidence(marks[index])).save(mouse_path)
        written[f"request/{mouse_path.name}"] = sha(mouse_path.read_bytes())

    # Optional eleventh image. On the artwork card above, the drawing sits in its
    # bind pose while the skeleton holds the requested pose, so the two visibly
    # disagree and the artwork can be misread as the pose. Here they agree, which
    # is what teaches the skeleton-to-artwork correspondence.
    character = request / "character-reference.png"
    panel(art, rest, mouse_grid, "both",
          "the character and its own skeleton, not a pose request",
          mouse_half, MOUSE_BONES).save(character)
    written[f"request/{character.name}"] = sha(character.read_bytes())

    manifest = {
        "generated_by": "scripts/prepare_pose_analogy.py",
        "submitted": False,
        "panel": list(PANEL),
        "window_cells": {"columns": COLUMNS, "rows_up": ROWS_UP, "rows_down": ROWS_DOWN},
        "reference_grid": reference_grid,
        "mouse_grid": mouse_grid,
        "mouse_ground_y": MOUSE_GROUND_Y,
        "leg_scale_normaliser": normaliser["name"],
        "leg_scale_factor": factor,
        "hip_bob": hip_bob(posed, rest, MOUSE_GROUND_Y, trace),
        "request_frames": [frames[index]["name"] for index in chosen],
        "request_supports": [supports[index] for index in chosen],
        "artwork_card": frames[anchor_index]["name"],
        "supports": supports,
        "posed": {frame["name"]: pose for frame, pose in zip(frames, posed, strict=True)},
        "input_sha256": {
            path.name: sha(path.read_bytes())
            for path in [args.trace, args.document, args.reference_strip,
                         args.reference_neutral, args.character_art]
        },
        "output_sha256": written,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {len(written)} images and a manifest to {args.output}")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--trace", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-trace-v1.json")
    parser.add_argument("--reference-strip", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-10.png")
    parser.add_argument("--reference-neutral", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-neutral.png")
    parser.add_argument("--document", type=Path,
                        default=root / "experiments/character_binding/puppet_documents/mouse_run_reference_v2.json")
    parser.add_argument("--character-art", type=Path,
                        default=root / "experiments/character_binding/inputs/interactive-run-source-v1.png")
    parser.add_argument("--output", type=Path,
                        default=root / "experiments/pose_analogy/evidence/analogy-inputs-v1")
    parser.add_argument("--request-frames", type=int, nargs="+", default=[0, 2, 4, 6, 8],
                        help="Zero-based frames to build a submission set for. The default "
                             "samples every other frame, covering near support, far support "
                             "and one flight phase.")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
