"""Prepare a fixed-pose isolated-leg comparison. No uploads or provider calls."""

import argparse
import hashlib
import json
import math
import shutil
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

if __package__:
    from .prepare_boot_view_guides import render_boot
else:
    from prepare_boot_view_guides import render_boot


PREFIX = "zebes-leg-pose-v1"
SEED = 17290401
PROMPT = """Use case: precise-object-edit
Image 1 is the edit target and exact pose authority: ONE isolated folded trouser leg and boot. Image 2 supplies the original mouse's palette, pixel-art style and clothing materials ONLY. Image 3 is a contour/landmark reference of Image 1, not artwork to copy into the result.
Paint fabric folds and leather shading onto Image 1 while preserving its silhouette, placement, size and boot orientation. Keep the calf almost horizontal, extending LEFT from the knee into the cuff. Keep the sole's heel-to-toe direction DOWN and slightly LEFT, exactly as shown by the arrow. Do not curl the calf downward, sag the ankle, turn the toe right, or substitute a familiar running-leg pose. The knee is the intended bend. Folds may shade the cloth but must not bend the underlying lower leg.
One stout dark-brown trouser leg and one stout brown leather boot, with cuff, dark sole and restrained pixel clusters matching Image 2. Keep the existing thickness and the complete hip attachment. Add no other body parts, coat, mouse face or scenery.
Keep the entire 1024 x 1024 white canvas and its empty space. Do not recenter or enlarge the asset. Return only the finished part on plain white, without annotations, arrows, labels, guide lines, checkerboard or cast shadow. Geometry preservation takes priority over polishing or rounding the guide's shape.
"""
POSITIVE = "pixel art game asset, one isolated folded dark brown trouser leg and brown leather boot, stout proportions, thigh slanting down right to knee, calf extending horizontally left, boot toe pointing down left, bent knee, fabric folds, leather shading, cuff, dark sole, consistent dark pixel outline, plain white background"
NEGATIVE = "whole character, mouse face, torso, second leg, second boot, coat, cape, curved shin, bent calf, sagging ankle, toe pointing right, changed silhouette, moved boot, text, arrows, labels, checkerboard, cast shadow, scenery, photorealistic"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def graph(denoise):
    if denoise not in (0.65, 0.85):
        raise ValueError("use one of the two predeclared redraw strengths")
    def node(kind, **inputs):
        return {"class_type": kind, "inputs": inputs}
    return {
        "1": node("CheckpointLoaderSimple", ckpt_name="sd_xl_base_1.0.safetensors"),
        "2": node("LoraLoader", model=["1", 0], clip=["1", 1], lora_name="pixel-art-xl.safetensors", strength_model=0.8, strength_clip=0.8),
        "3": node("IPAdapterModelLoader", ipadapter_file="ip-adapter-plus_sdxl_vit-h.safetensors"),
        "4": node("CLIPVisionLoader", clip_name="CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"),
        "5": node("LoadImage", image=PREFIX + "/identity.png"),
        "6": node("IPAdapterAdvanced", model=["2", 0], ipadapter=["3", 0], clip_vision=["4", 0], image=["5", 0],
                  weight=0.3, weight_type="linear", combine_embeds="concat", start_at=0.0, end_at=1.0, embeds_scaling="V only"),
        "7": node("LoadImage", image=PREFIX + "/edit-input.png"),
        "8": node("VAEEncode", pixels=["7", 0], vae=["1", 2]),
        "9": node("CLIPTextEncode", text=POSITIVE, clip=["2", 1]),
        "10": node("CLIPTextEncode", text=NEGATIVE, clip=["2", 1]),
        "11": node("LoadImage", image=PREFIX + "/canny.png"),
        "12": node("ControlNetLoader", control_net_name="xinsir-controlnet-canny-sdxl-1.0.safetensors"),
        "13": node("ControlNetApplyAdvanced", positive=["9", 0], negative=["10", 0], control_net=["12", 0],
                   image=["11", 0], strength=1.0, start_percent=0.0, end_percent=1.0, vae=["1", 2]),
        "14": node("KSampler", model=["6", 0], seed=SEED, steps=30, cfg=5.5, sampler_name="dpmpp_2m", scheduler="karras",
                   positive=["13", 0], negative=["13", 1], latent_image=["8", 0], denoise=denoise),
        "15": node("VAEDecode", samples=["14", 0], vae=["1", 2]),
        "16": node("SaveImage", images=["15", 0], filename_prefix=f"{PREFIX}/canny-{denoise:.2f}-raw"),
    }


def map_point(point, crop, size=1024):
    if crop[2] - crop[0] != crop[3] - crop[1] or crop[2] <= crop[0]:
        raise ValueError("expected a nonempty square crop")
    return [(point[i] - crop[i]) * size / (crop[2] - crop[0]) for i in (0, 1)]


