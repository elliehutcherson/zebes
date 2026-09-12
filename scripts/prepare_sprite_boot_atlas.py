"""Bake reusable material tiles and rasterize the C++ boot-view atlas contract.

No model calls. Original generation evidence is read-only. The atlas and its
pose-independent paint recipe are shared by every leg and a second motion.
"""

import argparse
import json
import math
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, ImageDraw, ImageFilter

from scripts.prepare_sprite_sequence import (
    DOCUMENT, LANDMARKS, RENDER, ROOT, TRACE, digest, font, geometry_rows,
    make_grid, white, write_json,
)
from scripts.sprite_material_atlas import SOURCE_SHA256, samples


EXPERIMENT = ROOT / "experiments/sprite_sequence"
CONFIG = EXPERIMENT / "boot-atlas-v1.json"
DONOR = EXPERIMENT / "evidence/profile-v2/near-01-06-raw.png"
MATERIALS = ("trousers", "shaft", "profile", "sole_visible")
TILE = 128
SCALE = 3


def atlas_rows(document, trace, landmarks, config):
    if config["version"] != 1:
        raise ValueError("unsupported boot atlas version")
    if [f["name"] for f in config["frames"]] != [f["name"] for f in document["frames"]]:
        raise ValueError("atlas must cover the same ordered twelve frames")
    controls = geometry_rows(document, trace, landmarks).splitlines()
    rows = []
    for index, row in enumerate(controls):
        fields = row.split()
        fields[-2:] = [str(config["shaft_length"]), str(config["shaft_width"])]
        frame = config["frames"][index // 2]
        drop = config["heel_drop_working_px"].get(frame["name"], {"near": 0, "far": 0})[fields[1]]
        rows.append(" ".join(fields + frame[fields[1]] + [str(drop)]))
    return "\n".join(rows) + "\n"


def bake_atlas(donor):
    """Surface-local lighting/folds are authored once, never from posed bounds."""
    source = samples(donor)
    result = Image.new("RGB", (TILE * len(MATERIALS), TILE))
    for index, name in enumerate(MATERIALS):
        sample = source["cloth" if name == "trousers" else "leather"]
        patch = sample.resize((TILE, TILE), Image.Resampling.BICUBIC)
        pixels = []
        for y in range(TILE):
            v = y / (TILE - 1)
            for x in range(TILE):
                u = x / (TILE - 1)
                color = patch.getpixel((x, y))
                if name == "trousers":
                    light = 0.73 + 0.42 * math.sin(math.pi * u)
                    fold = math.exp(-((v - 0.76 - 0.12*math.cos(u*5)) / 0.035)**2)
                    light -= 0.22 * fold
                elif name == "shaft":
                    light = 0.65 + 0.55 * math.sin(math.pi * u)
                    light += 0.2 * math.exp(-((v-0.10)/0.027)**2)
                    light -= 0.26 * math.exp(-((v-0.18)/0.018)**2)
                    light -= 0.12 * math.exp(-((v-0.76+0.07*math.cos(u*7))/0.025)**2)
                else:
                    light = 0.67 + 0.49 * math.sin(math.pi*v/1.5)
                    light += 0.13 * math.exp(-((u-0.73)/0.16)**2)
                    if v < (0.48 if name == "sole_visible" else 0.12):
                        color = (63, 43, 29)
                        light = 0.85 + 0.25*math.sin(math.pi*u)
                        if name == "sole_visible" and 0.10 < v < 0.37:
                            light += 0.35 if int(u*9) % 2 else -0.17
                    elif v < (0.53 if name == "sole_visible" else 0.16):
                        light = 1.28
                pixels.append(tuple(max(0, min(255, round(c*light))) for c in color))
        tile = Image.new("RGB", (TILE, TILE))
        tile.putdata(pixels)
        result.paste(tile, (index*TILE, 0))
    return result


def surface_mask(surface, size=256, scale=SCALE):
    mask = Image.new("L", (size*scale, size*scale))
    ImageDraw.Draw(mask).polygon([(x*scale, y*scale) for x, y in surface["outline"]], fill=255)
    return mask


def raster_surface(surface, atlas, scale=SCALE):
    index = MATERIALS.index(surface["material"])
    tile = atlas.crop((index*TILE, 0, (index+1)*TILE, TILE))
    ox, oy = surface["origin"]
    ux, uy = surface["u"]
    vx, vy = surface["v"]
    det = ux*vy-uy*vx
    if abs(det) < 1e-6:
        raise ValueError("C++ surface has collapsed texture coordinates")
    factor = (TILE-1)/det
    transform = (vy*factor/scale, -vx*factor/scale, (-ox*vy+oy*vx)*factor,
                 -uy*factor/scale, ux*factor/scale, (ox*uy-oy*ux)*factor)
    image = tile.transform((256*scale, 256*scale), Image.Transform.AFFINE, transform,
                           Image.Resampling.BICUBIC, fillcolor=tile.getpixel((64, 64))).convert("RGBA")
    image.putalpha(surface_mask(surface, scale=scale))
    return image


def render_leg(leg, atlas):
    image = Image.new("RGBA", (256*SCALE, 256*SCALE))
    for materials, ink in (({"trousers"}, (40, 35, 26)), ({"shaft", "profile", "sole_visible"}, (53, 32, 22))):
        layer = Image.new("RGBA", image.size)
        for surface in leg["surfaces"]:
            if surface["material"] in materials:
                layer.alpha_composite(raster_surface(surface, atlas))
        mask = layer.getchannel("A")
        rim = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(2*SCALE+1)))
        layer.paste((*ink, 255), mask=rim)
        image.alpha_composite(layer)
    if leg["side"] == "far":
        shade = Image.new("RGBA", image.size, (12, 18, 16, 35))
        shade.putalpha(image.getchannel("A").point(lambda v: round(v*0.12)))
        image.alpha_composite(shade)
    return image.resize((256, 256), Image.Resampling.LANCZOS)


