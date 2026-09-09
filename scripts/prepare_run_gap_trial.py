"""Prepare reviewable gap-prefill inputs from the accepted C++ puppet render.

No provider calls. Source-space cuff annotations are transformed with each
rigid boot. Every variant uses the same mask, canvas, pose and original pixels
outside the declared repair region. Procedural paint is a conditioning input.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


VARIANTS = ("blank", "blur", "shaped")
PROTECTED = ("head", "body", "tail", "near_arm", "far_arm")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rigid_point(point, rest, pose, suffix):
    anchor, tip = "ankle_" + suffix, "toe_" + suffix
    a, b = rest[anchor], rest[tip]
    c, d = pose[anchor], pose[tip]
    if min(math.dist(a, b), math.dist(c, d)) <= 1e-6:
        raise ValueError("collapsed boot bone")
    if abs(math.dist(a, b) - math.dist(c, d)) > 1e-4:
        raise ValueError("cuff transfer requires a rigid, unscaled boot")
    turn = math.atan2(d[1] - c[1], d[0] - c[0]) - math.atan2(b[1] - a[1], b[0] - a[0])
    x, y = point[0] - a[0], point[1] - a[1]
    return [c[0] + x * math.cos(turn) - y * math.sin(turn),
            c[1] + x * math.sin(turn) + y * math.cos(turn)]


def maximum(images):
    result = Image.new("L", images[0].size)
    for image in images:
        result = ImageChops.lighter(result, image)
    return result


def capsule(draw, start, end, width, color):
    draw.line([tuple(start), tuple(end)], fill=color, width=width)
    radius = width / 2
    for x, y in (start, end):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)


def weighted_blur(source, radius, fallback):
    """Extend nearby character color without averaging in transparent white."""
    red, green, blue, alpha = source.split()
    blurred_alpha = alpha.filter(ImageFilter.GaussianBlur(radius))
    channels = [ImageChops.multiply(channel, alpha).filter(ImageFilter.GaussianBlur(radius))
                for channel in (red, green, blue)]
    result = Image.new("RGB", source.size)
    colors = []
    for r, g, b, a in zip(*(channel.getdata() for channel in channels), blurred_alpha.getdata(), strict=True):
        colors.append(tuple(min(255, round(value * 255 / a)) for value in (r, g, b)) if a else tuple(fallback))
    result.putdata(colors)
    return result


def make_inputs(source, parts, rest, pose, config):
    if source.size != tuple(config["canvas"]) or any(part.size != source.size for part in parts.values()):
        raise ValueError("source, part and annotation canvases must agree")
    size = source.size
    shapes, cuff_masks, annotations = {}, {}, {}
    paint = Image.new("RGBA", size)
    # Same far-before-near order as the lower-body layers of the current puppet.
    for side in ("far", "near"):
        settings = config["sides"][side]
        suffix = settings["joint_suffix"]
        cuff = [rigid_point(point, rest, pose, suffix) for point in settings["cuff"]]
        center = [(cuff[0][i] + cuff[1][i]) / 2 for i in (0, 1)]
        hip, knee = pose["hip_c"], pose["knee_" + suffix]
        shape = Image.new("L", size)
        draw = ImageDraw.Draw(shape)
        capsule(draw, hip, knee, settings["thigh_width"], 255)
        capsule(draw, knee, center, settings["calf_width"], 255)
        cuff_mask = Image.new("L", size)
        ImageDraw.Draw(cuff_mask).line([tuple(point) for point in cuff], fill=255, width=5)
        shapes[side], cuff_masks[side] = shape, cuff_mask
        layer = Image.new("RGBA", size, tuple(settings["outline"]) + (0,))
        layer.putalpha(shape)
        interior = shape.filter(ImageFilter.MinFilter(3))
        layer.paste(tuple(settings["color"]) + (255,), mask=interior)
        paint = Image.alpha_composite(paint, layer)
        annotations[side] = {"cuff": cuff, "cuff_center": center, "knee": knee,
                             "ankle": pose["ankle_" + suffix], "toe": pose["toe_" + suffix]}
    full_shape = maximum(list(shapes.values()))
    # Painted bridge goes behind existing pixels, preserving the original art.
    candidate = Image.alpha_composite(paint, source)
    source_alpha = source.getchannel("A")
    missing = ImageChops.multiply(full_shape, ImageChops.invert(source_alpha))
    missing = missing.point(lambda value: 255 if value > 16 else 0)
    margin = config["seam_margin"]
    repair = maximum([missing.filter(ImageFilter.MaxFilter(2 * margin + 1)), *cuff_masks.values()])
    protected = maximum([parts[name].getchannel("A") for name in PROTECTED])
    for side in ("far", "near"):
        boot = parts[side + "_boot"].getchannel("A")
        protected = ImageChops.lighter(protected, ImageChops.subtract(boot, cuff_masks[side]))
    protected = protected.point(lambda value: 255 if value else 0)
    repair = ImageChops.subtract(repair, protected).point(lambda value: 255 if value else 0)
    missing = ImageChops.darker(missing, repair)
    white = Image.new("RGBA", size, "white")
    blank = Image.alpha_composite(white, source).convert("RGB")
    shaped = Image.composite(Image.alpha_composite(white, candidate).convert("RGB"), blank, repair)
    blurred = weighted_blur(source, config["blur_radius"], (48, 42, 33))
    blur = Image.composite(blurred, blank, missing)
    variants = {"blank": blank, "blur": blur, "shaped": shaped}
    for variant in variants.values():
        difference = maximum(list(ImageChops.difference(blank, variant).split()))
        if ImageChops.subtract(difference, repair).getbbox():
            raise ValueError("prefill modified protected pixels")
    return variants, repair, missing, protected, annotations


def prepare(args):
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError("output must be empty; use a new revision instead of replacing reviewed inputs")
    document = json.loads(args.document.read_text())
    config = json.loads(args.config.read_text())
    if digest(args.document) != config["document_sha256"]:
        raise ValueError("document changed since cuff annotation; review a new configuration")
    names = [frame["name"] for frame in document["frames"]]
    if names != [f"reference_{i:02d}" for i in range(1, 13)]:
        raise ValueError("requires the accepted twelve ordered reference frames")
    render_manifest = json.loads((args.render / "manifest.json").read_text())
    if render_manifest["poses"] != names:
        raise ValueError("render frame order differs from the current document")
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"version": 1, "status": "awaiting input review; no generation submitted", "provider_calls": 0,
                "document_sha256": digest(args.document), "config_sha256": digest(args.config),
                "render_manifest_sha256": digest(args.render / "manifest.json"),
                "canvas": config["canvas"], "generation_canvas": config["generation_canvas"],
                "variants": list(VARIANTS), "mask_convention": "opaque grayscale PNG: white=edit; black=preserve",
                "prefill": "procedural conditioning paint, not final art", "frames": []}
    for frame in document["frames"]:
        name = frame["name"]
        directory = args.output / name
        directory.mkdir()
        source_path = args.render / "working" / (name + ".png")
        source = Image.open(source_path).convert("RGBA")
        parts = {part["name"]: Image.open(args.render / "part-poses" / part["name"] / (name + ".png")).convert("RGBA")
                 for part in document["parts"]}
        variants, repair, missing, protected, annotations = make_inputs(source, parts, document["rest_pose"], frame["pose"], config)
        source.save(directory / "source.png")
        for label, value in {"repair-mask": repair, "missing-region": missing, "protected": protected}.items():
            value.save(directory / (label + ".png"))
        repair.resize(tuple(config["generation_canvas"]), Image.Resampling.NEAREST).save(directory / "mask-1024.png")
        hashes = {}
        for variant, image in variants.items():
            image.save(directory / (variant + ".png"), optimize=True)
            path = directory / (variant + "-1024.png")
            image.resize(tuple(config["generation_canvas"]), Image.Resampling.NEAREST).save(path, optimize=True)
            hashes[variant] = digest(path)
        manifest["frames"].append({"name": name, "source_sha256": digest(source_path), "inputs_sha256": hashes,
                                   "mask_sha256": digest(directory / "mask-1024.png"), "annotations": annotations,
                                   "repair_pixels": sum(value > 0 for value in repair.getdata()),
                                   "missing_pixels": sum(value > 0 for value in missing.getdata())})
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared 12 poses × 3 image inputs at {args.output}; zero generation calls.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--render", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    prepare(parser.parse_args())


if __name__ == "__main__":
    main()
