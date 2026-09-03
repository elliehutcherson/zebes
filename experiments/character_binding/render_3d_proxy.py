"""Render a bounded Dead Cells-style low-poly mouse proof in Blender.

This is Blender Python because Blender exposes its offline authoring API through
`bpy`; no Python enters the engine. The experiment renders exactly neutral and
contact at native 48x48 with one camera, model, palette, and scale.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

PALETTE = {
    "outline": (0.035, 0.025, 0.025, 1.0),
    "fur": (0.58, 0.30, 0.16, 1.0),
    "muzzle": (0.92, 0.76, 0.56, 1.0),
    "ear": (0.86, 0.48, 0.48, 1.0),
    "coat": (0.22, 0.34, 0.20, 1.0),
    "coat_rear": (0.14, 0.23, 0.14, 1.0),
    "scarf": (0.68, 0.035, 0.025, 1.0),
    "leather": (0.28, 0.12, 0.055, 1.0),
    "boot": (0.20, 0.07, 0.035, 1.0),
    "eye": (0.015, 0.012, 0.01, 1.0),
    "nose": (0.70, 0.10, 0.12, 1.0),
}

MIRROR_REFERENCE_X = True
TARGET_SUBJECT_HEIGHT = 5.8


def model_x(value: float) -> float:
    return -value if MIRROR_REFERENCE_X else value

POSES = {
    "neutral": {
        "front_arm": ((-0.62, 3.48), (-0.92, 2.85), (-0.98, 2.30)),
        "rear_arm": ((0.52, 3.44), (0.92, 2.86), (0.98, 2.30)),
        "front_leg": ((-0.34, 1.70), (-0.38, 1.10), (-0.42, 0.56), (-0.82, 0.42)),
        "rear_leg": ((0.34, 1.68), (0.38, 1.10), (0.42, 0.56), (0.76, 0.42)),
    },
    "contact": {
        "front_arm": ((-0.62, 3.48), (0.18, 3.34), (1.02, 3.04)),
        "rear_arm": ((0.46, 3.42), (-0.48, 3.26), (-1.28, 2.94)),
        "front_leg": ((-0.34, 1.70), (-0.84, 1.12), (-1.32, 0.54), (-1.76, 0.42)),
        "rear_leg": ((0.30, 1.68), (0.76, 1.18), (1.04, 0.82), (1.36, 0.72)),
    },
}


def material(name: str, color: tuple[float, float, float, float]):
    result = bpy.data.materials.new(name)
    result.diffuse_color = color
    result.use_nodes = True
    nodes = result.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = color
    emission.inputs["Strength"].default_value = 1.0
    result.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    return result


def apply_material(obj, value):
    obj.data.materials.append(value)
    obj.color = value.diffuse_color


def add_outline(obj, scale_factor=1.10):
    outline_material = bpy.data.materials.get("outline")
    if outline_material is None:
        return
    outline = obj.copy()
    outline.data = obj.data.copy()
    outline.name = f"{obj.name}_outline"
    outline.scale = tuple(value * scale_factor for value in obj.scale)
    outline.location = obj.location.copy()
    outline.location.y += 0.08
    outline.data.materials.clear()
    bpy.context.collection.objects.link(outline)
    apply_material(outline, outline_material)


def ellipsoid(name, location, scale, value, segments=8, rings=4, outlined=True):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        location=(model_x(location[0]), location[1], location[2]),
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    if outlined:
        add_outline(obj)
    return obj


def cube(name, location, scale, value, outlined=True):
    bpy.ops.mesh.primitive_cube_add(
        size=1.0, location=(model_x(location[0]), location[1], location[2])
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    if outlined:
        add_outline(obj, 1.12)
    return obj


def segment(name, start, end, radius, depth, value, outlined=True):
    start_point = Vector((model_x(start[0]), depth, start[1]))
    end_point = Vector((model_x(end[0]), depth, end[1]))
    direction = end_point - start_point
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=8,
        radius=radius,
        depth=direction.length,
        location=(start_point + end_point) / 2.0,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = direction.to_track_quat("Z", "Y")
    apply_material(obj, value)
    if outlined:
        add_outline(obj, 1.10)
    return obj


def prism(name, points, depth, thickness, value, outlined=True):
    vertices = [(model_x(x), depth - thickness / 2.0, z) for x, z in points]
    vertices += [(model_x(x), depth + thickness / 2.0, z) for x, z in points]
    count = len(points)
    faces = [tuple(range(count)), tuple(range(count, count * 2))]
    for index in range(count):
        following = (index + 1) % count
        faces.append((index, following, following + count, index + count))
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    apply_material(obj, value)
    if outlined:
        add_outline(obj, 1.08)
    return obj


def make_camera():
    bpy.ops.object.camera_add(location=(0.0, -14.0, 3.05))
    camera = bpy.context.object
    direction = Vector((0.0, 0.0, 3.05)) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 6.3
    bpy.context.scene.camera = camera


def load_reference(path: Path):
    image = bpy.data.images.load(str(path), check_existing=False)
    width, height = image.size
    pixels = list(image.pixels)
    covered = [
        index
        for index in range(width * height)
        if pixels[index * 4 + 3] >= 0.5
    ]
    if not covered:
        raise RuntimeError(f"reference has no opaque subject: {path}")
    left = min(index % width for index in covered)
    right = max(index % width for index in covered)
    bottom = min(index // width for index in covered)
    top = max(index // width for index in covered)
    stats = {
        "path": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "image_size": [width, height],
        "subject_bounds": {
            "left": left,
            "right": right,
            "bottom": bottom,
            "top": top,
        },
        "subject_width_ratio": (right - left + 1) / width,
        "subject_height_ratio": (top - bottom + 1) / height,
    }
    return image, stats


def make_reference_material(image):
    value = bpy.data.materials.new("reference")
    value.use_nodes = True
    value.blend_method = "BLEND"
    nodes = value.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    transparent = nodes.new("ShaderNodeBsdfTransparent")
    emission = nodes.new("ShaderNodeEmission")
    texture = nodes.new("ShaderNodeTexImage")
    mix = nodes.new("ShaderNodeMixShader")
    texture.image = image
    texture.interpolation = "Closest"
    texture.extension = "CLIP"
    value.node_tree.links.new(texture.outputs["Color"], emission.inputs["Color"])
    value.node_tree.links.new(texture.outputs["Alpha"], mix.inputs[0])
    value.node_tree.links.new(transparent.outputs[0], mix.inputs[1])
    value.node_tree.links.new(emission.outputs[0], mix.inputs[2])
    value.node_tree.links.new(mix.outputs[0], output.inputs["Surface"])
    return value


def add_reference_plane(image, stats):
    width, height = stats["image_size"]
    bounds = stats["subject_bounds"]
    subject_fraction = stats["subject_height_ratio"]
    plane_size = TARGET_SUBJECT_HEIGHT / subject_fraction
    source_center_x = ((bounds["left"] + bounds["right"] + 1) / 2.0) / width - 0.5
    source_center_z = ((bounds["bottom"] + bounds["top"] + 1) / 2.0) / height - 0.5
    visual_center_x = -source_center_x if MIRROR_REFERENCE_X else source_center_x
    location_x = -visual_center_x * plane_size
    location_z = 3.05 - source_center_z * plane_size
    bpy.ops.mesh.primitive_plane_add(
        size=plane_size,
        location=(location_x, 0.0, location_z),
        rotation=(math.pi / 2.0, 0.0, 0.0),
    )
    plane = bpy.context.object
    plane.name = "reference_model_sheet"
    if MIRROR_REFERENCE_X:
        plane.scale.x = -1.0
    apply_material(plane, make_reference_material(image))
    stats["plane_size"] = plane_size
    stats["mirrored_x"] = MIRROR_REFERENCE_X


def add_limb(name, joints, depth, sleeve_material, hand_material, boot_material=None):
    if boot_material is None:
        shoulder, elbow, hand = joints
        segment(f"{name}_upper", shoulder, elbow, 0.22, depth, sleeve_material)
        segment(f"{name}_lower", elbow, hand, 0.18, depth, sleeve_material)
        ellipsoid(f"{name}_hand", (hand[0], depth - 0.02, hand[1]), (0.21, 0.18, 0.20), hand_material)
        return

    hip, knee, ankle, toe = joints
    segment(f"{name}_thigh", hip, knee, 0.31, depth, sleeve_material)
    segment(f"{name}_shin", knee, ankle, 0.27, depth, sleeve_material)
    segment(f"{name}_boot", ankle, toe, 0.25, depth - 0.03, boot_material)
    ellipsoid(f"{name}_toe", (toe[0], depth - 0.04, toe[1]), (0.24, 0.20, 0.17), boot_material)


def build_mouse(pose_name: str):
    colors = {name: material(name, color) for name, color in PALETTE.items()}
    pose = POSES[pose_name]

    # Rear appendages first in camera depth. The camera looks from -Y toward +Y.
    add_limb("rear_arm", pose["rear_arm"], 0.34, colors["coat_rear"], colors["fur"])
    add_limb(
        "rear_leg",
        pose["rear_leg"],
        0.30,
        colors["leather"],
        colors["fur"],
        colors["boot"],
    )
    segment("tail_base", (0.72, 2.18), (1.22, 1.96), 0.11, 0.40, colors["fur"])
    segment("tail_tip", (1.22, 1.96), (1.54, 2.18), 0.08, 0.40, colors["fur"])

    # The model-sheet reference is broad through the coat and hides short legs.
    ellipsoid("torso", (0.0, 0.0, 2.76), (0.94, 0.52, 1.10), colors["coat"])
    prism(
        "coat_skirt",
        ((-1.02, 3.16), (0.94, 3.16), (1.25, 1.48), (-1.30, 1.48)),
        0.0,
        0.88,
        colors["coat"],
    )
    cube("belt", (0.0, -0.45, 2.44), (1.00, 0.08, 0.10), colors["leather"])
    cube("belt_buckle", (-0.05, -0.56, 2.44), (0.13, 0.05, 0.14), colors["muzzle"])

    add_limb("front_leg", pose["front_leg"], -0.38, colors["leather"], colors["fur"], colors["boot"])
    add_limb("front_arm", pose["front_arm"], -0.42, colors["coat"], colors["fur"])

    # Hood, face, and ears are separate geometry so their silhouette cannot drift.
    ellipsoid("hood", (-0.08, 0.0, 4.62), (1.06, 0.68, 1.05), colors["coat"])
    ellipsoid("rear_ear", (0.96, 0.18, 5.28), (0.74, 0.24, 0.70), colors["fur"])
    ellipsoid("rear_ear_inner", (0.96, -0.08, 5.28), (0.50, 0.06, 0.48), colors["ear"])
    ellipsoid("front_ear", (-0.96, -0.18, 5.30), (0.78, 0.22, 0.74), colors["fur"])
    ellipsoid("front_ear_inner", (-0.96, -0.43, 5.30), (0.52, 0.06, 0.50), colors["ear"])
    ellipsoid("face", (-0.30, -0.58, 4.62), (0.76, 0.10, 0.74), colors["fur"])
    ellipsoid("muzzle", (-0.91, -0.70, 4.40), (0.40, 0.08, 0.32), colors["muzzle"])
    ellipsoid("nose", (-1.27, -0.78, 4.42), (0.11, 0.06, 0.11), colors["nose"], segments=6, rings=3)
    ellipsoid("eye", (-0.60, -0.76, 4.82), (0.13, 0.05, 0.17), colors["eye"], segments=6, rings=3)
    ellipsoid("scarf", (-0.16, -0.52, 3.84), (0.76, 0.10, 0.24), colors["scarf"])
    prism(
        "scarf_tail",
        ((-0.05, 3.80), (0.25, 3.73), (0.18, 3.08), (-0.12, 3.18)),
        -0.54,
        0.08,
        colors["scarf"],
    )


def configure_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in bpy.data.materials:
        bpy.data.materials.remove(block)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 48
    scene.render.resolution_y = 48
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = True
    scene.eevee.taa_render_samples = 1
    scene.render.filter_size = 0.01
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    make_camera()


def render_reference(reference: Path, out: Path):
    configure_scene()
    image, stats = load_reference(reference)
    add_reference_plane(image, stats)
    scene = bpy.context.scene
    scene.render.filepath = str(out / "reference.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(out / "reference.blend"))
    bpy.ops.render.render(write_still=True)
    print(f"rendered {scene.render.filepath}")
    return stats


def render_pose(pose_name: str, out: Path):
    configure_scene()
    build_mouse(pose_name)
    scene = bpy.context.scene
    scene.render.filepath = str(out / f"{pose_name}.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(out / f"{pose_name}.blend"))
    bpy.ops.render.render(write_still=True)
    print(f"rendered {scene.render.filepath}")


def main() -> None:
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--reference", required=True)
    args = parser.parse_args(arguments)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    reference_stats = render_reference(Path(args.reference), out)
    for pose_name in ("neutral", "contact"):
        render_pose(pose_name, out)
    manifest = {
        "version": 1,
        "goal": (
            "Normalize a generated profile as a right-facing native model sheet, "
            "fit one reusable low-poly model to its measured silhouette, and "
            "render neutral/contact without changing identity."
        ),
        "blender": bpy.app.version_string,
        "render": {
            "engine": "BLENDER_EEVEE",
            "taa_render_samples": 1,
            "filter_size": 0.01,
        },
        "camera": {"type": "orthographic", "scale": 6.3},
        "poses": POSES,
        "palette": PALETTE,
        "reference": reference_stats,
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