def prepare(source, output):
    if output.exists():
        raise ValueError("output already exists; preserve earlier trial revisions")
    previous = json.loads((source / "manifest.json").read_text())
    entry = next(entry for entry in previous["entries"] if entry["name"] == "near")
    if digest(source / "near/edit-input.png") != entry["input_sha256"]:
        raise ValueError("original approved edit target changed")
    if digest(source / "identity.png") != previous["identity_sha256"]:
        raise ValueError("original appearance reference changed")
    crop = entry["crop"]
    pose = next(frame for frame in previous["source_ownership"]["frames"] if frame["name"] == "reference_10")
    hip, knee, cuff, heel, toe = pose["paths"]["near"]
    config = previous["source_ownership"]["source_guide_config"]
    _, _, boot = render_boot(heel, toe, config, knee)
    points = {"hip": hip, "knee": knee, "cuff": cuff, "proxy_ankle": boot["ankle"], "heel": heel, "toe": toe}
    mapped = {name: map_point(point, crop) for name, point in points.items()}
    target = Image.open(source / "near/edit-input.png").convert("RGB")
    if target.size != (1024, 1024):
        raise ValueError("original edit target is not the expected 1024px square")
    part = Image.open(source / "near/source-part.png").convert("RGBA")
    if part.size != (crop[2] - crop[0], crop[3] - crop[1]):
        raise ValueError("source part disagrees with fixed crop")
    edge = cv2.Canny(cv2.cvtColor(np.asarray(target), cv2.COLOR_RGB2GRAY), 100, 200)
    # A separate ordinary reference: outer silhouette plus semantic landmarks.
    alpha = np.asarray(part.getchannel("A").resize(target.size, Image.Resampling.NEAREST))
    contour = cv2.Canny(alpha, 100, 200)
    diagram = Image.fromarray(255 - cv2.dilate(contour, np.ones((3, 3), np.uint8))).convert("RGB")
    draw = ImageDraw.Draw(diagram)
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 23) if font_path.exists() else ImageFont.load_default()
    blue, orange = (26, 113, 181), (187, 74, 15)
    draw.line([tuple(mapped[key]) for key in ("knee", "cuff")], fill=blue, width=4)
    h, t = np.array(mapped["heel"]), np.array(mapped["toe"])
    v = (t - h) / np.linalg.norm(t - h)
    side = np.array([-v[1], v[0]])
    draw.line([tuple(h), tuple(t)], fill=orange, width=5)
    draw.polygon([tuple(t), tuple(t - v * 22 + side * 10), tuple(t - v * 22 - side * 10)], fill=orange)
    for name, label in (("knee", "K"), ("cuff", "C"), ("proxy_ankle", "A"), ("heel", "H"), ("toe", "T")):
        x, y = mapped[name]
        draw.ellipse((x-7, y-7, x+7, y+7), fill="white", outline=blue, width=3)
        draw.text((x+12, y-12), label, fill=blue, font=font)
    for index, text in enumerate(("K to C: almost horizontal calf", "H to T: sole points down-left", "A: proxy ankle inside the boot", "Labels and arrows are reference only")):
        draw.text((80, 600 + index * 36), text, fill=(30, 35, 40), font=font)
    output.mkdir(parents=True)
    for name, origin in (("edit-input.png", "near/edit-input.png"), ("identity.png", "identity.png"), ("source-part.png", "near/source-part.png")):
        shutil.copyfile(source / origin, output / name)
    diagram.save(output / "landmarks.png")
    Image.fromarray(edge).convert("RGB").save(output / "canny.png")
    (output / "builtin-prompt.txt").write_text(PROMPT)
    jobs = [{"name": "builtin", "provider": "built-in image_gen", "prompt": "builtin-prompt.txt",
             "references": ["edit-input.png", "identity.png", "landmarks.png"], "status": "prepared"}]
    for denoise in (0.65, 0.85):
        filename = f"canny-{denoise:.2f}.json"
        (output / filename).write_text(json.dumps(graph(denoise), indent=2) + "\n")
        jobs.append({"name": f"canny-{denoise:.2f}", "provider": "derry ComfyUI", "graph": filename, "status": "prepared"})
    names = ("edit-input.png", "identity.png", "landmarks.png", "canny.png", "source-part.png", "builtin-prompt.txt", "canny-0.65.json", "canny-0.85.json")
    manifest = {"status": "prepared for input review; nothing uploaded or generated", "provider_calls": 0,
                "source_trial": str(source), "source_manifest_sha256": digest(source / "manifest.json"),
                "fixed_crop": crop, "working_canvas": [256, 256], "requested_canvas": [1024, 1024],
                "landmarks_working": points, "landmarks_1024": mapped,
                "sole_angle_screen_degrees": math.degrees(math.atan2(toe[1] - heel[1], toe[0] - heel[0])),
                "registration": "inverse fixed crop only; no per-result fitting, rotation, warp or silhouette clipping",
                "canny": {"source": "edit-input.png", "thresholds": [100, 200], "opencv": cv2.__version__,
                          "note": "actual image edges; no labeled skeleton, fake depth or ordinal surface map"},
                "remote_asset_prefix": PREFIX, "file_sha256": {name: digest(output / name) for name in names},
                "jobs": jobs, "comfy_node_schema_verified": "ControlNetLoader/ControlNetApplyAdvanced via live object_info, 2026-09-10"}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    board = Image.new("RGB", (1440, 460), (243, 242, 237))
    labels = ("Pose target: unchanged", "Ordinary image reference", "ComfyUI Canny input", "Appearance only")
    files = ("edit-input.png", "landmarks.png", "canny.png", "identity.png")
    b = ImageDraw.Draw(board)
    for index, (label, filename) in enumerate(zip(labels, files, strict=True)):
        b.text((index * 360 + 10, 12), label, fill=(25, 30, 35), font=font)
        resampling = Image.Resampling.BOX if filename in ("landmarks.png", "canny.png") else Image.Resampling.NEAREST
        image = Image.open(output / filename).convert("RGB").resize((340, 340), resampling)
        board.paste(image, (index * 360 + 10, 50))
    b.text((10, 411), "One folded leg. Same crop and pose in all trials. New outputs will be judged before any compositing.", fill=(25, 30, 35), font=font)
    board.save(output / "input-review.png")
    print("Prepared three trials on the original approved pose. No uploads or generation.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-trial", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source_trial, args.output)


if __name__ == "__main__":
    main()
