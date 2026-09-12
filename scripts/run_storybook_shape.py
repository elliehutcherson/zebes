"""Bounded Hunyuan3D shape trial, retaining inputs, settings and raw geometry.

Run against an explicitly supplied external checkout and isolated dependencies.
Provider/model downloads live here, outside the self-contained experiments.
"""

import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024*1024), b""):
            result.update(chunk)
    return result.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def new_output(path):
    if path.exists():
        raise ValueError(f"Output already exists; preserve it and choose a new path: {path}")
    path.mkdir(parents=True)


def prepare(args):
    from PIL import Image
    from hy3dgen.rembg import BackgroundRemover
    from hy3dgen.shapegen.preprocessors import ImageProcessorV2

    new_output(args.out)
    source = Image.open(args.input).convert("RGB")
    masked = BackgroundRemover()(source)
    if masked.mode != "RGBA" or masked.getchannel("A").getextrema() != (0, 255):
        raise ValueError("Foreground preprocessing did not produce a usable alpha mask")
    masked.save(args.out / "masked-input.png")
    processed = ImageProcessorV2(size=512)(masked, border_ratio=0.15, to_tensor=False)
    Image.fromarray(processed["image"]).save(args.out / "conditioning-image.png")
    Image.fromarray(processed["mask"][..., 0]).save(args.out / "conditioning-mask.png")
    shutil.copyfile(args.input, args.out / "source.png")
    write_json(args.out / "inputs.json", {
        "source_sha256": digest(args.input),
        "masked_sha256": digest(args.out / "masked-input.png"),
        "conditioning_sha256": digest(args.out / "conditioning-image.png"),
        "mask_sha256": digest(args.out / "conditioning-mask.png"),
        "processor": "Hunyuan ImageProcessorV2, 512px, border_ratio=0.15",
        "background_remover": "Hunyuan BackgroundRemover / rembg default u2net",
        "source_size": source.size,
    })
    print(f"Prepared exact input image and foreground mask: {args.out}", flush=True)


def download(args):
    from huggingface_hub import HfApi, hf_hub_download

    new_output(args.out)
    revision = args.revision or HfApi().model_info(args.model).sha
    files = {}
    for filename in ("config.yaml", "model.fp16.safetensors"):
        source = hf_hub_download(args.model, f"{args.subfolder}/{filename}", revision=revision,
                                 cache_dir=str(args.cache))
        destination = args.out / filename
        shutil.copyfile(source, destination)
        files[filename] = digest(destination)
        print(f"Downloaded {filename}", flush=True)
    write_json(args.out / "model.json", {"model": args.model, "revision": revision,
                                         "subfolder": args.subfolder, "sha256": files})


def validate_prepared(directory):
    manifest = json.loads((directory / "inputs.json").read_text())
    for name, key in (("source.png", "source_sha256"), ("masked-input.png", "masked_sha256"),
                      ("conditioning-image.png", "conditioning_sha256"), ("conditioning-mask.png", "mask_sha256")):
        if digest(directory / name) != manifest[key]:
            raise ValueError(f"Prepared input changed: {name}")
    return manifest


