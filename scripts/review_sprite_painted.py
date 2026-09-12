"""Audit and present a complete painted master without modifying its renders."""

import argparse
import gzip
import json
from pathlib import Path

import numpy as np
from PIL import Image

from scripts.prepare_sprite_sequence import ROOT, digest, make_grid, white, write_json


def cross_section(image, a, b):
    a, b = np.array(a)*2, np.array(b)*2
    center = (a+b)/2
    normal = np.array([a[1]-b[1], b[0]-a[0]]) / np.linalg.norm(b-a)
    offsets = np.arange(-64, 64.01, 0.25)
    points = np.rint(center+offsets[:, None]*normal).astype(int)
    alpha = np.array(image.getchannel("A"))
    covered = alpha[points[:, 1], points[:, 0]] >= 128
    middle = len(offsets)//2
    if not covered[middle]:
        raise ValueError("cross-section center misses its painted limb")
    left, right = middle, middle
    while left > 0 and covered[left-1]:
        left -= 1
    while right+1 < len(covered) and covered[right+1]:
        right += 1
    return float(offsets[right]-offsets[left]+0.25)


def proportion_comparison(output, geometry):
    previous = output.parent / "mapped-v3"
    rows, frames, labels = [], [], []
    for frame in geometry["frames"]:
        name = frame["name"]
        for leg in frame["legs"]:
            part = leg["side"]+"_leg"
            images = [Image.open(p / "parts" / f"{name}-{part}.png").convert("RGBA") for p in (previous, output)]
            for label, first, second in (("thigh", "hip", "knee"), ("calf", "knee", "cuff"), ("boot_shaft", "cuff", "ankle")):
                widths = [cross_section(im, leg["anchors"][first], leg["anchors"][second]) for im in images]
                rows.append({"frame": name, "part": part, "section": label,
                             "before_px": widths[0], "after_px": widths[1], "ratio": widths[1]/widths[0]})
        for part in ("near_arm", "far_arm", "head", "body", "tail"):
            before, after = [Image.open(p / "parts" / f"{name}-{part}.png").convert("RGBA") for p in (previous, output)]
            if part != "far_arm" and before.tobytes() != after.tobytes():
                raise ValueError("proportion correction unexpectedly changed "+part)
            if part == "far_arm":
                areas = [int((np.array(im.getchannel("A")) >= 128).sum()) for im in (before, after)]
                rows.append({"frame": name, "part": part, "before_area": areas[0], "after_area": areas[1], "ratio": areas[1]/areas[0]})
        for directory, label in ((previous, "Before"), (output, "Corrected")):
            frames.append(white(Image.open(directory / "frames" / (name+".png")).convert("RGBA")))
            labels.append(name+" · "+label)
    make_grid(frames, labels, 4, (512, 540)).save(output / "proportions-comparison.png")
    pairs = [make_grid(frames[i:i+2], labels[i:i+2], 2, (512, 540)) for i in range(0, 24, 2)]
    pairs[0].save(output / "before-after-512.png", save_all=True, append_images=pairs[1:], duration=1000/12, loop=0, disposal=0, blend=0)
    result = {"measurements": rows, "unchanged_parts": ["head", "body", "near_arm", "tail"],
              "method": "Connected alpha>=128 width at each segment midpoint, 0.25px sampling; painted area for back arm. Pose and ground translations unchanged."}
    write_json(output / "proportions.json", result)
    return result


def mesh_measurement(mapping, image):
    quads = np.array([quad for _, quad in mapping["mesh"]]).reshape(-1, 4, 2)
    dx = (quads[:, 3]-quads[:, 0])/4
    dy = (quads[:, 1]-quads[:, 0])/4
    jacobian = np.stack((dx, dy), axis=2)
    center = quads.mean(axis=1).round().astype(int)
    alpha = np.asarray(image.getchannel("A"))
    inside = (center[:, 0] >= 0) & (center[:, 0] < image.width) & (center[:, 1] >= 0) & (center[:, 1] < image.height)
    opaque = np.zeros(len(center), dtype=bool)
    opaque[inside] = alpha[center[inside, 1], center[inside, 0]] >= 128
    determinants = np.linalg.det(jacobian[opaque])
    if not len(determinants):
        raise ValueError("registration misses its artwork")
    singular = np.linalg.svd(jacobian[opaque], compute_uv=False)
    return {"name": mapping["name"], "sampled_art_cells": int(opaque.sum()),
            "inverted_art_cells": int((determinants <= 0).sum()),
            "maximum_local_anisotropy": float((singular[:, 0]/singular[:, 1]).max()),
            "p95_local_anisotropy": float(np.percentile(singular[:, 0]/singular[:, 1], 95))}


