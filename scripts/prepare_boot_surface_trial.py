"""Prepare a boot-only Canny/depth comparison without provider calls.

The proxy renderer owns projection, view, registration, and depth. Diffusion is
asked only to replace the proxy's brown materials with finished pixel art.
"""

import argparse
import hashlib
import json
import shutil
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

if __package__:
    from .prepare_boot_view_guides import render_boot
else:
    from prepare_boot_view_guides import render_boot


PREFIX = "zebes-boot-surface-v1"
SEED = 17290401
POSITIVE = "pixel art game asset, one isolated stout brown leather boot, dark sole, cuff, restrained leather highlights, side and upper surfaces readable, toe directed down left in the image, plain white background"
NEGATIVE = "leg, trousers, whole character, second boot, front-facing boot, boot pointing toward viewer, toe pointing right, exposed sole, round frontal toe cap, changed silhouette, moved object, text, shadow, scenery, photorealistic"
PROMPT = """One isolated reusable pixel-art boot on white. The brown proxy is the exact edit target and owns silhouette, placement, scale, heel-to-toe axis, camera view and visible surfaces. Repaint only its materials to match the mouse reference: stout brown leather, dark sole, cuff, restrained highlights and compact pixel clusters. The toe travels down-left across the image; this is a side/upper view, not a boot pointing toward the viewer. Do not add a frontal round toe cap or expose the sole. Keep the full canvas and empty space unchanged. Add no leg, character, text, annotation, cast shadow or scenery. Geometry and view preservation take priority over polish."""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def graph(depth_strength):
    if depth_strength not in (None, 0.45, 0.80):
        raise ValueError("use the declared Canny-only or depth strength")

    def node(kind, **inputs):
        return {"class_type": kind, "inputs": inputs}

    nodes = {
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
    }
    conditioning = "13"
    if depth_strength is not None:
        nodes.update({
            "14": node("LoadImage", image=PREFIX + "/depth.png"),
            "15": node("ControlNetLoader", control_net_name="xinsir-controlnet-depth-sdxl-1.0.safetensors"),
            "16": node("ControlNetApplyAdvanced", positive=["13", 0], negative=["13", 1], control_net=["15", 0],
                       image=["14", 0], strength=depth_strength, start_percent=0.0, end_percent=0.85, vae=["1", 2]),
        })
        conditioning = "16"
    label = "canny-only" if depth_strength is None else f"depth-{depth_strength:.2f}"
    nodes.update({
        "17": node("KSampler", model=["6", 0], seed=SEED, steps=30, cfg=5.5, sampler_name="dpmpp_2m", scheduler="karras",
                   positive=[conditioning, 0], negative=[conditioning, 1], latent_image=["8", 0], denoise=0.65),
        "18": node("VAEDecode", samples=["17", 0], vae=["1", 2]),
        "19": node("SaveImage", images=["18", 0], filename_prefix=PREFIX + "/" + label + "-raw"),
    })
    return nodes


def prepare(config_path, guide_manifest_path, identity_path, output):
    if output.exists():
        raise ValueError("output already exists; preserve earlier experiment revisions")
    config = json.loads(config_path.read_text())
    guide_manifest = json.loads(guide_manifest_path.read_text())
    frame = next(frame for frame in guide_manifest["frames"] if frame["name"] == "reference_10")
    anchors = frame["annotations"]["near"]
    material, surfaces, depth, annotations = render_boot(
        anchors["heel"], anchors["toe"], config, anchors.get("knee"), include_depth=True)
    crop = [88, 136, 184, 232]
    if material.getbbox() is None or material.getbbox()[0] < crop[0] or material.getbbox()[2] > crop[2] or material.getbbox()[1] < crop[1] or material.getbbox()[3] > crop[3]:
        raise ValueError("boot proxy no longer fits the declared fixed crop")
    output.mkdir(parents=True)
    white = Image.new("RGBA", tuple(config["canvas"]), "white")
    edit = Image.alpha_composite(white, material).convert("RGB").crop(crop).resize((1024, 1024), Image.Resampling.NEAREST)
    surface = Image.alpha_composite(white, surfaces).convert("RGB").crop(crop).resize((1024, 1024), Image.Resampling.NEAREST)
    depth_input = depth.crop(crop).resize((1024, 1024), Image.Resampling.NEAREST)
    canny = cv2.Canny(cv2.cvtColor(np.asarray(edit), cv2.COLOR_RGB2GRAY), 100, 200)
    edit.save(output / "edit-input.png")
    surface.save(output / "surfaces-review.png")
    depth_input.save(output / "depth.png")
    Image.fromarray(canny).convert("RGB").save(output / "canny.png")
    shutil.copyfile(identity_path, output / "identity.png")
    (output / "prompt.txt").write_text(PROMPT + "\n")
    jobs = []
    for name, strength in (("canny-only", None), ("depth-0.45", 0.45), ("depth-0.80", 0.80)):
        filename = name + ".json"
        (output / filename).write_text(json.dumps(graph(strength), indent=2) + "\n")
        jobs.append({"name": name, "provider": "derry ComfyUI", "graph": filename, "output_node": "19", "status": "prepared"})
    files = ("edit-input.png", "surfaces-review.png", "depth.png", "canny.png", "identity.png", "prompt.txt",
             "canny-only.json", "depth-0.45.json", "depth-0.80.json")
    manifest = {
        "status": "prepared for visible input review; zero uploads and zero provider calls",
        "provider_calls": 0,
        "question": "Does actual proxy depth prevent a down-left boot from being repainted as a front-facing boot?",
        "architecture": "authored proxy owns pose/view/registration; diffusion owns appearance only",
        "source_config": str(config_path),
        "source_config_sha256": digest(config_path),
        "source_guide_manifest": str(guide_manifest_path),
        "source_guide_manifest_sha256": digest(guide_manifest_path),
        "fixed_crop": crop,
        "working_canvas": list(config["canvas"]),
        "requested_canvas": [1024, 1024],
        "remote_asset_prefix": PREFIX,
        "depth": {"source": "3D z-buffer from the reviewed fixed-camera boot proxy", **annotations},
        "registration": "whole 1024 canvas maps back to the fixed 96px crop; no bounds fitting, rotation, warp, or clipping",
        "file_sha256": {name: digest(output / name) for name in files},
        "jobs": jobs,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 22) if font_path.exists() else ImageFont.load_default()
    labels = ("Exact proxy/edit target", "Actual z-buffer depth", "Surface IDs: review only", "Canny contour", "Appearance only")
    names = ("edit-input.png", "depth.png", "surfaces-review.png", "canny.png", "identity.png")
    board = Image.new("RGB", (1800, 440), (243, 242, 237))
    draw = ImageDraw.Draw(board)
    for index, (label, name) in enumerate(zip(labels, names, strict=True)):
        x = index * 360
        draw.text((x + 10, 10), label, fill=(24, 28, 32), font=font)
        panel = Image.open(output / name).convert("RGB").resize((340, 340), Image.Resampling.NEAREST)
        board.paste(panel, (x + 10, 48))
    draw.text((10, 404), "One boot, one fixed camera. Depth is generated by the proxy z-buffer; surface colors never reach the model.", fill=(24, 28, 32), font=font)
    board.save(output / "input-review.png")
    print("Prepared boot surface-control trial. No uploads or provider calls.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--guide-manifest", required=True, type=Path)
    parser.add_argument("--identity", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.config, args.guide_manifest, args.identity, args.output)


if __name__ == "__main__":
    main()