def generate(args):
    import numpy as np
    import torch
    from PIL import Image
    from hy3dgen.shapegen import Hunyuan3DDiTFlowMatchingPipeline

    inputs = validate_prepared(args.input)
    model = json.loads((args.weights / "model.json").read_text())
    for name, expected in model["sha256"].items():
        if digest(args.weights / name) != expected:
            raise ValueError(f"Model file changed: {name}")
    if not torch.cuda.is_available():
        raise RuntimeError("This bounded trial requires the CUDA render machine")
    free, total = torch.cuda.mem_get_info()
    if free < 7 * 1024**3:
        raise RuntimeError(f"Less than 7GiB free GPU memory: {free / 1024**3:.2f}GiB")
    new_output(args.out)
    started = time.monotonic()
    receipt = {"model": model, "input": inputs, "seed": args.seed, "steps": args.steps,
               "resolution": args.resolution, "guidance_scale": 5.5, "num_chunks": 8000,
               "code_revision": subprocess.check_output(["git", "-C", str(args.repo), "rev-parse", "HEAD"], text=True).strip(),
               "runner_sha256": digest(__file__), "device": torch.cuda.get_device_name(),
               "initial_free_bytes": free, "gpu_total_bytes": total,
               "packages": {name: importlib.metadata.version(name) for name in
                            ("torch", "torchvision", "transformers", "diffusers", "trimesh", "scikit-image")},
               "status": "running"}
    write_json(args.out / "receipt.json", receipt)
    try:
        pipeline = Hunyuan3DDiTFlowMatchingPipeline.from_single_file(
            str(args.weights / "model.fp16.safetensors"), str(args.weights / "config.yaml"),
            device="cpu", dtype=torch.float16, use_safetensors=True)
        # This plain upstream pipeline copied Diffusers' offload helper but
        # does not define its expected components mapping. Declare the three
        # modules that the inspected offload sequence actually owns.
        pipeline.components = {name: getattr(pipeline, name) for name in ('conditioner', 'model', 'vae')}
        pipeline.enable_model_cpu_offload(device="cuda")
        # Sampling helpers also read the old CPU device. Use the hooks' actual
        # execution device without moving every module back to the GPU.
        pipeline.device = pipeline._execution_device
        if pipeline.device.type != "cuda":
            raise RuntimeError("CPU offload hooks did not select a CUDA execution device")
        image = Image.open(args.input / "masked-input.png").convert("RGBA")
        prepared = pipeline.image_processor(image, to_tensor=False)
        expected = Image.open(args.input / "conditioning-image.png").convert("RGB")
        if not np.array_equal(prepared["image"], np.asarray(expected)):
            raise ValueError("Actual model preprocessing differs from the inspected conditioning image")
        print("Starting one shape-generation request", flush=True)
        mesh = pipeline(image=image, num_inference_steps=args.steps, guidance_scale=5.5,
                        generator=torch.Generator(device="cuda").manual_seed(args.seed),
                        octree_resolution=args.resolution, num_chunks=8000, mc_algo="mc")[0]
        if mesh is None or not len(mesh.vertices) or not np.isfinite(mesh.vertices).all():
            raise ValueError("The generator returned an invalid/empty mesh")
        mesh.export(args.out / "raw.glb")
        mesh.export(args.out / "raw.ply")
        components = mesh.split(only_watertight=False)
        receipt.update(status="complete", elapsed_seconds=time.monotonic()-started,
                       vertices=len(mesh.vertices), faces=len(mesh.faces), watertight=bool(mesh.is_watertight),
                       bounds=mesh.bounds.tolist(), components=sorted(
                           [{"vertices": len(c.vertices), "faces": len(c.faces), "area": float(c.area),
                             "bounds": c.bounds.tolist()} for c in components], key=lambda c: -c["faces"]),
                       peak_gpu_allocated_bytes=torch.cuda.max_memory_allocated(),
                       raw_sha256={name: digest(args.out / name) for name in ("raw.glb", "raw.ply")})
        shutil.copyfile(__file__, args.out / "run_storybook_shape.py")
        print(f"Generated {len(mesh.vertices)} vertices, {len(mesh.faces)} faces, {len(components)} components", flush=True)
    except Exception as error:
        receipt.update(status="failed", elapsed_seconds=time.monotonic()-started,
                       error_type=type(error).__name__, error=str(error))
        raise
    finally:
        write_json(args.out / "receipt.json", receipt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--input", type=Path, required=True)
    prep.add_argument("--out", type=Path, required=True)
    weights = commands.add_parser("download")
    weights.add_argument("--model", default="tencent/Hunyuan3D-2")
    weights.add_argument("--subfolder", default="hunyuan3d-dit-v2-0")
    weights.add_argument("--revision", help="Pinned Hugging Face commit; resolve current revision once when omitted")
    weights.add_argument("--cache", type=Path, required=True)
    weights.add_argument("--out", type=Path, required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("--input", type=Path, required=True)
    gen.add_argument("--weights", type=Path, required=True)
    gen.add_argument("--out", type=Path, required=True)
    gen.add_argument("--seed", type=int, default=42)
    gen.add_argument("--steps", type=int, default=50)
    gen.add_argument("--resolution", type=int, default=384)
    args = parser.parse_args()
    if not (args.repo / "hy3dgen/shapegen/pipelines.py").is_file():
        raise ValueError("An external Hunyuan3D-2 checkout is required")
    sys.path.insert(0, str(args.repo.resolve()))
    {"prepare": prepare, "download": download, "generate": generate}[args.command](args)


if __name__ == "__main__":
    main()
