"""Measure fixed-registration pose drift in the isolated-leg trial.

This reviewer deliberately scores only canvas registration and silhouette. It
does not claim that an overlapping colored region identifies a knee, cuff, or
boot surface; those remain explicit visual-review questions.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont


REVIEW_SIZE = 1024


def foreground_mask(image):
    rgb = np.asarray(image.convert("RGB"), dtype=np.int16)
    # All declared inputs and outputs use a white matte. A modest distance from
    # white retains antialiased colored edges without treating compression noise
    # or the built-in generator's off-white canvas as character artwork.
    mask = (np.max(255 - rgb, axis=2) >= 28).astype(np.uint8)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    if count == 1:
        return mask.astype(bool)
    largest = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    return labels == largest


def bounds(mask):
    ys, xs = np.nonzero(mask)
    if not len(xs):
        raise ValueError("candidate contains no foreground")
    return [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]


def working_point(point, crop):
    width, height = crop[2] - crop[0], crop[3] - crop[1]
    return [crop[0] + point[0] * width / REVIEW_SIZE, crop[1] + point[1] * height / REVIEW_SIZE]


def measure(target, candidate, crop):
    target_mask, candidate_mask = foreground_mask(target), foreground_mask(candidate)
    intersection = np.logical_and(target_mask, candidate_mask).sum()
    union = np.logical_or(target_mask, candidate_mask).sum()
    target_area, candidate_area = target_mask.sum(), candidate_mask.sum()
    target_bounds, candidate_bounds = bounds(target_mask), bounds(candidate_mask)
    target_center = np.mean(np.argwhere(target_mask), axis=0)[::-1]
    candidate_center = np.mean(np.argwhere(candidate_mask), axis=0)[::-1]
    target_center_working = working_point(target_center, crop)
    candidate_center_working = working_point(candidate_center, crop)
    convert_bounds = lambda box: working_point(box[:2], crop) + working_point(box[2:], crop)
    return {
        "silhouette_iou": float(intersection / union),
        "target_foreground_pixels_1024": int(target_area),
        "candidate_foreground_pixels_1024": int(candidate_area),
        "area_ratio": float(candidate_area / target_area),
        "added_pixels_1024": int(np.logical_and(candidate_mask, ~target_mask).sum()),
        "missing_pixels_1024": int(np.logical_and(target_mask, ~candidate_mask).sum()),
        "centroid_drift_working_pixels": float(math.dist(target_center_working, candidate_center_working)),
        "target_bounds_working": convert_bounds(target_bounds),
        "candidate_bounds_working": convert_bounds(candidate_bounds),
    }


def overlay(candidate, target):
    result = candidate.convert("RGB").copy()
    target_mask = foreground_mask(target)
    edge = target_mask ^ np.logical_and.reduce((
        np.roll(target_mask, 1, 0), np.roll(target_mask, -1, 0),
        np.roll(target_mask, 1, 1), np.roll(target_mask, -1, 1)))
    pixels = np.asarray(result).copy()
    pixels[edge] = (0, 190, 225)
    return Image.fromarray(pixels)


def review(trial):
    manifest = json.loads((trial / "manifest.json").read_text())
    crop = manifest["fixed_crop"]
    candidates = tuple(job["name"] for job in manifest["jobs"] if (trial / "results" / job["name"] / "raw-output.png").is_file())
    if not candidates:
        raise ValueError("trial has no completed raw outputs")
    target = Image.open(trial / "edit-input.png").convert("RGB")
    if target.size != (REVIEW_SIZE, REVIEW_SIZE):
        raise ValueError("pose authority must remain 1024x1024")
    records, panels = [], []
    for name in candidates:
        raw_path = trial / "results" / name / "raw-output.png"
        raw = Image.open(raw_path).convert("RGB")
        if raw.width != raw.height:
            raise ValueError(f"{name} output is not square; stop before registration")
        registered = raw.resize((REVIEW_SIZE, REVIEW_SIZE), Image.Resampling.NEAREST)
        record = {
            "name": name,
            "raw_size": list(raw.size),
            "raw_sha256": hashlib.sha256(raw_path.read_bytes()).hexdigest(),
            "registration": "whole square canvas scaled to 1024, then inverse fixed crop; no bounds fitting",
            **measure(target, registered, crop),
            "view_direction": "manual review required; silhouette metrics cannot establish visible boot surfaces",
        }
        records.append(record)
        panels.append(overlay(registered, target))

    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 24) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 18) if font_path.exists() else font
    board = Image.new("RGB", (400 * (len(records) + 1), 520), (243, 242, 237))
    draw = ImageDraw.Draw(board)
    source = target.crop((170, 20, 850, 700)).resize((380, 380), Image.Resampling.NEAREST)
    draw.text((10, 10), "Pose authority", fill=(24, 28, 32), font=font)
    board.paste(source, (10, 50))
    for index, (record, panel) in enumerate(zip(records, panels, strict=True), start=1):
        x = index * 400
        crop_panel = panel.crop((170, 20, 850, 700)).resize((380, 380), Image.Resampling.NEAREST)
        board.paste(crop_panel, (x + 10, 50))
        draw.text((x + 10, 10), record["name"], fill=(24, 28, 32), font=font)
        draw.text((x + 10, 440), f"IoU {record['silhouette_iou']:.3f}  area {record['area_ratio']:.2f}x", fill=(24, 28, 32), font=small)
        draw.text((x + 10, 468), f"centroid drift {record['centroid_drift_working_pixels']:.1f}px", fill=(24, 28, 32), font=small)
    draw.text((10, 480), "Cyan: exact target contour. Metrics use fixed canvas registration, not fitted bounds.", fill=(24, 28, 32), font=small)
    board.save(trial / "review.png")
    summary = {
        "status": "three results measured; boot view requires visual decision",
        "pose_authority_sha256": hashlib.sha256((trial / "edit-input.png").read_bytes()).hexdigest(),
        "fixed_crop": crop,
        "metric_scope": "Registration and silhouette only; not anatomy, surface visibility, leg ownership, or finish.",
        "candidates": records,
    }
    assessment_path = trial / "assessment.json"
    if assessment_path.is_file():
        summary["visual_assessment"] = json.loads(assessment_path.read_text())
    (trial / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("Reviewed three fixed-registration results; see review.png and summary.json.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trial", required=True, type=Path)
    review(parser.parse_args().trial)


if __name__ == "__main__":
    main()
