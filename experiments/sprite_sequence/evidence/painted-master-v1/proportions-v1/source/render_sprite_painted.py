"""Sample complete raw paintings through C++ semantic registration at 512px.

No provider calls, per-cell bounds fitting, procedural shading or guide alpha.
The background extraction and all mapped assets are separate from raw images.
"""

import argparse
import gzip
import json
import math
from pathlib import Path
import subprocess
import shutil

import numpy as np
from PIL import Image, ImageChops, ImageDraw

from scripts.prepare_sprite_painted import CELL, EXPERIMENT, LEG_CROP, UPPER
from scripts.prepare_sprite_sequence import DOCUMENT, ROOT, digest, font, make_grid, white, write_json
from scripts.prepare_sprite_boot_atlas import shifted

LANDMARKS = EXPERIMENT / "painted-landmarks-v1.json"
MASTER = 512


def extract(image):
    rgb = np.asarray(image.convert("RGB"), dtype=float)
    alpha = np.clip((245-rgb.min(axis=2))/35, 0, 1)
    color = np.clip((rgb-255*(1-alpha[..., None]))/np.maximum(alpha[..., None], 1/255), 0, 255)
    return Image.fromarray(np.concatenate((color, alpha[..., None]*255), axis=2).round().astype(np.uint8))


def row(name, sources, targets):
    if len(sources) != len(targets):
        raise ValueError("source/target landmark count differs")
    return " ".join([name, str(len(sources))] + [format(v, ".15g") for s, t in zip(sources, targets, strict=True) for v in (*s, *t)])


def sole_observations(image, points):
    """Measure the retained painted outsole; do not infer any target geometry."""
    heel, toe, ankle = [np.array(points[i], dtype=float) for i in (4, 5, 3)]
    axis = (toe-heel)/np.linalg.norm(toe-heel)
    normal = np.array([-axis[1], axis[0]])
    if np.dot(normal, ankle-heel) > 0:
        normal = -normal
    y, x = np.where(np.asarray(image.getchannel("A")) >= 128)
    delta = np.column_stack((x, y))-heel
    along, across = delta@axis, delta@normal
    length = np.linalg.norm(toe-heel)
    result = []
    for fraction in np.linspace(0, 1, 9):
        band = (abs(along-fraction*length) <= 2) & (abs(across) <= 16)
        if not band.any():
            raise ValueError("observed sole landmark has no adjacent painted contour")
        depth = across[band].max()+0.5
        result.append((heel+axis*fraction*length+normal*depth).tolist())
    return result


def coat(image, control):
    attachment = control["attachment_y"]*2
    mesh = []
    for x, scale in enumerate(control["column_scale"]):
        left, right = x*2, (x+1)*2
        bottom = attachment + (MASTER-attachment)/scale
        mesh.append(((left, attachment, right, MASTER), (left, attachment, left, bottom, right, bottom, right, attachment)))
    result = image.transform(image.size, Image.Transform.MESH, mesh, Image.Resampling.BICUBIC)
    result.paste(image.crop((0, 0, MASTER, attachment)), (0, 0))
    return result


def animation(frames, path, size=512):
    frames = [im.resize((size, size), Image.Resampling.LANCZOS) for im in frames]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=1000/12, loop=0, disposal=0, blend=0)


