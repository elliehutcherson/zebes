"""Render a simple 3D boot as reviewable view guides, not generated final art.

Orthographic projection and a z buffer determine which boot surfaces are seen.
One camera is shared by all poses. Sole anchors are distinct from rig joints.
The adjoining leg is a 2D placeholder; no full-character depth is exported.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

if __package__:
    from .prepare_run_gap_trial import capsule, maximum, rigid_point
else:
    from prepare_run_gap_trial import capsule, maximum, rigid_point


SURFACES = {"sole": (151, 102, 212), "edge": (69, 65, 81), "side": (50, 151, 191),
            "toe": (240, 155, 56), "heel": (190, 91, 112), "upper": (183, 150, 106), "cuff": (94, 179, 109)}
MATERIALS = {"sole": (43, 30, 23), "edge": (53, 32, 23), "side": (120, 62, 34),
             "toe": (152, 85, 43), "heel": (96, 48, 30), "upper": (159, 93, 48), "cuff": (77, 45, 28)}


def box(x0, x1, y0, y1, z0, z1, bottom="upper", top="upper"):
    vertices = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [([0, 1, 2, 3], "side"), ([4, 7, 6, 5], "side"), ([0, 4, 5, 1], bottom),
             ([3, 2, 6, 7], top), ([1, 5, 6, 2], "toe"), ([0, 3, 7, 4], "heel")]
    return [(vertices[a], vertices[b], vertices[c], label)
            for indices, label in faces for a, b, c in ((indices[0], indices[1], indices[2]), (indices[0], indices[2], indices[3]))]


def camera_point(point, pitch, yaw, elevation):
    x, y, z = point
    x, y = x * math.cos(pitch) - y * math.sin(pitch), x * math.sin(pitch) + y * math.cos(pitch)
    cy, sy, ce, se = math.cos(yaw), math.sin(yaw), math.cos(elevation), math.sin(elevation)
    return (cy * x - sy * z, se * sy * x - ce * y + se * cy * z,
            ce * sy * x + se * y + ce * cy * z)


def render_boot(heel, toe, config, knee=None):
    yaw, elevation = map(math.radians, (config["camera_yaw_degrees"], config["camera_elevation_degrees"]))
    dx, dy = toe[0] - heel[0], toe[1] - heel[1]
    if math.hypot(dx, dy) < 1:
        raise ValueError("collapsed sole anchors")
    # Solve sagittal rotation whose projected sole direction matches the pins.
    pitch = math.atan2(math.sin(elevation) * math.sin(yaw) * dx - math.cos(yaw) * dy,
                       math.cos(elevation) * dx)
    projected_toe = camera_point((1.5, 0, 0), pitch, yaw, elevation)
    scale = math.hypot(dx, dy) / math.hypot(*projected_toe[:2])
    def project(point):
        x, y, depth = camera_point(point, pitch, yaw, elevation)
        return [heel[0] + scale * x, heel[1] + scale * y, depth * scale]
    size = tuple(config["canvas"])
    material, surface = Image.new("RGBA", size), Image.new("RGBA", size)
    depths = [-math.inf] * (size[0] * size[1])
    face_ids = [None] * len(depths)
    # A sole, toe box and shaft. Deliberately simple geometry exposes the view
    # change without pretending to reproduce the final leather drawing.
    proportions = config.get("proportions", {})
    sole_thickness = proportions.get("sole_thickness", 0.10)
    foot_depth = proportions.get("foot_depth", 0.58) / 2
    toe_height = proportions.get("toe_box_height", 0.36)
    shaft_width = proportions.get("shaft_width", 0.54) / 2
    shaft_depth = proportions.get("shaft_depth", 0.52) / 2
    if min(sole_thickness, foot_depth, shaft_width, shaft_depth) <= 0 or not sole_thickness < toe_height < 1.05 or toe_height < .32:
        raise ValueError("boot proportions must be positive and overlap from sole through shaft")
    triangles = box(-0.08, 1.5, 0, sole_thickness, -foot_depth - .03, foot_depth + .03, bottom="sole")
    triangles += box(0, 1.40, sole_thickness, toe_height, -foot_depth, foot_depth)
    projected_triangles = [(project(a), project(b), project(c), label) for a, b, c, label in triangles]
    ankle = project((.29, .32, 0))
    alignment = config.get("shaft_alignment", "foot")
    if alignment not in ("foot", "shin"):
        raise ValueError("shaft_alignment must be foot or shin")
    if alignment == "shin":
        if knee is None:
            raise ValueError("shin-aligned boot requires the target knee")
        # Invert the camera's XY-plane projection to obtain a 3D shaft axis
        # whose screen projection points exactly from ankle toward knee.
        screen_x, screen_y = knee[0] - ankle[0], knee[1] - ankle[1]
        if math.hypot(screen_x, screen_y) <= 1e-6:
            raise ValueError("knee and boot ankle coincide")
        world_x = screen_x / math.cos(yaw)
        world_y = (math.sin(elevation) * math.sin(yaw) * world_x - screen_y) / math.cos(elevation)
        length = math.hypot(world_x, world_y)
        up = (world_x / length, world_y / length)
        across = (up[1], -up[0])
        def shaft_project(point):
            x, y, z = point
            offset = camera_point((across[0] * x + up[0] * y, across[1] * x + up[1] * y, z), 0, yaw, elevation)
            return [ankle[i] + scale * offset[i] for i in range(3)]
        cuff = shaft_project((0, .73, 0))[:2]
        if math.dist(cuff, ankle[:2]) >= math.dist(knee, ankle[:2]):
            raise ValueError("boot shaft reaches past the knee; revise the guide proportions")
        shaft = box(-shaft_width, shaft_width, 0, .73, -shaft_depth, shaft_depth, top="cuff")
        projected_triangles.extend((shaft_project(a), shaft_project(b), shaft_project(c), label) for a, b, c, label in shaft)
    else:
        shaft = box(.29 - shaft_width, .29 + shaft_width, .32, 1.05, -shaft_depth, shaft_depth, top="cuff")
        projected_triangles.extend((project(a), project(b), project(c), label) for a, b, c, label in shaft)
        cuff = project((.29, 1.05, 0))[:2]
    for a, b, c, label in projected_triangles:
        area = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(area) < 1e-8:
            continue
        left, right = max(0, math.floor(min(a[0], b[0], c[0]))), min(size[0], math.ceil(max(a[0], b[0], c[0])))
        top, bottom = max(0, math.floor(min(a[1], b[1], c[1]))), min(size[1], math.ceil(max(a[1], b[1], c[1])))
        for y in range(top, bottom):
            for x in range(left, right):
                u = ((b[1] - c[1]) * (x + .5 - c[0]) + (c[0] - b[0]) * (y + .5 - c[1])) / area
                v = ((c[1] - a[1]) * (x + .5 - c[0]) + (a[0] - c[0]) * (y + .5 - c[1])) / area
                w = 1 - u - v
                if min(u, v, w) < -1e-7:
                    continue
                depth = u * a[2] + v * b[2] + w * c[2]
                index = y * size[0] + x
                if depth < depths[index]:
                    continue
                depths[index], face_ids[index] = depth, label
                material.putpixel((x, y), MATERIALS[label] + (255,))
                surface.putpixel((x, y), SURFACES[label] + (255,))
    projected = project((1.5, 0, 0))[:2]
    if math.dist(projected, toe) > 1e-6:
        raise ValueError("projection did not preserve sole anchors")
    annotations = {"heel": heel, "toe": toe, "ankle": ankle[:2], "cuff": cuff, "shaft_alignment": alignment,
                   "pitch_degrees": math.degrees(pitch), "visible_sole_pixels": face_ids.count("sole"),
                   "sole_pin_error": math.dist(projected, toe)}
    if knee is not None:
        calf = [cuff[i] - knee[i] for i in (0, 1)]
        shaft_axis = [ankle[i] - cuff[i] for i in (0, 1)]
        denominator = math.hypot(*calf) * math.hypot(*shaft_axis)
        if denominator <= 1e-8:
            raise ValueError("collapsed calf or shaft axis")
        cosine = sum(a * b for a, b in zip(calf, shaft_axis)) / denominator
        annotations["calf_shaft_angle_degrees"] = math.degrees(math.acos(max(-1, min(1, cosine))))
        annotations["knee"] = list(knee)
    return material, surface, annotations


def prepare(document, config, render, output):
    if output.exists():
        raise ValueError("guide output exists; create a new revision")
    output.mkdir(parents=True)
    size = tuple(config["canvas"])
    manifest = {"status": config.get("status", "draft inputs awaiting visual review; no generation"), "camera": config,
                "surfaces": SURFACES, "notes": "Boxy boot proxy plus 2D leg placeholders. Surface IDs are not depth.", "frames": []}
    if len(document["frames"]) != 12 or any(len(config["sole_directions_degrees"][side]) != 12 for side in ("near", "far")):
        raise ValueError("guide requires twelve explicit foot directions per side")
    for frame_index, frame in enumerate(document["frames"]):
        directory = output / frame["name"]
        directory.mkdir()
        source = Image.open(render / "working" / (frame["name"] + ".png")).convert("RGBA")
        parts = {part["name"]: Image.open(render / "part-poses" / part["name"] / (frame["name"] + ".png")).convert("RGBA")
                 for part in document["parts"]}
        proxy, identifiers, annotations = {}, {}, {}
        for side in ("far", "near"):
            spec = config[side]
            suffix = spec["suffix"]
            heel, toe = [rigid_point(spec[key], document["rest_pose"], frame["pose"], suffix) for key in ("sole_heel", "sole_toe")]
            length = math.dist(heel, toe)
            direction = math.radians(config["sole_directions_degrees"][side][frame_index])
            toe = [heel[0] + length * math.cos(direction), heel[1] + length * math.sin(direction)]
            support = config["support"][frame_index]
            limit = config["ground_y"] - (6 if support == "flight" else 0)
            shift = limit - max(heel[1], toe[1])
            if side != support:
                shift = min(0, shift)
            heel[1] += shift
            toe[1] += shift
            knee = frame["pose"]["knee_" + suffix]
            proxy[side + "_boot"], identifiers[side + "_boot"], annotations[side] = render_boot(heel, toe, config, knee)
            annotations[side]["sole_ground_shift_y"] = shift
            leg = Image.new("RGBA", size)
            draw = ImageDraw.Draw(leg)
            proportions = config.get("proportions", {})
            capsule(draw, frame["pose"]["hip_c"], knee, proportions.get("thigh_width", 11), (49, 43, 33, 255))
            capsule(draw, knee, annotations[side]["cuff"], proportions.get("calf_width", 9), (55, 48, 36, 255))
            proxy[side + "_leg"] = leg
            identifiers[side + "_leg"] = Image.new("RGBA", size, (208, 193, 93, 0))
            identifiers[side + "_leg"].putalpha(leg.getchannel("A"))
        composite, id_composite = Image.new("RGBA", size), Image.new("RGBA", size)
        for name in frame["draw_order"]:
            composite = Image.alpha_composite(composite, proxy.get(name, parts[name]))
            id_composite = Image.alpha_composite(id_composite, identifiers.get(name, parts[name]))
        allowance = maximum([image.getchannel("A") for image in proxy.values()] +
                            [parts[name].getchannel("A") for name in proxy])
        allowance = allowance.filter(ImageFilter.MaxFilter(5))
        protected = maximum([image.getchannel("A") for name, image in parts.items() if name not in proxy])
        allowance = ImageChops.subtract(allowance, protected.point(lambda v: 255 if v else 0))
        # Preserve exact original pixels outside the candidate editing region.
        white = Image.new("RGBA", size, "white")
        blank = Image.alpha_composite(white, source).convert("RGB")
        for name, image in (("geometry", composite), ("surfaces", id_composite)):
            image = Image.composite(Image.alpha_composite(white, image).convert("RGB"), blank, allowance)
            image.save(directory / (name + ".png"), optimize=True)
            image.resize(tuple(config["generation_canvas"]), Image.Resampling.NEAREST).save(directory / (name + "-1024.png"), optimize=True)
        blank.save(directory / "original.png", optimize=True)
        allowance.save(directory / "mask.png")
        allowance.resize(tuple(config["generation_canvas"]), Image.Resampling.NEAREST).save(directory / "mask-1024.png")
        manifest["frames"].append({"name": frame["name"], "annotations": annotations})
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared twelve boot-view guides at {output}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("document", "config", "render", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    if hashlib.sha256(args.document.read_bytes()).hexdigest() != config["document_sha256"]:
        raise ValueError("document changed after sole annotation")
    prepare(json.loads(args.document.read_text()), config, args.render, args.output)


if __name__ == "__main__":
    main()
