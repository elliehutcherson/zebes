"""Prepare immutable full-detail image requests from standalone C++ geometry."""

import argparse
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw

from scripts.prepare_sprite_boot_atlas import CONFIG, atlas_rows
from scripts.prepare_sprite_sequence import (
    DOCUMENT, LANDMARKS, RENDER, ROOT, SOURCE, TRACE, digest, font,
    make_grid, white, write_json,
)

EXPERIMENT = ROOT / "experiments/sprite_sequence"
LEG_CROP = (80, 128, 208, 224)
CELL = (768, 576)
UPPER = ("head", "body", "near_arm", "far_arm", "tail")


def guide(leg):
    image = Image.new("RGBA", (1536, 1536))
    draw = ImageDraw.Draw(image)
    for surface in leg["surfaces"]:
        color = (83, 72, 47) if surface["material"] == "trousers" else (143, 76, 39)
        draw.polygon([(x*6, y*6) for x, y in surface["outline"]], fill=color)
    heel, toe = leg["anchors"]["heel"], leg["anchors"]["toe"]
    draw.line([(x*6, y*6) for x, y in (heel, toe)], fill=(48, 32, 23), width=12)
    if leg["view"] == "sole_visible":
        # This is an authored surface label, not a depth conditioning image.
        foot = leg["surfaces"][-1]
        h, t = heel, toe
        v = foot["v"]
        points = [h, t, [t[i]+v[i]*0.28 for i in (0, 1)], [h[i]+v[i]*0.28 for i in (0, 1)]]
        draw.polygon([(x*6, y*6) for x, y in points], fill=(61, 38, 25))
    return image


def prepare(binary, output):
    if output.exists():
        raise ValueError("output exists; choose a new evidence revision")
    document, trace, landmarks, config = [json.loads(p.read_text()) for p in (DOCUMENT, TRACE, LANDMARKS, CONFIG)]
    config["heel_drop_working_px"]["reference_01"]["near"] = 4
    rows = atlas_rows(document, trace, landmarks, config)
    run = subprocess.run([str(binary.resolve()), "--painted"], input=rows, capture_output=True, text=True)
    if run.returncode:
        raise ValueError(run.stderr)
    geometry = json.loads(run.stdout)
    output.mkdir(parents=True)
    (output / "geometry-input.txt").write_text(rows)
    write_json(output / "geometry.json", geometry)
    write_json(output / "contact-config.json", config)
    cells = []
    controls, overlays = [], []
    for frame in geometry["frames"]:
        whole = Image.new("RGBA", (1536, 1536))
        over = whole.copy()
        for leg in frame["legs"]:
            part = guide(leg)
            whole.alpha_composite(part)
            over.alpha_composite(part)
            draw = ImageDraw.Draw(over)
            a = leg["anchors"]
            color = (240, 111, 30) if leg["side"] == "near" else (30, 150, 240)
            draw.line([(a[k][0]*6, a[k][1]*6) for k in ("hip", "knee", "cuff", "ankle", "toe")], fill=color, width=5)
            for key in ("hip", "knee", "cuff", "ankle", "heel", "toe"):
                x, y = a[key]
                draw.ellipse((x*6-6, y*6-6, x*6+6, y*6+6), fill=color)
        controls.append(white(whole).resize((384, 384), Image.Resampling.LANCZOS))
        overlays.append(white(over).resize((384, 384), Image.Resampling.LANCZOS))
        # Twelve near-leg paintings also supply the opposite limb six phases
        # later. The C++ target remains the original far-leg pose, never a swap.
        near = next(leg for leg in frame["legs"] if leg["side"] == "near")
        box = tuple(v*6 for v in LEG_CROP)
        cells.append(white(guide(near)).crop(box))
    names = [f["name"] for f in geometry["frames"]]
    make_grid(controls, names, 4, (384, 412)).save(output / "all-poses.png")
    make_grid(overlays, names, 4, (384, 412)).save(output / "all-anchors.png")
    for group in range(3):
        sheet = Image.new("RGB", (1536, 1152), "white")
        for j in range(4):
            sheet.paste(cells[group*4+j], ((j % 2)*CELL[0], (j // 2)*CELL[1]))
        sheet.save(output / f"legs-{group+1}-guide.png")
    # Keep the renderer's independently owned parts separated in one high-detail
    # request. Fixed cell/canvas transforms remain visible in the manifest.
    upper = Image.new("RGB", (1536, 1024), "white")
    for index, name in enumerate(UPPER):
        part = Image.open(RENDER / "part-poses" / name / "reference_01.png").convert("RGBA")
        upper.paste(white(part).resize((512, 512), Image.Resampling.NEAREST), ((index % 3)*512, (index // 3)*512))
    upper.save(output / "upper-guide.png")
    write_json(output / "manifest.json", {
        "version": 1, "master_canvas": [512, 512], "fps": 12,
        "leg_cells": {"size": list(CELL), "world_crop": list(LEG_CROP), "world_scale": 6, "order": names},
        "upper_cells": {"size": [512, 512], "world_scale": 2, "order": list(UPPER), "pose": "reference_01"},
        "contract": "C++ owns geometry, heel anatomy, views, contact and cyclic coat response. New raw paintings require explicit semantic registration; no bounds fitting.",
        "source_hashes": {str(p.relative_to(ROOT)): digest(p) for p in (DOCUMENT, TRACE, LANDMARKS, CONFIG, SOURCE, Path(__file__), ROOT / "scripts/sprite_sequence_geometry.cc")},
        "prior_raw_hashes": {str(p.relative_to(ROOT)): digest(p) for p in sorted((EXPERIMENT / "evidence/profile-v2").glob("*-raw.png"))},
        "input_hashes": {p.name: digest(p) for p in sorted(output.glob("*.png"))},
    })
    print(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.geometry, args.output)