def observations(inputs, output, landmarks):
    sheets = [Image.open(inputs / f"legs-{i}-raw.png").convert("RGB") for i in (1, 2, 3)]
    if any(im.size != (1448, 1086) for im in sheets):
        raise ValueError("native raw dimensions changed; observed landmarks require review")
    painted = []
    for index in range(12):
        sheet, cell = sheets[index//4], index % 4
        ox, oy = (cell % 2)*724, (cell // 2)*543
        image = extract(sheet.crop((ox, oy, ox+724, oy+543)))
        points = [[x-ox, y-oy] for x, y in landmarks["legs"][index]]
        image.save(output / "extracted" / f"leg-{index+1:02}.png")
        annotated = white(image)
        draw = ImageDraw.Draw(annotated)
        for key, (x, y) in zip(landmarks["leg_order"], points, strict=True):
            draw.ellipse((x-4, y-4, x+4, y+4), fill=(210, 30, 120))
            draw.text((x+7, y-12), key, fill=(125, 0, 65), font=font(16))
        annotated.save(output / "observations" / f"leg-{index+1:02}.png")
        painted.append((image, points))
    upper_raw = Image.open(inputs / "upper-raw.png").convert("RGB")
    if upper_raw.size != (1536, 1024):
        raise ValueError("upper raw dimensions changed")
    upper = {}
    for index, name in enumerate(UPPER):
        ox, oy = (index % 3)*512, (index // 3)*512
        image = extract(upper_raw.crop((ox, oy, ox+512, oy+512)))
        points = [[x-ox, y-oy] for x, y in landmarks["upper"][name]["points"]]
        image.save(output / "extracted" / (name+".png"))
        upper[name] = (image, points)
    return painted, upper


def render(binary, inputs, output, landmark_path=LANDMARKS, geometry_path=None, proportions=False):
    inputs, output, landmark_path = inputs.resolve(), output.resolve(), landmark_path.resolve()
    if output.exists():
        raise ValueError("output exists; choose a new revision to preserve evidence")
    geometry_path = (geometry_path or inputs / "geometry.json").resolve()
    geometry = json.loads(geometry_path.read_text())
    document = json.loads(DOCUMENT.read_text())
    landmarks = json.loads(landmark_path.read_text())
    if landmarks["version"] != 1:
        raise ValueError("unknown landmark version")
    output.mkdir(parents=True)
    snapshot = output / "source"
    snapshot.mkdir()
    for path in (landmark_path, Path(__file__), ROOT / "scripts/sprite_painted_registration.cc", ROOT / "scripts/sprite_sequence_geometry.cc"):
        shutil.copyfile(path, snapshot / path.name)
    for kind in ("extracted", "observations", "frames", "legs", "raw-frames", "raw-legs", "overlays", "parts", "maps"):
        (output / kind).mkdir()
    painted, upper = observations(inputs, output, landmarks)
    sets = {kind: [] for kind in ("frames", "legs", "raw-frames", "raw-legs", "overlays")}
    measurements, all_rows, mappings = [], [], []
    for index, (frame, posed) in enumerate(zip(document["frames"], geometry["frames"], strict=True)):
        rows, sources, raw_parts = [], {}, {}
        for leg in posed["legs"]:
            side = leg["side"]
            donor = index if side == "near" else (index+6) % 12
            source, points = painted[donor]
            name = side+"_leg"
            targets = [[v*2 for v in leg["anchors"][key]] for key in landmarks["leg_order"]]
            if leg["contact_mode"] != "none":
                contour = sole_observations(source, points)
                points = points[:4]+[contour[0], contour[-1]]+contour[1:-1]
                targets += [[v*2 for v in point] for point in leg["sole_samples"]]
            rows.append(row(name, points, targets))
            sources[name] = source
            # Exact sheet inversion, without any local correction. For the
            # reused far painting, retain its original near-pose placement.
            raw = Image.new("RGBA", (512, 512))
            native_scale = 1448/1536*6
            raw = source.transform((512, 512), Image.Transform.AFFINE,
                                   (native_scale/2, 0, -LEG_CROP[0]*native_scale,
                                    0, native_scale/2, -LEG_CROP[1]*native_scale), Image.Resampling.BICUBIC)
            raw_parts[name] = raw
            mappings.append({"frame": frame["name"], "part": name, "donor_phase": donor+1,
                             "source": points, "target_master": targets,
                             "maximum_landmark_correction_master_px": max(math.dist([LEG_CROP[0]*2+s[0]*2/native_scale, LEG_CROP[1]*2+s[1]*2/native_scale], t) for s, t in zip(points, targets, strict=True))})
        for name, (source, points) in upper.items():
            joints = landmarks["upper"][name]["joints"]
            targets = [[v*2 for v in frame["pose"][key]] for key in joints]
            rows.append(row(name, points, targets))
            sources[name] = source
        text = "\n".join(rows)+"\n"
        all_rows.append(text)
        command = [str(binary.resolve())] + (["--proportions"] if proportions else [])
        run = subprocess.run(command, input=text, text=True, capture_output=True)
        if run.returncode:
            raise ValueError(run.stderr)
        maps = json.loads(run.stdout)
        with gzip.open(output / "maps" / (frame["name"]+".json.gz"), "wt") as stream:
            stream.write(run.stdout)
        parts = {}
        for mapping in maps:
            name = mapping["name"]
            mesh = [(tuple(box), tuple(quad)) for box, quad in mapping["mesh"]]
            parts[name] = sources[name].transform((512, 512), Image.Transform.MESH, mesh, Image.Resampling.BICUBIC)
            parts[name].save(output / "parts" / (frame["name"]+"-"+name+".png"))
            measurements.append({"frame": frame["name"], "part": name,
                                 "landmark_residual_raw_px": mapping["landmark_residual_raw_px"],
                                 "width_controls": mapping.get("width_controls", [])})
        parts["body"] = coat(parts["body"], posed["coat"])
        whole, lower, raw_whole, raw_lower = [Image.new("RGBA", (512, 512)) for _ in range(4)]
        for name in frame["draw_order"]:
            if name.endswith("_boot"):
                continue
            whole.alpha_composite(parts[name])
            raw_whole.alpha_composite(raw_parts.get(name, parts[name]))
            if name.endswith("_leg"):
                lower.alpha_composite(parts[name])
                raw_lower.alpha_composite(raw_parts[name])
        translation = [v*2 for v in posed["translation"]]
        whole, lower, raw_whole, raw_lower = [shifted(im, translation) for im in (whole, lower, raw_whole, raw_lower)]
        over = white(whole).convert("RGBA")
        draw = ImageDraw.Draw(over)
        draw.line((50, 428, 460, 428), fill=(170, 145, 110), width=1)
        for leg in posed["legs"]:
            color = (230, 110, 20) if leg["side"] == "near" else (30, 145, 230)
            pins = {key: [point[i]*2+translation[i] for i in (0, 1)] for key, point in leg["anchors"].items()}
            draw.line([tuple(pins[k]) for k in ("hip", "knee", "cuff", "ankle", "toe")], fill=color, width=2)
            for key in ("heel", "ankle", "toe"):
                x, y = pins[key]
                draw.ellipse((x-3, y-3, x+3, y+3), fill=color)
        for kind, image in zip(sets, (whole, lower, raw_whole, raw_lower, over), strict=True):
            sets[kind].append(image)
            image.save(output / kind / (frame["name"]+".png"))
        print(frame["name"], flush=True)
    names = [frame["name"] for frame in document["frames"]]
    for kind, images in sets.items():
        animation(images, output / f"{kind}-512.png")
        animation(images, output / f"{kind}-96.png", 96)
        make_grid([white(im) for im in images], names, 4, (512, 540)).save(output / (kind+"-sheet.png"))
    write_json(output / "registration.json", {"legs": mappings, "residuals": measurements})
    for index, rows in enumerate(all_rows):
        (output / "maps" / f"reference_{index+1:02}-input.txt").write_text(rows)
    write_json(output / "manifest.json", {
        "version": 1, "canvas": [512, 512], "fps": 12, "frames": names,
        "proportions": proportions,
        "raw_sha256": {p.name: digest(p) for p in sorted(inputs.glob("*-raw.png"))},
        "source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in (landmark_path, DOCUMENT, Path(__file__), ROOT / "scripts/sprite_painted_registration.cc", geometry_path)},
        "sampling": "C++ inverse thin-plate mesh from explicit semantic pairs; two pins use similarity; native RGB/alpha artwork registered directly at 512px with bicubic interpolation, followed by coat deformation and whole-character grounding. No guide clipping or procedural paint.",
        "background": "Separate extraction: alpha=clamp((245-minRGB)/35), white unmatte; raw bytes untouched.",
        "reuse": "Twelve near-phase complete paintings supply both sides through their own original pose targets; far uses donor six phases later. Head, coat, arms and tail reuse five complete paintings.",
        "status": "full-resolution candidate; visual review and contact diagnostics required"
    })
    print(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registration", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--landmarks", type=Path, default=LANDMARKS)
    parser.add_argument("--geometry", type=Path)
    parser.add_argument("--proportions", action="store_true")
    args = parser.parse_args()
    render(args.registration, args.inputs, args.output, args.landmarks, args.geometry, args.proportions)