def review(inputs, output, geometry_path):
    geometry = json.loads(geometry_path.read_text())
    manifest = json.loads((output / "manifest.json").read_text())
    registration = json.loads((output / "registration.json").read_text())
    raw_hashes = {p.name: digest(p) for p in sorted(inputs.glob("*-raw.png"))}
    if raw_hashes != manifest["raw_sha256"]:
        raise ValueError("raw images changed since registration")
    records, audit = [], []
    for index, frame in enumerate(geometry["frames"]):
        name = frame["name"]
        alpha = np.array(Image.open(output / "legs" / (name+".png")).getchannel("A"))
        support = [leg for leg in frame["legs"] if leg["contact_mode"] != "none"]
        thresholds = {}
        for threshold in (32, 128, 224):
            bottom = int(np.where(alpha >= threshold)[0].max())
            thresholds[str(threshold)] = {"bottom_row": bottom, "pixels_below_428": int((alpha[429:] >= threshold).sum())}
        ratios = []
        for leg in frame["legs"]:
            a = leg["anchors"]
            h, t, ankle = [np.array(a[key]) for key in ("heel", "toe", "ankle")]
            ratios.append(float(np.dot(ankle-h, t-h)/np.dot(t-h, t-h)))
        records.append({"name": name, "support": support[0]["side"]+" / "+support[0]["contact_mode"] if support else "flight",
                        "contact_pin_y_master": (support[0]["anchors"]["contact"][1]+frame["translation"][1])*2 if support else None,
                        "ankle_sole_fraction": ratios, "alpha_thresholds": thresholds})
        maps = json.loads(gzip.decompress((output / "maps" / (name+".json.gz")).read_bytes()))
        for mapping in maps:
            part = mapping["name"]
            if part.endswith("_leg"):
                donor = index if part == "near_leg" else (index+6) % 12
                image = Image.open(output / "extracted" / f"leg-{donor+1:02}.png").convert("RGBA")
            else:
                donor = manifest.get("part_donors", {}).get(part, part)
                image = Image.open(output / "extracted" / (donor+".png")).convert("RGBA")
            audit.append({"frame": name, **mesh_measurement(mapping, image)})
    sizes, decoded = {}, {}
    for kind in ("frames", "legs", "raw-frames", "raw-legs", "overlays"):
        path = output / f"{kind}-512.png"
        animation = Image.open(path)
        if animation.n_frames != 12 or animation.size != (512, 512):
            raise ValueError("animation must contain twelve full-resolution frames")
        for index, name in enumerate(manifest["frames"]):
            animation.seek(index)
            still = Image.open(output / kind / (name+".png")).convert("RGBA")
            if animation.convert("RGBA").tobytes() != still.tobytes():
                raise ValueError("decoded animation differs from retained PNG frame")
        decoded[kind] = 12
        sizes[kind] = path.stat().st_size
    proportions = manifest.get("proportions", False)
    summary = {"canvas": [512, 512], "frames": 12, "fps": 12, "generation_requests": 0 if proportions else 4,
               "contacts": records, "mesh_audit": audit,
               "maximum_semantic_residual_raw_px": max(r["landmark_residual_raw_px"] for r in registration["residuals"]),
               "maximum_leg_correction_master_px": max(r["maximum_landmark_correction_master_px"] for r in registration["legs"]),
               "decoded_frames_verified": decoded, "apng_bytes": sizes,
               "raw_hashes_verified": raw_hashes,
               "interpretation": "C++ pins and imposed registration are not proof of model pose obedience or aesthetic acceptance. Native painted parts and all raw outputs are retained."}
    write_json(output / "summary.json", summary)
    data = {"names": manifest["frames"], "contacts": records}
    if proportions:
        data["proportions"] = proportion_comparison(output, geometry)
    template = Path(__file__).with_name("sprite_proportions_review.html" if proportions else "sprite_painted_review.html")
    (output / "review.html").write_text(template.read_text().replace("__DATA__", json.dumps(data)))
    old = inputs.parent / "boot-atlas-v2"
    panels, labels = [], []
    for name, side in (("reference_07", "far"), ("reference_01", "near"), ("reference_10", "near"), ("reference_04", "far")):
        for source, label in ((old / "parts" / f"{name}-{side}.png", "atlas v2"),
                              (output / "parts" / f"{name}-{side}_leg.png", "complete painted part")):
            im = Image.open(source).convert("RGBA")
            if im.width == 256:
                im = im.resize((512, 512), Image.Resampling.NEAREST)
            panels.append(white(im).crop((160, 256, 416, 448)).resize((512, 384), Image.Resampling.NEAREST))
            labels.append(name+" / "+side+" / "+label)
    make_grid(panels, labels, 2, (512, 416)).save(output / "anatomy-comparison.png")
    print(json.dumps({"frames": 12, "inverted_art_cells": sum(r["inverted_art_cells"] for r in audit),
                      "max_anisotropy": max(r["maximum_local_anisotropy"] for r in audit),
                      "max_pin_error_raw_px": summary["maximum_semantic_residual_raw_px"],
                      "max_correction_master_px": summary["maximum_leg_correction_master_px"]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--geometry", type=Path, required=True)
    args = parser.parse_args()
    review(args.inputs, args.output, args.geometry)
