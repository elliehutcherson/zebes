"""Prepare and retain a four-pose redraw trial; no provider calls in this script."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

if __package__:
    from .pose_cleanup_pilot import extract_white_matte
else:
    from pose_cleanup_pilot import extract_white_matte


FRAMES = ("reference_01", "reference_07", "reference_10", "reference_12")
PROMPT = """Use case: precise-object-edit
Asset: one finished pixel-art mouse animation frame on a plain white 1024 x 1024 canvas.
Image 1 is the EDIT TARGET: the exact posed mouse. Its lower legs and boots are rough material-colored geometry guides. Image 2 is the finished character APPEARANCE REFERENCE only; do not copy its pose. Image 3 is an editing-region guide: white permits redraw, black means preserve.
Replace the rough dark trouser shapes and block-shaped brown boots in Image 1 with beautifully finished artwork matching Image 2. Draw thick, stout trouser legs and chunky rounded brown leather boots, with convincing volume, restrained fabric folds, leather shading, boot cuffs, toe boxes and dark soles. Keep the substantial thickness of the guide. The calf must enter the boot opening coherently, without a gap or severed ankle.
The entire legs and boots inside the white region may be redrawn. Preserve their guide pose, knee bends, foot directions, heel/toe locations, overlaps and the surfaces visible to the camera. The guide shows the required view of each boot, including whether its underside is visible. Keep the near and far legs distinct. Round and finish the crude corners; do not simply texture the cuboids. Match the character's existing pixel clusters and outline style.
Preserve every other part of Image 1: head, ears, face, green hood, coat, red scarf, belt, arms, hands and tail. Do not redesign or reposition the character. Do not change its scale, canvas, camera or motion phase. Do not create extra limbs. Do not paint the mask or guide colors. Keep the white negative spaces between legs and the fixed clothing boundaries.
Return only one finished frame, on the same whole 1024 x 1024 plain white canvas. No checkerboard, text, labels, ground line, ground shadow, scene or sprite sheet.
Pose-specific requirement: {pose}
"""
POSES = {
    "reference_01": "Keep the forward boot on screen-right extended with its underside visible; the trailing boot on screen-left points downward and does not expose its underside.",
    "reference_07": "Keep the opposing contact phase exactly as drawn. The forward boot on screen-right exposes its underside and the trailing boot on screen-left does not; preserve this phase's different leg overlap and hand pose.",
    "reference_10": "Keep one boot planted flat at the low point on screen-right, and the other leg folded back with its boot tucked high on screen-left. Do not turn this into a wide split stride or place both feet at the same height.",
    "reference_12": "Both feet are airborne. Preserve their different heights and directions from the guide; do not lower either boot, add a ground shadow or turn this into a grounded contact pose.",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(args):
    if args.output.exists():
        raise ValueError("output exists; preserve submitted/reviewed revisions")
    args.output.mkdir(parents=True)
    shutil.copyfile(args.identity, args.output / "identity.png")
    manifest = {"method": "built-in image_gen full-leg/boot redraw from stouter material guides",
                "model_id": None, "model_note": "the built-in tool does not expose a model identifier or seed",
                "authorization": "user approved v1 views as generator inputs with the requested stouter v2 legs and boots",
                "surface_id_image_sent": False, "mask_delivery": "ordinary reference image; not a hard provider inpainting mask",
                "provider_calls": 0, "identity_sha256": digest(args.identity), "frames": []}
    for name in FRAMES:
        directory = args.output / name
        directory.mkdir()
        for src, dst in (("geometry-1024.png", "edit-input.png"), ("mask-1024.png", "region-guide.png"),
                         ("geometry.png", "geometry.png"), ("mask.png", "repair-mask.png"), ("original.png", "original.png")):
            shutil.copyfile(args.guides / name / src, directory / dst)
        shutil.copyfile(args.render / "working" / (name + ".png"), directory / "source-rgba.png")
        prompt = PROMPT.format(pose=POSES[name])
        (directory / "prompt.txt").write_text(prompt)
        manifest["frames"].append({"name": name, "status": "prepared", "prompt": prompt,
                                   "edit_input_sha256": digest(directory / "edit-input.png"),
                                   "mask_sha256": digest(directory / "region-guide.png")})
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Prepared four redraw targets; no provider calls.")


def review(args):
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    entry = next(frame for frame in manifest["frames"] if frame["name"] == args.frame)
    if entry["status"] not in ("prepared", "received"):
        raise ValueError("frame output already recorded")
    directory = args.output / args.frame
    raw_path = directory / "raw-output.png"
    if entry["status"] == "prepared":
        shutil.copyfile(args.raw, raw_path)
        entry.update(status="received", raw_source=str(args.raw), raw_sha256=digest(raw_path))
        manifest["provider_calls"] += 1
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    elif digest(args.raw) != entry["raw_sha256"]:
        raise ValueError("cannot replace a received provider result")
    raw = Image.open(raw_path).convert("RGBA")
    if raw.width != raw.height:
        raise ValueError("non-square output requires separate registration review")
    base = Image.open(directory / "original.png").convert("RGBA")
    mask = Image.open(directory / "repair-mask.png").convert("L")
    mapped = Image.alpha_composite(Image.new("RGBA", raw.size, "white"), raw).resize((256, 256), Image.Resampling.NEAREST)
    mapped.save(directory / "canvas-mapped.png")
    composite = Image.composite(mapped, base, mask)
    # Gray pixels are inside the declared feathered repair region. Only black
    # pixels are protected; a fractional mask still permits a fractional edit.
    outside = mask.point(lambda value: 255 if value == 0 else 0)
    if any(ImageChops.multiply(channel, outside).getbbox() for channel in ImageChops.difference(composite, base).split()):
        raise ValueError("outside-mask pixels changed")
    composite.save(directory / "composite.png")
    extracted = extract_white_matte(composite, Image.open(directory / "source-rgba.png").convert("RGBA"))
    extracted.save(directory / "composite-rgba.png")
    extracted.resize((48, 48), Image.Resampling.NEAREST).save(directory / "native-48.png")
    entry.update(status="complete", raw_source=str(args.raw), raw_sha256=digest(raw_path), raw_size=list(raw.size),
                 raw_alpha_extrema=raw.getchannel("A").getextrema(), outside_mask_changed_pixels=0,
                 registration="uniform whole-canvas scale to 256; no per-character fitting")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Retained {args.frame}: native {raw.size}, fixed-canvas composite and 48px RGBA preview.")


def wider_comparison(args):
    """Post-hoc compositing comparison, explicitly separate from the sent mask."""
    document = json.loads(args.document.read_text())
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    poses = {frame["name"]: frame["pose"] for frame in document["frames"]}
    for entry in manifest["frames"]:
        if entry["status"] != "complete":
            raise ValueError("all requested outputs must be retained first")
        directory = args.output / entry["name"]
        destination = directory / "wide-composite.png"
        if destination.exists():
            raise ValueError("broader comparison already exists")
        base = Image.open(directory / "original.png").convert("RGBA")
        mask = Image.new("L", base.size)
        # Same anatomical-region rule across every pose. No fitting to generated
        # bounds, color segmentation, translation or output-dependent tracing.
        bounds = [40, int(poses[entry["name"]]["hip_c"][1] - 8), 220, 230]
        ImageDraw.Draw(mask).rectangle(bounds, fill=255)
        protected = Image.new("L", base.size)
        for name in ("body", "head", "tail", "near_arm", "far_arm"):
            alpha = Image.open(args.render / "part-poses" / name / (entry["name"] + ".png")).convert("RGBA").getchannel("A")
            protected = ImageChops.lighter(protected, alpha)
        protected = protected.point(lambda value: 255 if value else 0)
        mask = ImageChops.subtract(mask, protected)
        mapped = Image.open(directory / "canvas-mapped.png").convert("RGBA")
        composite = Image.composite(mapped, base, mask)
        mask.save(directory / "wide-mask.png")
        composite.save(destination)
        extracted = extract_white_matte(composite, Image.open(directory / "source-rgba.png").convert("RGBA"))
        extracted.save(directory / "wide-composite-rgba.png")
        extracted.resize((48, 48), Image.Resampling.NEAREST).save(directory / "wide-native-48.png")
        original_mask = Image.open(directory / "repair-mask.png").convert("L")
        extra = ImageChops.subtract(mask, original_mask.point(lambda value: 255 if value else 0))
        # Diagnose visible generated content admitted outside the original mask;
        # this can include an expanded coat silhouette, so visual review matters.
        nonwhite = mapped.convert("RGB").point(lambda value: 255 if value < 225 else 0)
        channels = nonwhite.split()
        foreground = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
        count = sum(value > 0 for value in ImageChops.darker(extra, foreground).getdata())
        entry["wider_comparison"] = {"status": "post-hoc comparison; not the provider's input mask",
                                      "region": bounds, "protected_parts": ["body", "head", "tail", "near_arm", "far_arm"],
                                      "additional_nonwhite_pixels": count,
                                      "caveat": "Existing protected-part pixels remain exact; newly drawn pixels outside their old silhouettes may still appear."}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Retained four broader composites separately; no additional generation.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare")
    for name in ("guides", "identity", "render", "output"):
        prep.add_argument("--" + name, required=True, type=Path)
    report = sub.add_parser("review")
    report.add_argument("--output", required=True, type=Path)
    report.add_argument("--frame", required=True, choices=FRAMES)
    report.add_argument("--raw", required=True, type=Path)
    wide = sub.add_parser("wider-comparison")
    for name in ("document", "render", "output"):
        wide.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    {"prepare": prepare, "review": review, "wider-comparison": wider_comparison}[args.action](args)


if __name__ == "__main__":
    main()