def shifted(image, translation):
    dx, dy = translation
    return image.transform(image.size, Image.Transform.AFFINE, (1, 0, -dx, 0, 1, -dy),
                           Image.Resampling.BICUBIC)


def coat_image(image, control):
    attachment = control["attachment_y"]
    mesh = []
    for x, scale in enumerate(control["column_scale"]):
        if not 0.45 <= scale <= 1 or not 0 < attachment < 256:
            raise ValueError("invalid C++ coat deformation")
        mesh.append(((x, 0, x+1, attachment), (x, 0, x, attachment, x+1, attachment, x+1, 0)))
        bottom = attachment + (256-attachment)/scale
        mesh.append(((x, attachment, x+1, 256), (x, attachment, x, bottom, x+1, bottom, x+1, attachment)))
    result = image.transform(image.size, Image.Transform.MESH, mesh, Image.Resampling.BICUBIC)
    # Pillow's RGBA resampling can round color even in an identity mesh. The
    # attachment contract requires the unchanged torso pixels, including alpha.
    result.paste(image.crop((0, 0, 256, attachment)), (0, 0))
    return result


def compose(frame, geometry, parts, coat=True):
    whole = Image.new("RGBA", (256, 256))
    lower = whole.copy()
    for name in frame["draw_order"]:
        if name in ("near_boot", "far_boot"):
            continue
        if name in ("near_leg", "far_leg"):
            part = parts[name.split("_")[0]]
            lower.alpha_composite(part)
        else:
            part = Image.open(RENDER / "part-poses" / name / (frame["name"] + ".png")).convert("RGBA")
            if name == "body" and coat:
                part = coat_image(part, geometry["coat"])
        whole.alpha_composite(part)
    return [shifted(im, geometry["translation"]) for im in (whole, lower)]


