"""Prepare/review the bounded posed-mouse cleanup pilot; makes no provider calls.

prepare writes exact image inputs, fixed repair masks, prompts and provenance.
review uses only whole-canvas scaling, then restores every pixel outside the
predeclared repair mask. Raw outputs remain separate from composites.
"""

import argparse
import hashlib
import json
import math
from collections import deque
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


FRAMES = ["reference_01", "reference_07", "reference_10", "reference_12"]
ALL_FRAMES = [f"reference_{index:02d}" for index in range(1, 13)]
MOVING = ["near_arm", "far_arm", "near_leg", "far_leg"]
PROTECTED = ["head", "near_boot", "far_boot", "tail"]
PROMPT = """Use case: precise-object-edit
Asset type: one pixel-art character cutout, one PNG.
Input images: Image 1 is the exact posed image to edit. Image 2 shows the same character's finished texture and costume; use it only for appearance, never its pose. Image 3 is an editing-region guide: white permits repair, black means preserve.
Repair only the white regions in Image 3: join torn sleeve and trouser edges at the shoulders, elbows, hips and knees; remove loose scraps; complete small missing areas of green coat or dark trousers behind the moving limbs. Match the existing pixel clusters, shading and outline style. Preserve the already drawn body geometry and limb positions.
Keep the face, ears, hood, scarf, belt, both boots and tail unchanged. Keep the exact bends, overlapping order, hand locations, foot directions, apparent size and position from Image 1. Keep transparent space between limbs and outside the character. Do not extend the coat into the entire footprint of a moving arm.
Return only the edited Image 1 on its original 1024 x 1024 transparent canvas. Exactly one character, same placement and scale. No text, marks, guide colors, mask, sheet, extra limbs, scenery, shadow, new pose or redesign. Do not copy the pose from Image 2.
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def alpha(path, size=(256, 256)):
    with Image.open(path) as image:
        if image.size != size:
            raise ValueError(f"unexpected canvas: {path}")
        return image.convert("RGBA").getchannel("A").point(lambda value: 255 if value else 0)


def prepare(args):
    existing = args.output / "manifest.json"
    if existing.exists() and json.loads(existing.read_text())["provider_calls"]:
        raise ValueError("cannot replace inputs after provider submission")
    args.output.mkdir(parents=True, exist_ok=True)
    with Image.open(args.identity) as image:
        if image.size != (256, 256):
            raise ValueError("pilot identity must use the 256px working canvas")
        identity = image.convert("RGBA")
        if args.white_matte:
            identity = Image.alpha_composite(Image.new("RGBA", identity.size, "white"), identity)
        identity.resize((1024, 1024), Image.Resampling.NEAREST).save(args.output / "identity.png")
    prompt = PROMPT
    if args.white_matte:
        prompt = prompt.replace("Keep transparent space", "Keep white background space").replace(
            "1024 x 1024 transparent canvas", "1024 x 1024 canvas with a perfectly plain solid white (#FFFFFF) background; no checkerboard or texture")
    names = ALL_FRAMES if args.all_frames else FRAMES
    manifest = {
        "method": "E3 built-in image_gen posed-frame local cleanup",
        "status": "prepared; awaiting provider outputs",
        "input_review": "User accepted current poses as an experiment starting point; foot directions remain a known issue.",
        "provider_calls": 0,
        "document_sha256": digest(args.document),
        "identity_sha256": digest(args.identity),
        "canvas_mapping": "whole-canvas uniform scale only; no per-figure crop, shift, or registration",
        "repair_mask": "arms/legs dilated 3px plus missing leg connections along posed bones outside the coat; protect head, boots, tail, scarf and belt with 1px collar",
        "prompt": prompt,
        "background": "plain white matte" if args.white_matte else "requested transparency",
        "scope": "complete twelve-frame trial" if args.all_frames else "four-frame pilot",
        "frames": [],
    }
    board = Image.new("RGB", (1024, 550 * math.ceil(len(names) / 4)), (237, 237, 234))
    draw = ImageDraw.Draw(board)
    document = json.loads(args.document.read_text())
    rest = document["rest_pose"]
    source_angle = math.atan2(rest["hip_c"][1] - rest["chest"][1], rest["hip_c"][0] - rest["chest"][0])
    for index, name in enumerate(names):
        directory = args.output / name
        directory.mkdir(exist_ok=True)
        source = Image.open(args.render / "working" / (name + ".png")).convert("RGBA")
        allowed = Image.new("L", (256, 256))
        for part in MOVING:
            allowed = ImageChops.lighter(allowed, alpha(args.render / "part-poses" / part / (name + ".png")))
        allowed = allowed.filter(ImageFilter.MaxFilter(7))
        pose = next(frame["pose"] for frame in document["frames"] if frame["name"] == name)
        leg_gaps = Image.new("L", (256, 256))
        leg_draw = ImageDraw.Draw(leg_gaps)
        for side in ["l", "r"]:
            for start, end, width in [("hip_c", "knee_" + side, 12),
                                      ("knee_" + side, "ankle_" + side, 10)]:
                leg_draw.line([tuple(pose[start]), tuple(pose[end])], fill=255, width=width)
        body = alpha(args.render / "part-poses" / "body" / (name + ".png"))
        allowed = ImageChops.lighter(allowed, ImageChops.subtract(leg_gaps, body))
        protected = Image.new("L", (256, 256))
        for part in PROTECTED:
            protected = ImageChops.lighter(protected, alpha(args.render / "part-poses" / part / (name + ".png")))
        turn = math.atan2(pose["hip_c"][1] - pose["chest"][1], pose["hip_c"][0] - pose["chest"][0]) - source_angle
        protected_draw = ImageDraw.Draw(protected)
        # Source-space scarf and belt envelopes follow the rigid body.
        for polygon in [[[136, 103], [170, 108], [165, 134], [141, 126]],
                        [[124, 133], [169, 140], [167, 155], [122, 146]]]:
            placed = []
            for x, y in polygon:
                x, y = x - rest["chest"][0], y - rest["chest"][1]
                placed.append((pose["chest"][0] + x * math.cos(turn) - y * math.sin(turn),
                               pose["chest"][1] + x * math.sin(turn) + y * math.cos(turn)))
            protected_draw.polygon(placed, fill=255)
        protected = protected.filter(ImageFilter.MaxFilter(3))
        allowed = ImageChops.subtract(allowed, protected)
        source.save(directory / "source.png")
        allowed.save(directory / "repair-mask.png")
        edit_input = Image.alpha_composite(Image.new("RGBA", source.size, "white"), source) if args.white_matte else source
        edit_input.resize((1024, 1024), Image.Resampling.NEAREST).save(directory / "edit-input.png")
        allowed.resize((1024, 1024), Image.Resampling.NEAREST).save(directory / "region-guide.png")
        (directory / "prompt.txt").write_text(prompt)
        tint = Image.new("RGBA", source.size, (241, 124, 34, 0))
        tint.putalpha(allowed.point(lambda value: 100 if value else 0))
        tinted = Image.alpha_composite(source, tint)
        left, top = (index % 4) * 256, (index // 4) * 550
        board.paste(edit_input, (left, top + 25), edit_input)
        board.paste(tinted, (left, top + 290), tinted)
        draw.text((left + 12, top + 8), name, fill=(25, 25, 25))
        draw.text((left + 12, top + 278), "Orange: allowed repairs", fill=(25, 25, 25))
        manifest["frames"].append({
            "name": name,
            "status": "not submitted",
            "inputs": [str(directory / "edit-input.png"), str(args.output / "identity.png"), str(directory / "region-guide.png")],
            "input_sha256": [digest(directory / "edit-input.png"), digest(args.output / "identity.png"), digest(directory / "region-guide.png")],
            "repair_mask_sha256": digest(directory / "repair-mask.png"),
            "allowed_pixels": sum(bool(value) for value in allowed.tobytes()),
        })
    board.save(args.output / "input-review.png")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(args.output / "input-review.png")


def extract_neutral_matte(image, tolerance):
    """Remove the observed light achromatic checkerboard; retain RGB verbatim."""
    pixels = []
    for red, green, blue, opacity in image.getdata():
        background = min(red, green, blue) >= 70 and max(red, green, blue) - min(red, green, blue) <= tolerance
        pixels.append((red, green, blue, 0 if background else opacity))
    extracted = Image.new("RGBA", image.size)
    extracted.putdata(pixels)
    return extracted


def extract_white_matte(image, source):
    pixels = list(image.getdata())
    width, height = image.size
    candidate = [min(pixel[:3]) >= 225 and max(pixel[:3]) - min(pixel[:3]) <= 16 for pixel in pixels]
    outside = set()
    pending = deque(index for index in range(len(pixels))
                    if (index < width or index >= width * (height - 1) or index % width in [0, width - 1]) and candidate[index])
    while pending:
        index = pending.popleft()
        if index in outside:
            continue
        outside.add(index)
        x, y = index % width, index // width
        for nx, ny in [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]:
            neighbor = ny * width + nx
            if 0 <= nx < width and 0 <= ny < height and candidate[neighbor] and neighbor not in outside:
                pending.append(neighbor)
    source_alpha = source.getchannel("A").tobytes()
    result = Image.new("RGBA", image.size)
    result.putdata([(r, g, b, 0 if index in outside or (candidate[index] and source_alpha[index] == 0) else a)
                    for index, (r, g, b, a) in enumerate(pixels)])
    return result


def review(args):
    directory = args.output / args.frame
    manifest = json.loads((args.output / "manifest.json").read_text())
    entry = next(frame for frame in manifest["frames"] if frame["name"] == args.frame)
    raw_path = directory / "raw-output.png"
    if not raw_path.exists():
        raise ValueError("retain the raw provider output before reviewing")
    source = Image.open(directory / "source.png").convert("RGBA")
    mask = Image.open(directory / "repair-mask.png").convert("L")
    raw = Image.open(raw_path).convert("RGBA")
    entry["raw_size"] = list(raw.size)
    entry["raw_sha256"] = digest(raw_path)
    if raw.width != raw.height:
        entry["status"] = "unmappable canvas aspect ratio"
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        raise ValueError("pilot only permits uniform square-canvas mapping")
    mapped = raw.resize(source.size, Image.Resampling.NEAREST)
    mapped.save(directory / "canvas-mapped.png")
    suffix = ""
    if args.neutral_matte_cutoff is not None or manifest.get("background") == "plain white matte":
        white = manifest.get("background") == "plain white matte"
        mapped = extract_white_matte(mapped, source) if white else extract_neutral_matte(mapped, args.neutral_matte_cutoff)
        mapped.save(directory / "matte-extracted.png")
        suffix = "-extracted"
        entry["matte_extraction"] = {
            "rule": "connected near-white exterior plus white holes already transparent in the source" if white else "alpha=0 where min(R,G,B)>=70 and max(R,G,B)-min(R,G,B)<=tolerance",
            "tolerance": args.neutral_matte_cutoff,
            "note": "deterministic background extraction; not native provider transparency",
        }
    composite = Image.composite(mapped, source, mask)
    composite.save(directory / ("masked-composite" + suffix + ".png"))
    for image, name in [(source, "baseline-native"), (mapped, "raw-native"), (composite, "composite-native")]:
        image.resize((48, 48), Image.Resampling.NEAREST).save(directory / (name + suffix + ".png"))
    pixels = list(source.getdata())
    new_pixels = list(composite.getdata())
    outside = [i for i, value in enumerate(mask.tobytes()) if value == 0]
    entry["outside_mask_changed_pixels"] = sum(pixels[i] != new_pixels[i] for i in outside)
    entry["changed_pixels"] = sum(a != b for a, b in zip(pixels, new_pixels, strict=True))
    entry["visible_changed_pixels"] = sum(a != b and (a[3] > 0 or b[3] > 0)
                                          for a, b in zip(pixels, new_pixels, strict=True))
    entry["source_bounds"] = list(source.getchannel("A").getbbox())
    entry["extracted_model_bounds"] = list(mapped.getchannel("A").getbbox()) if suffix else None
    entry["raw_alpha_extrema"] = list(raw.getchannel("A").getextrema())
    entry["status"] = "rendered; visual review required"
    board = Image.new("RGB", (768, 336), (237, 237, 234))
    draw = ImageDraw.Draw(board)
    model_label = "Model / background removed" if suffix else "Raw / canvas scale only"
    for i, (label, image) in enumerate([("Input", source), (model_label, mapped), ("Only allowed repairs retained", composite)]):
        draw.text((i * 256 + 8, 5), label, fill=(25, 25, 25))
        board.paste(image, (i * 256, 24), image)
        native = image.resize((48, 48), Image.Resampling.NEAREST)
        board.paste(native, (i * 256 + 104, 284), native)
    board.save(directory / ("comparison" + suffix + ".png"))
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(entry, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["prepare", "review"])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--identity", type=Path)
    parser.add_argument("--document", type=Path)
    parser.add_argument("--render", type=Path)
    parser.add_argument("--frame", choices=ALL_FRAMES)
    parser.add_argument("--neutral-matte-cutoff", type=int)
    parser.add_argument("--all-frames", action="store_true")
    parser.add_argument("--white-matte", action="store_true")
    args = parser.parse_args()
    if args.mode == "prepare":
        if not all([args.identity, args.document, args.render]):
            parser.error("prepare needs --identity, --document, and --render")
        prepare(args)
        return
    if args.frame is None:
        parser.error("review needs --frame")
    review(args)


if __name__ == "__main__":
    main()
