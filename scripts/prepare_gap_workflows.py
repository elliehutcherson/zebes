"""Write ComfyUI API graphs for reviewed inputs; never connect or submit jobs."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


POSITIVE = "pixel art, side view of a running brown mouse in a green hooded coat, red scarf, dark brown trousers and brown leather boots, two anatomically connected legs, trouser fabric entering the boot openings, continuous calf silhouette, consistent dark outline and pixel clusters, plain white background"
NEGATIVE = "detached boot, severed leg, extra leg, extra foot, merged legs, changed pose, changed boot direction, extended coat, white gap inside a leg, text, checkerboard, scenery, shadow, photorealistic"


def graph(frame, variant, denoise, seed):
    if variant not in ("blank", "blur", "shaped") or not 0 < denoise < 1:
        raise ValueError("invalid prefill or denoise")
    asset = f"zebes-run-gap-v1/{frame}/"
    prefix = f"zebes-run-gap-v1/{frame}-{variant}-{denoise:.2f}-{seed}"
    def node(kind, **inputs):
        return {"class_type": kind, "inputs": inputs}
    return {
        "1": node("CheckpointLoaderSimple", ckpt_name="sd_xl_base_1.0.safetensors"),
        "2": node("LoraLoader", model=["1", 0], clip=["1", 1], lora_name="pixel-art-xl.safetensors", strength_model=0.8, strength_clip=0.8),
        "3": node("IPAdapterModelLoader", ipadapter_file="ip-adapter-plus_sdxl_vit-h.safetensors"),
        "4": node("CLIPVisionLoader", clip_name="CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"),
        "5": node("LoadImage", image="zebes-run-gap-v1/identity-1024.png"),
        "6": node("IPAdapterAdvanced", model=["2", 0], ipadapter=["3", 0], clip_vision=["4", 0], image=["5", 0],
                  weight=0.45, weight_type="linear", combine_embeds="concat", start_at=0.0, end_at=1.0, embeds_scaling="V only"),
        "7": node("LoadImage", image=asset + variant + "-1024.png"),
        "8": node("LoadImage", image=asset + "mask-1024.png"),
        # PNG is opaque grayscale. LoadImage's alpha-derived MASK is all zero.
        "9": node("ImageToMask", image=["8", 0], channel="red"),
        # Preserve the actual prefill; VAEEncodeForInpaint would neutralize it.
        "10": node("VAEEncode", pixels=["7", 0], vae=["1", 2]),
        "11": node("SetLatentNoiseMask", samples=["10", 0], mask=["9", 0]),
        "12": node("CLIPTextEncode", text=POSITIVE, clip=["2", 1]),
        "13": node("CLIPTextEncode", text=NEGATIVE, clip=["2", 1]),
        "14": node("KSampler", model=["6", 0], seed=seed, steps=30, cfg=5.5, sampler_name="dpmpp_2m", scheduler="karras",
                   positive=["12", 0], negative=["13", 0], latent_image=["11", 0], denoise=denoise),
        "15": node("VAEDecode", samples=["14", 0], vae=["1", 2]),
        "16": node("SaveImage", images=["15", 0], filename_prefix=prefix + "-raw"),
        "17": node("LoadImage", image=asset + "blank-1024.png"),
        "18": node("ImageCompositeMasked", destination=["17", 0], source=["15", 0], mask=["9", 0], x=0, y=0, resize_source=False),
        "19": node("SaveImage", images=["18", 0], filename_prefix=prefix + "-composite"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefills", type=Path, required=True)
    parser.add_argument("--identity", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("workflow output already exists; retain reviewed revisions")
    manifest = json.loads((args.prefills / "manifest.json").read_text())
    if manifest["provider_calls"]:
        raise ValueError("inputs already submitted")
    args.output.mkdir(parents=True)
    identity = Image.open(args.identity).convert("RGBA")
    white = Image.alpha_composite(Image.new("RGBA", identity.size, "white"), identity).convert("RGB")
    white.resize((1024, 1024), Image.Resampling.NEAREST).save(args.prefills / "identity-1024.png", optimize=True)
    jobs = []
    for frame in ("reference_05", "reference_10"):
        for variant in ("blank", "blur", "shaped"):
            for denoise in (0.35, 0.60):
                filename = f"{frame}-{variant}-{denoise:.2f}.json"
                (args.output / filename).write_text(json.dumps(graph(frame, variant, denoise, 17290401), indent=2) + "\n")
                jobs.append({"frame": frame, "variant": variant, "denoise": denoise, "seed": 17290401, "graph": filename})
    (args.output / "plan.json").write_text(json.dumps({
        "status": "prepared only; input review required before upload or submission",
        "node_schema_verified": "derry ComfyUI /object_info, 2026-09-09; no sampler executed",
        "identity_source_sha256": hashlib.sha256(args.identity.read_bytes()).hexdigest(),
        "identity_input_sha256": hashlib.sha256((args.prefills / "identity-1024.png").read_bytes()).hexdigest(),
        "initial_jobs": jobs, "reserved_checks": "chosen setting on frames 5/10 with seed 17290402; frame 11 with both seeds",
        "maximum_pilot_images": 16, "maximum_full_cycle_images": 12,
        "remote_asset_prefix": "zebes-run-gap-v1", "edge_depth_controls": "disabled",
        "output": "1024 RGB white matte; raw and outside-mask-restored outputs retained separately",
    }, indent=2) + "\n")
    print("Prepared twelve initial API graphs; nothing uploaded or queued.")


if __name__ == "__main__":
    main()