def overlay(image, geometry):
    result = white(image)
    draw = ImageDraw.Draw(result)
    draw.line((35, 214, 220, 214), fill=(183, 171, 144))
    dx, dy = geometry["translation"]
    for leg in geometry["legs"]:
        color = (223, 109, 30) if leg["side"] == "near" else (36, 134, 205)
        a = {k: (p[0]+dx, p[1]+dy) for k, p in leg["anchors"].items()}
        draw.line([a[k] for k in ("hip", "knee", "ankle", "toe")], fill=color, width=1)
        for key in ("ankle", "cuff", "heel", "toe"):
            x, y = a[key]
            draw.ellipse((x-1.5, y-1.5, x+1.5, y+1.5), fill=color)
        if leg["contact_mode"] != "none":
            x, y = a["contact"]
            draw.ellipse((x-3, y-3, x+3, y+3), outline=(182, 30, 117), width=1)
    return result.convert("RGBA")


def animate(frames, path, size):
    frames = [white(im).resize((size, size), Image.Resampling.NEAREST) for im in frames]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=1000/12,
                   loop=0, disposal=0, blend=0)


def review_artifacts(output, geometry, flex, frames):
    crop = (85, 135, 205, 215)
    images, labels = [], []
    for name, side in (("reference_10", "near"), ("reference_04", "far"),
                       ("reference_01", "near"), ("reference_07", "far")):
        image = white(Image.open(output / "parts" / f"{name}-{side}.png"))
        images.append(image.crop(crop).resize((480, 320), Image.Resampling.NEAREST))
        labels.append(name + " / " + side)
    make_grid(images, labels, 2, (480, 348)).save(output / "boot-views.png")
    original = EXPERIMENT / "evidence/profile-v2"
    control = Image.open(original / "parts/reference_10/near.png").convert("RGBA")
    raw = Image.open(original / "results/generated-parts/reference_10-near.png").convert("RGBA")
    new = Image.open(output / "parts/reference_10-near.png").convert("RGBA")
    make_grid([white(im).crop(crop).resize((480, 320), Image.Resampling.NEAREST) for im in (control, raw, new)],
              ["Accepted profile starting point", "Retained raw sheet artwork", "Rounded reusable atlas"], 3, (480, 348)).save(output / "recovery-comparison.png")
    contacts = []
    for frame in geometry["frames"]:
        active = [leg for leg in frame["legs"] if leg["contact_mode"] != "none"]
        bottom = max(p[1] for leg in frame["legs"] for s in leg["surfaces"] for p in s["outline"])
        contacts.append({"name": frame["name"], "support": active[0]["side"]+"_"+active[0]["contact_mode"] if active else "flight",
                         "translation_y": frame["translation"][1], "clearance": 214-bottom-frame["translation"][1]})
    summary = {"frame_count": 12, "leg_count": 24, "contacts": contacts,
               "maximum_ground_penetration_px": max(0, -min(f["clearance"] for f in contacts)),
               "interpretation": "Exact authored control is imposed by C++; this does not score generator obedience or imply final art acceptance."}
    write_json(output / "summary.json", summary)
    data = {"names": [f["name"] for f in geometry["frames"]], "contacts": contacts,
            "geometry": geometry["frames"], "reuse": flex["frames"]}
    template = Path(__file__).with_name("sprite_boot_atlas_review.html")
    (output / "review.html").write_text(template.read_text().replace("__DATA__", json.dumps(data)))
    preview = [white(im).resize((96, 96), Image.Resampling.NEAREST).resize((384, 384), Image.Resampling.NEAREST) for im in frames]
    preview[0].save(output / "preview-96.png", save_all=True, append_images=preview[1:], duration=1000/12, loop=0, disposal=0, blend=0)


