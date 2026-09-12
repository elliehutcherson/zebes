"""Review raw sheet outputs at fixed registration; never fit or clip silhouettes."""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

from scripts.prepare_sprite_sequence import CELL, CROP, DOCUMENT, RENDER, digest, make_grid, white, write_json
from scripts.sprite_material_atlas import SAMPLES, SOURCE_SHA256, samples, textured_leg


def foreground(image, threshold=220):
    red, green, blue = image.convert("RGB").split()
    minimum = ImageChops.darker(ImageChops.darker(red, green), blue)
    return minimum.point(lambda value: 255 if value < threshold else 0)


def mapped_sheet(raw, expected):
    if raw.width * expected[1] != raw.height * expected[0]:
        raise ValueError("raw sheet aspect ratio changed; no anisotropic fitting is allowed")
    return raw.convert("RGB").resize(expected, Image.Resampling.LANCZOS)


def metrics(target, candidate, threshold=220):
    a, b = foreground(target, threshold), foreground(candidate, threshold)
    intersection = sum(ImageChops.darker(a, b).get_flattened_data()) / 255
    union = sum(ImageChops.lighter(a, b).get_flattened_data()) / 255
    def stats(mask):
        coords = [(index % mask.width, index // mask.width) for index, value in enumerate(mask.get_flattened_data()) if value]
        if not coords:
            raise ValueError("empty foreground cannot be scored")
        return len(coords), [sum(point[i] for point in coords) / len(coords) for i in (0, 1)]
    count_a, center_a = stats(a)
    count_b, center_b = stats(b)
    scale = (CROP[2] - CROP[0]) / CELL
    return {"iou": intersection / union, "area_ratio": count_b / count_a,
            "centroid_delta_working_px": [(center_b[i]-center_a[i])*scale for i in (0, 1)],
            "foreground_threshold": threshold}


def world_part(cell):
    crop = cell.convert("RGBA").resize((CROP[2]-CROP[0], CROP[3]-CROP[1]), Image.Resampling.LANCZOS)
    crop.putalpha(foreground(crop))
    result = Image.new("RGBA", (256, 256))
    result.alpha_composite(crop, CROP[:2])
    return result


def review(directory, output, complete=False):
    if output.exists():
        raise ValueError("review exists; preserve it and choose a new directory")
    manifest = json.loads((directory / "manifest.json").read_text())
    for name, expected in manifest["generation_inputs"].items():
        if digest(directory / name) != expected:
            raise ValueError(f"generation input changed: {name}")
    target = Image.open(directory / "sheet-input.png").convert("RGB")
    raw = {kind: Image.open(directory / (kind + "-raw.png")) for kind in ("sheet", "single")}
    mapped = {kind: mapped_sheet(image, target.size) for kind, image in raw.items()}
    records, candidates, paired, pair_labels = [], {}, [], []
    recovery_detail = None
    for index, mapping in enumerate(manifest["mapping"]):
        x, y, width, height = mapping["cell"]
        rect = (x, y, x+width, y+height)
        control = target.crop(rect)
        candidate = mapped["sheet"].crop(rect)
        name = mapping["name"]
        candidates[(name, "near")] = world_part(candidate)
        records.append({"name": name, "side": "near", "kind": "sheet", **metrics(control, candidate),
                        "threshold_sensitivity": [metrics(control, candidate, threshold) for threshold in (180, 200, 240)]})
        # Cyan contour is only a review overlay; saved raw and composite parts are not clipped.
        outline = ImageChops.subtract(foreground(control), foreground(control).filter(ImageFilter.MinFilter(3)))
        overlay = candidate.copy()
        overlay.paste((0, 175, 205), mask=outline)
        paired.extend([control, candidate, overlay])
        pair_labels.extend([name + " control", "raw mapped sheet", "cyan: target contour"])
        if index == 3:
            single = mapped["single"].crop(rect)
            records.append({"name": name, "side": "near", "kind": "single", **metrics(control, single),
                            "threshold_sensitivity": [metrics(control, single, threshold) for threshold in (180, 200, 240)]})
            comparison = make_grid([control, single, candidate], ["Control: pose 10", "Single leg: raw mapped", "Six-pose sheet: raw mapped"], 3, (CELL, CELL+28))
            recovery_detail = make_grid(
                [im.crop((110, 70, 270, 230)).resize((480, 480), Image.Resampling.NEAREST) for im in (control, single, candidate)],
                ["Control: pose 10", "Single leg: raw mapped", "Six-pose sheet: raw mapped"], 3, (480, 508))
    blank_pixels = sum(sum(foreground(mapped["single"].crop((i%3*CELL, i//3*CELL, (i%3+1)*CELL, (i//3+1)*CELL))).get_flattened_data())//255 for i in (0, 1, 2, 4, 5))
    completion_comparisons = []
    completion_labels = []
    if complete:
        for group in ("near-01-06", "far-01-06", "far-07-12"):
            guide = Image.open(directory / (group + "-input.png")).convert("RGB")
            raw[group] = Image.open(directory / (group + "-raw.png"))
            mapped[group] = mapped_sheet(raw[group], guide.size)
            for mapping in manifest["sheets"][group]:
                x, y, width, height = mapping["cell"]
                rect = (x, y, x+width, y+height)
                control, candidate = guide.crop(rect), mapped[group].crop(rect)
                name, side = mapping["name"], mapping["side"]
                if (name, side) in candidates:
                    raise ValueError("duplicate generated leg coverage")
                candidates[(name, side)] = world_part(candidate)
                records.append({"name": name, "side": side, "kind": group, **metrics(control, candidate),
                                "threshold_sensitivity": [metrics(control, candidate, threshold) for threshold in (180, 200, 240)]})
                completion_comparisons.extend([control, candidate])
                completion_labels.extend([name + " " + side + " control", "raw " + group])
        if len(candidates) != 24:
            raise ValueError("complete review requires all 24 generated legs")
    output.mkdir(parents=True)
    comparison.save(output / "recovery-comparison.png")
    recovery_detail.save(output / "recovery-detail.png")
    make_grid(paired, pair_labels, 3, (CELL, CELL+28)).save(output / "sheet-comparison.png")
    if complete:
        make_grid(completion_comparisons, completion_labels, 6, (CELL, CELL+28)).save(output / "completion-comparison.png")
    for kind, image in mapped.items():
        image.save(output / (kind + "-canvas-mapped.png"))
    doc = json.loads(DOCUMENT.read_text())
    hybrid_frames, old_frames, profile_frames, material_frames = [], [], [], []
    leg_frames = {kind: [] for kind in ("old", "profile", "hybrid")}
    material_parts = {}
    if complete:
        if digest(directory / "near-01-06-raw.png") != SOURCE_SHA256:
            raise ValueError("material source changed; revise the calibrated sample recipe")
        atlas = samples(raw["near-01-06"])
        geometry = json.loads((directory / "geometry.json").read_text())
        material_parts = {(leg["name"], leg["side"]): textured_leg(leg, atlas) for leg in geometry}
        leg_frames["material"] = []
        (output / "material").mkdir()
        (output / "material-parts").mkdir()
        for name, sample in atlas.items():
            sample.save(output / (name + "-sample.png"))
    (output / "hybrid").mkdir()
    (output / "generated-parts").mkdir()
    for kind in leg_frames:
        (output / (kind + "-legs")).mkdir()
    for frame in doc["frames"]:
        name = frame["name"]
        old_frames.append(white(Image.open(RENDER / "working" / (name + ".png")).convert("RGBA")))
        profile_frames.append(white(Image.open(directory / "frames" / (name + ".png")).convert("RGBA")))
        composite = Image.new("RGBA", (256, 256))
        material_composite = Image.new("RGBA", (256, 256))
        lower = {kind: Image.new("RGBA", (256, 256)) for kind in leg_frames}
        for part_name in frame["draw_order"]:
            if part_name in ("near_leg", "far_leg", "near_boot", "far_boot"):
                lower["old"].alpha_composite(Image.open(RENDER / "part-poses" / part_name / (name + ".png")).convert("RGBA"))
            if part_name in ("near_boot", "far_boot"):
                continue
            if part_name in ("near_leg", "far_leg"):
                side = part_name.split("_")[0]
                control_part = Image.open(directory / "parts" / name / (side + ".png")).convert("RGBA")
                part = candidates.get((name, side), control_part)
                lower["profile"].alpha_composite(control_part)
                lower["hybrid"].alpha_composite(part)
                if complete:
                    material_composite.alpha_composite(material_parts[(name, side)])
                    lower["material"].alpha_composite(material_parts[(name, side)])
            else:
                part = Image.open(RENDER / "part-poses" / part_name / (name + ".png")).convert("RGBA")
                material_composite.alpha_composite(part)
            composite.alpha_composite(part)
        for side in ("near", "far"):
            if (name, side) in candidates:
                candidates[(name, side)].save(output / "generated-parts" / (name + "-" + side + ".png"))
            if complete:
                material_parts[(name, side)].save(output / "material-parts" / (name + "-" + side + ".png"))
        white(composite).save(output / "hybrid" / (name + ".png"))
        hybrid_frames.append(white(composite))
        if complete:
            white(material_composite).save(output / "material" / (name + ".png"))
            material_frames.append(white(material_composite))
        for kind, im in lower.items():
            white(im).save(output / (kind + "-legs") / (name + ".png"))
            leg_frames[kind].append(white(im))
    labels = [frame["name"] for frame in doc["frames"]]
    make_grid(hybrid_frames, labels, 6).save(output / "hybrid-cycle.png")
    sequences = {"old": old_frames, "profile": profile_frames, "hybrid": hybrid_frames}
    if complete:
        sequences["material"] = material_frames
        make_grid(material_frames, labels, 6).save(output / "material-cycle.png")
    for kind, sequence in sequences.items():
        for size in (48, 96, 256):
            frames = [frame.resize((size, size), Image.Resampling.NEAREST) for frame in sequence]
            frames[0].save(output / f"{kind}-{size}.png", save_all=True, append_images=frames[1:], duration=1000/12, loop=0, disposal=0, blend=0)
    coverage = ("All 24 leg poses use generated appearance in fixed world coordinates. This is a complete twelve-frame profile draft, not accepted final artwork."
                if complete else "Only near-leg poses 7–12 are generated; all other legs are diagnostic controls. This is not a completed artwork cycle.")
    summary = {"status": "profile starting point approved; generated artwork pending review", "provider": "built-in imagegen",
               "provider_request_count": len(raw), "generated_leg_count": len(candidates),
               "raw_sizes": {kind: list(image.size) for kind, image in raw.items()},
               "raw_sha256": {kind: digest(directory / (kind + "-raw.png")) for kind in raw},
               "prompt_sha256": {kind: digest(directory.parent.parent / ((kind if kind in ("sheet", "single") else "completion") + "-prompt.txt")) for kind in raw},
               "input_manifest_sha256": digest(directory / "manifest.json"),
               "review_source_sha256": digest(Path(__file__)),
               "review_template_sha256": digest(Path(__file__).with_name("sprite_sequence_review.html")),
               "normalization": "one uniform whole-canvas scale; fixed grid cells and world crop; no local fitting, warping or silhouette clipping",
               "alpha_extraction": "RGB minimum < 220; threshold sensitivity retained; raw white-background outputs unchanged",
               "single_control_foreground_in_other_cells": blank_pixels,
               "records": records, "coverage": coverage}
    if complete:
        summary["material_route"] = {"source": "near-01-06-raw.png", "source_sha256": SOURCE_SHA256,
                                     "sample_rectangles": SAMPLES, "source_script_sha256": digest(Path(__file__).with_name("sprite_material_atlas.py")),
                                     "meaning": "Two fixed generated material samples shade C++ control polygons. Alpha follows the controls by construction; this is not raw model performance.",
                                     "pose_dependent_generation_requests": 0}
    write_json(output / "summary.json", summary)
    template = (Path(__file__).with_name("sprite_sequence_review.html")).read_text()
    data = {"labels": labels, "records": records, "kinds": list(sequences)}
    (output / "review.html").write_text(template.replace("__DATA__", json.dumps(data).replace("<", "\\u003c")).replace("__COVERAGE__", coverage).replace("__REQUEST_COUNT__", str(len(raw))).replace("__MATERIAL_DISPLAY__", "block" if complete else "none"))
    print(json.dumps({"generated_leg_count": len(candidates), "request_count": len(raw), "output": str(output)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--complete", action="store_true", help="Require the three additional sheets covering all 24 leg poses")
    args = parser.parse_args()
    review(args.inputs, args.output, args.complete)


if __name__ == "__main__":
    main()
