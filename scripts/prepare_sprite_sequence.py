"""Build a provider-free profile-control experiment around C++ leg geometry.

The original pose, bindings and earlier evidence are read-only inputs. Pillow
only rasterizes the C++ polygons and assembles fixed-coordinate review images.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from scripts.prepare_boot_view_guides import render_boot


ROOT = Path(__file__).resolve().parents[1]
CHARACTER = ROOT / "experiments/character_binding"
DOCUMENT = CHARACTER / "puppet_documents/mouse_run_reference_v2.json"
TRACE = CHARACTER / "inputs/run-pose-trace-v1.json"
SOURCE = CHARACTER / "inputs/interactive-run-source-v1.png"
SHEET = CHARACTER / "inputs/run-pose-reference-12.png"
RENDER = ROOT / "experiments/sprite_sequence/inputs/baseline-v1"
LANDMARKS = ROOT / "experiments/sprite_sequence/heel-landmarks-v1.json"
CROP = (48, 80, 224, 256)
CELL = 352
COLORS = {"near": (223, 109, 30), "far": (36, 134, 205)}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def font(size=16):
    path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    return ImageFont.truetype(str(path), size) if path.exists() else ImageFont.load_default()


def geometry_rows(document, trace, landmarks):
    names = [frame["name"] for frame in document["frames"]]
    if len(names) != 12 or len(set(names)) != 12:
        raise ValueError("requires twelve distinct frames")
    if any([frame["name"] for frame in item["frames"]] != names for item in (trace, landmarks)):
        raise ValueError("document, trace and landmarks must have the same ordered frames")
    if landmarks["version"] != 1 or landmarks["view"] != "profile":
        raise ValueError("only profile landmark version 1 is supported")
    rows = []
    for frame, source, mark in zip(document["frames"], trace["frames"], landmarks["frames"], strict=True):
        for side, suffix in (("far", "r"), ("near", "l")):
            pose, ref = frame["pose"], source["pose"]
            points = [pose["hip_c"], pose["knee_" + suffix], pose["ankle_" + suffix], pose["toe_" + suffix],
                      ref["ankle_" + suffix], mark[side], ref["toe_" + suffix]]
            rows.append(" ".join([frame["name"], side] + [str(value) for point in points for value in point] + ["12", "11"]))
    return "\n".join(rows) + "\n"


def line(draw, points, fill, width):
    draw.line([tuple(point) for point in points], fill=fill, width=width, joint="curve")


def capsule(draw, a, b, width, fill):
    line(draw, [a, b], fill, width)
    for x, y in (a, b):
        r = width / 2
        draw.ellipse((x-r, y-r, x+r, y+r), fill=fill)


def leg_image(leg):
    image = Image.new("RGBA", (256, 256))
    draw = ImageDraw.Draw(image)
    capsule(draw, leg["hip"], leg["knee"], 15, (58, 49, 38, 255))
    capsule(draw, leg["knee"], leg["cuff"], 13, (70, 59, 43, 255))
    mask = Image.new("L", image.size)
    mask_draw = ImageDraw.Draw(mask)
    for key in ("foot", "shaft"):
        mask_draw.polygon([tuple(point) for point in leg[key]], fill=255)
    # Join shaft and foot at the invariant ankle; only the outer contour is inked.
    a = leg["ankle"]
    mask_draw.ellipse((a[0]-4, a[1]-4, a[0]+4, a[1]+4), fill=255)
    rim = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(3)))
    leather = Image.new("RGBA", image.size, (129, 71, 39, 0))
    leather.putalpha(mask)
    edge = Image.new("RGBA", image.size, (57, 32, 23, 0))
    edge.putalpha(rim)
    leather.alpha_composite(edge)
    ink = ImageDraw.Draw(leather)
    line(ink, [leg["heel"], leg["toe"]], (48, 32, 24, 255), 2)
    line(ink, leg["shaft"][1:3], (173, 108, 56, 255), 2)
    image.alpha_composite(leather)
    return image


def white(image):
    result = Image.new("RGBA", image.size, "white")
    result.alpha_composite(image)
    return result.convert("RGB")


def overlay(image, legs):
    image = white(image).copy()
    draw = ImageDraw.Draw(image)
    for leg in legs:
        color = COLORS[leg["side"]]
        line(draw, [leg[key] for key in ("hip", "knee", "ankle", "toe")], color, 2)
        line(draw, [leg["ankle"], leg["heel"], leg["toe"]], (176, 49, 128), 2)
        for key in ("knee", "ankle", "heel", "toe"):
            x, y = leg[key]
            draw.ellipse((x-2, y-2, x+2, y+2), fill="white", outline=color)
    return image


def make_grid(images, labels, columns, cell=(256, 284)):
    rows = (len(images) + columns - 1) // columns
    grid = Image.new("RGB", (columns * cell[0], rows * cell[1]), (244, 242, 237))
    draw = ImageDraw.Draw(grid)
    for index, (im, label) in enumerate(zip(images, labels, strict=True)):
        x, y = index % columns * cell[0], index // columns * cell[1]
        grid.paste(im, (x, y + 28))
        draw.text((x + 6, y + 6), label, fill=(30, 35, 39), font=font(15))
    return grid


def audit(document, trace, geometry):
    old = json.loads((CHARACTER / "evidence/boot-view-guides-v2/manifest.json").read_text())
    config = json.loads((CHARACTER / "inputs/boot-view-guide-v2.json").read_text())
    records = []
    for frame, traced, guide in zip(document["frames"], trace["frames"], old["frames"], strict=True):
        for side, suffix in (("near", "l"), ("far", "r")):
            pins = guide["annotations"][side]
            _, _, legacy = render_boot(pins["heel"], pins["toe"], config, frame["pose"]["knee_" + suffix])
            leg = next(item for item in geometry if item["name"] == frame["name"] and item["side"] == side)
            errors = {}
            for start, end in (("hip_c", "knee_" + suffix), ("knee_" + suffix, "ankle_" + suffix),
                               ("ankle_" + suffix, "toe_" + suffix)):
                def angle(pose):
                    return math.atan2(pose[end][1]-pose[start][1], pose[end][0]-pose[start][0])
                difference = angle(frame["pose"]) - angle(traced["pose"])
                errors[start + "->" + end] = abs(math.degrees(math.atan2(math.sin(difference), math.cos(difference))))
            records.append({"name": frame["name"], "side": side,
                            "legacy_ankle": legacy["ankle"], "target_ankle": leg["ankle"],
                            "legacy_ankle_error_px": math.dist(legacy["ankle"], leg["ankle"]),
                            "new_ankle_error_px": math.dist(leg["ankle"], frame["pose"]["ankle_" + suffix]),
                            "new_toe_error_px": math.dist(leg["toe"], frame["pose"]["toe_" + suffix]),
                            "trace_direction_error_degrees": errors})
    return records


def prepare(binary, output):
    if output.exists():
        raise ValueError("output exists; retain evidence and choose a new revision")
    document, trace, landmarks = [json.loads(path.read_text()) for path in (DOCUMENT, TRACE, LANDMARKS)]
    rows = geometry_rows(document, trace, landmarks)
    completed = subprocess.run([str(binary.resolve())], input=rows, text=True, capture_output=True, check=True)
    geometry = json.loads(completed.stdout)
    records = audit(document, trace, geometry)
    source_sheet = Image.open(SHEET).convert("RGB")
    frames, overlays, source_views, comparisons = [], [], [], []
    isolated = {}
    for index, frame in enumerate(document["frames"]):
        name = frame["name"]
        legs = [item for item in geometry if item["name"] == name]
        leg_parts = {leg["side"]: leg_image(leg) for leg in legs}
        isolated[name] = leg_parts
        composite = Image.new("RGBA", (256, 256))
        for part_name in frame["draw_order"]:
            if part_name in ("near_leg", "far_leg"):
                part = leg_parts[part_name.split("_")[0]]
            elif part_name in ("near_boot", "far_boot"):
                continue
            else:
                part = Image.open(RENDER / "part-poses" / part_name / (name + ".png")).convert("RGBA")
            composite.alpha_composite(part)
        frames.append(composite)
        overlays.append(overlay(composite, legs))
        left, top, width, height = trace["frames"][index]["cell"]
        source = source_sheet.crop((left, top, left+width, top+height))
        draw = ImageDraw.Draw(source)
        for side, suffix in (("near", "l"), ("far", "r")):
            pose = trace["frames"][index]["pose"]
            line(draw, [pose["hip_c"], pose["knee_" + suffix], pose["ankle_" + suffix], pose["toe_" + suffix]], COLORS[side], 2)
            line(draw, [pose["ankle_" + suffix], landmarks["frames"][index][side], pose["toe_" + suffix]], (176, 49, 128), 2)
        source.thumbnail((210, 256), Image.Resampling.LANCZOS)
        panel = Image.new("RGB", (256, 256), "white")
        panel.paste(source, (20, 0))
        source_views.append(panel)
        old_image = Image.open(CHARACTER / "evidence/boot-view-guides-v2" / name / "geometry.png").convert("RGB")
        draw = ImageDraw.Draw(old_image)
        for record in records:
            if record["name"] != name:
                continue
            line(draw, [record["legacy_ankle"], record["target_ankle"]], (230, 20, 110), 2)
            for point, color in ((record["legacy_ankle"], "red"), (record["target_ankle"], "cyan")):
                x, y = point
                draw.ellipse((x-2, y-2, x+2, y+2), fill=color)
        comparisons.extend([old_image, overlays[-1]])
    output.mkdir(parents=True)
    (output / "geometry-input.txt").write_text(rows)
    write_json(output / "geometry.json", geometry)
    write_json(output / "binding-audit.json", records)
    for name, parts in isolated.items():
        directory = output / "parts" / name
        directory.mkdir(parents=True)
        for side, part in parts.items():
            part.save(directory / (side + ".png"))
    for name, items in (("frames", frames), ("overlays", overlays)):
        (output / name).mkdir()
        for frame, im in zip(document["frames"], items, strict=True):
            im.save(output / name / (frame["name"] + ".png"))
    labels = [frame["name"] for frame in document["frames"]]
    make_grid([white(im) for im in frames], labels, 6).save(output / "cycle.png")
    make_grid(source_views, labels, 6).save(output / "source-landmarks.png")
    make_grid(overlays, labels, 6).save(output / "pose-overlays.png")
    make_grid(comparisons, [name + suffix for name in labels for suffix in (" legacy", " profile")], 6).save(output / "binding-comparison.png")
    for size in (48, 96, 256):
        sequence = [white(im).resize((size, size), Image.Resampling.NEAREST) for im in frames]
        sequence[0].save(output / f"cycle-{size}.png", save_all=True, append_images=sequence[1:], duration=1000/12, loop=0, disposal=0, blend=0)
    sheets = {}
    for group, side, start in (("sheet", "near", 7), ("near-01-06", "near", 1),
                               ("far-01-06", "far", 1), ("far-07-12", "far", 7)):
        targets = Image.new("RGB", (CELL * 3, CELL * 2), "white")
        mapping = []
        for index in range(6):
            name = f"reference_{index+start:02}"
            cell = white(isolated[name][side]).crop(CROP).resize((CELL, CELL), Image.Resampling.NEAREST)
            x, y = index % 3 * CELL, index // 3 * CELL
            targets.paste(cell, (x, y))
            mapping.append({"name": name, "side": side, "cell": [x, y, CELL, CELL], "world_crop": list(CROP)})
        targets.save(output / (group + "-input.png"))
        sheets[group] = mapping
    targets = Image.open(output / "sheet-input.png").convert("RGB")
    single = Image.new("RGB", targets.size, "white")
    single.paste(targets.crop((0, CELL, CELL, CELL*2)), (0, CELL))
    single.save(output / "single-input.png")
    white(Image.open(SOURCE).convert("RGBA")).resize((768, 768), Image.Resampling.NEAREST).save(output / "identity.png")
    manifest = {"version": 1, "status": "prepared inputs; generation results tracked separately", "provider_calls_by_preparer": 0,
                "input_hashes": {str(path.relative_to(ROOT)): digest(path) for path in (DOCUMENT, TRACE, LANDMARKS, SOURCE, SHEET, Path(__file__), ROOT / "scripts/sprite_sequence_geometry.cc")},
                "render_input_hashes": {str(path.relative_to(ROOT)): digest(path) for path in sorted((RENDER / "part-poses").glob("*/*.png"))},
                "legacy_geometry_inputs": {str(path.relative_to(ROOT)): digest(path) for path in (CHARACTER / "evidence/boot-view-guides-v2/manifest.json", CHARACTER / "inputs/boot-view-guide-v2.json", ROOT / "scripts/prepare_boot_view_guides.py")},
                "geometry_binary_sha256": digest(binary), "generation_inputs": {name: digest(output / name) for name in ("sheet-input.png", "single-input.png", "identity.png", "near-01-06-input.png", "far-01-06-input.png", "far-07-12-input.png")},
                "mapping": sheets["sheet"], "sheets": sheets, "fps": 12, "working_canvas": [256, 256], "review_sizes": [48, 96, 256],
                "world_registration": "original posed joints; no local fitting, no new grounding or centering",
                "limitation": "Profile-only diagnostic, estimated heels, existing ground calibration. Does not solve contact sole visibility, final style, cloth or binding hidden by source artwork."}
    write_json(output / "manifest.json", manifest)
    print(f"Prepared {len(frames)} frames and 24 connected leg controls at {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.geometry, args.output)


if __name__ == "__main__":
    main()