def prepare(binary, output):
    if output.exists():
        raise ValueError("output exists; preserve evidence and choose a new revision")
    if digest(DONOR) != SOURCE_SHA256:
        raise ValueError("retained material donor changed")
    document, trace, landmarks, config = [json.loads(p.read_text()) for p in (DOCUMENT, TRACE, LANDMARKS, CONFIG)]
    rows = atlas_rows(document, trace, landmarks, config)
    result = subprocess.run([str(binary.resolve()), "--atlas"], input=rows, text=True, capture_output=True)
    if result.returncode:
        raise ValueError(result.stderr.strip())
    geometry = json.loads(result.stdout)
    atlas = bake_atlas(Image.open(DONOR))
    flex_result = subprocess.run([str(binary.resolve()), "--atlas-flex"], input=rows,
                                 text=True, capture_output=True)
    if flex_result.returncode:
        raise ValueError(flex_result.stderr.strip())
    flex = json.loads(flex_result.stdout)
    output.mkdir(parents=True)
    atlas.save(output / "atlas.png")
    (output / "geometry-input.txt").write_text(rows)
    write_json(output / "geometry.json", geometry)
    write_json(output / "reuse-geometry.json", flex)
    sets = {name: [] for name in ("frames", "legs", "overlays", "fixed-coat", "reuse", "reuse-legs")}
    for kind in (*sets, "parts"):
        (output / kind).mkdir()
    for frame, posed, alternate in zip(document["frames"], geometry["frames"], flex["frames"], strict=True):
        parts = {leg["side"]: render_leg(leg, atlas) for leg in posed["legs"]}
        whole, lower = compose(frame, posed, parts)
        fixed, _ = compose(frame, posed, parts, coat=False)
        alternate_parts = {leg["side"]: render_leg(leg, atlas) for leg in alternate["legs"]}
        reused, reused_lower = compose(frame, alternate, alternate_parts)
        for kind, im in zip(sets, (whole, lower, overlay(whole, posed), fixed, reused, reused_lower), strict=True):
            sets[kind].append(im)
            im.save(output / kind / (frame["name"] + ".png"))
        for side, im in parts.items():
            im.save(output / "parts" / (frame["name"] + "-" + side + ".png"))
    labels = [f["name"] for f in document["frames"]]
    for kind, frames in sets.items():
        make_grid([white(im) for im in frames], labels, 6).save(output / (kind + "-sheet.png"))
        for size in (48, 96, 256):
            animate(frames, output / f"{kind}-{size}.png", size)
    review_artifacts(output, geometry, flex, sets["frames"])
    manifest = {
        "version": 1, "status": "authored atlas candidate; visual acceptance pending",
        "provider_requests": 0, "atlas_sha256": digest(output / "atlas.png"),
        "materials": {name: {"rect": [i*TILE, 0, TILE, TILE], "uv_space": "C++ surface basis"} for i, name in enumerate(MATERIALS)},
        "input_hashes": {str(p.relative_to(ROOT)): digest(p) for p in (DOCUMENT, TRACE, LANDMARKS, CONFIG, DONOR, Path(__file__), ROOT / "scripts/sprite_sequence_geometry.cc")},
        "baseline_hashes": {str(p.relative_to(ROOT)): digest(p) for p in sorted((RENDER / "part-poses").glob("*/*.png"))},
        "fps": 12, "working_canvas": [256, 256], "review_sizes": [48, 96, 256],
        "geometry_authority": "C++ anchors, rounded surfaces, UV coordinates, view and contact validation; entire character receives one C++ translation",
        "raw_evidence": "../profile-v2/results/review.html",
        "review_template_sha256": digest(Path(__file__).with_name("sprite_boot_atlas_review.html")),
        "raw_evidence_hashes": {str(p.relative_to(ROOT)): digest(p) for p in sorted((EXPERIMENT / "evidence/profile-v2").glob("*-raw.png"))},
        "reuse": "Second cycle rotates free shins twelve degrees in C++; same PNG atlas and view schedule, no per-frame artwork or model requests. A reuse proof, not accepted alternate animation.",
        "coat": "C++ authored lower-coat compression with cyclic three-frame lag. Original fixed-coat comparison retained. Torso and belt remain fixed before whole-character grounding.",
        "limitations": "Authored 2D views, estimated heels, provisional cloth response and inherited head artwork. Final art and logical resolution remain unaccepted."
    }
    write_json(output / "manifest.json", manifest)
    print(f"Prepared twelve frames with one reusable atlas at {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.geometry, args.output)


if __name__ == "__main__":
    main()
