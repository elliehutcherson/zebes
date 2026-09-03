"""Render a bounded Dead Cells-style low-poly mouse proof in Blender.

This is Blender Python because Blender exposes its offline authoring API through
`bpy`; no Python enters the engine. The experiment renders exactly neutral and
contact at native 48x48 with one camera, model, palette, and scale.
"""

from __future__ import annotations

import argparse
import json
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

POSES = {
    "neutral": {
        "front_arm": ((-0.45, 3.55), (-0.68, 2.95), (-0.66, 2.56)),
        "rear_arm": ((0.30, 3.50), (0.66, 3.00), (0.72, 2.56)),
        "front_leg": ((-0.28, 2.18), (-0.34, 1.32), (-0.40, 0.52), (-0.82, 0.40)),
        "rear_leg": ((0.28, 2.16), (0.35, 1.32), (0.42, 0.52), (0.74, 0.40)),
    },
    "contact": {
        "front_arm": ((-0.54, 3.56), (0.16, 3.42), (0.94, 3.14)),
        "rear_arm": ((0.22, 3.48), (-0.52, 3.34), (-1.18, 3.04)),
        "front_leg": ((-0.34, 2.18), (-0.88, 1.38), (-1.38, 0.55), (-1.82, 0.43)),
        "rear_leg": ((0.24, 2.14), (0.76, 1.48), (1.05, 0.88), (1.36, 0.76)),
    },
}


def material(name: str, color: tuple[float, float, float, float]):
    result = bpy.data.materials.new(name)
    result.diffuse_color = color
    result.use_nodes = False
    return result


def apply_material(obj, value):
    obj.data.materials.append(value)
    obj.color = value.diffuse_color


def ellipsoid(name, location, scale, value, segments=8, rings=4):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    return obj


def cube(name, location, scale, value):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    return obj


def segment(name, start, end, radius, depth, value):
    start_point = Vector((start[0], depth, start[1]))
    end_point = Vector((end[0], depth, end[1]))
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
    return obj


def prism(name, points, depth, thickness, value):
    vertices = [(x, depth - thickness / 2.0, z) for x, z in points]
    vertices += [(x, depth + thickness / 2.0, z) for x, z in points]
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
    return obj


def make_camera():
    bpy.ops.object.camera_add(location=(0.0, -14.0, 3.05))
    camera = bpy.context.object
    direction = Vector((0.0, 0.0, 3.05)) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 6.3
    bpy.context.scene.camera = camera


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
    segment("tail_base", (0.55, 2.38), (1.05, 2.06), 0.10, 0.40, colors["fur"])
    segment("tail_tip", (1.05, 2.06), (1.36, 2.34), 0.075, 0.40, colors["fur"])

    # Torso and coat are one stable silhouette, deliberately ending above the knees.
    ellipsoid("torso", (0.0, 0.0, 3.05), (0.72, 0.48, 1.03), colors["coat"])
    prism(
        "coat_skirt",
        ((-0.76, 3.02), (0.70, 3.02), (0.92, 2.08), (-0.94, 2.08)),
        0.0,
        0.82,
        colors["coat"],
    )
    cube("belt", (0.0, -0.45, 2.67), (0.82, 0.08, 0.10), colors["leather"])
    cube("belt_buckle", (-0.05, -0.56, 2.67), (0.13, 0.05, 0.14), colors["muzzle"])

    add_limb("front_leg", pose["front_leg"], -0.38, colors["leather"], colors["fur"], colors["boot"])
    add_limb("front_arm", pose["front_arm"], -0.42, colors["coat"], colors["fur"])

    # Hood, face, and ears are separate geometry so their silhouette cannot drift.
    ellipsoid("hood", (-0.08, 0.0, 4.62), (0.92, 0.64, 1.05), colors["coat"])
    ellipsoid("rear_ear", (0.58, 0.18, 5.28), (0.58, 0.24, 0.62), colors["fur"])
    ellipsoid("rear_ear_inner", (0.58, -0.08, 5.28), (0.38, 0.06, 0.42), colors["ear"])
    ellipsoid("front_ear", (-0.62, -0.18, 5.30), (0.62, 0.22, 0.66), colors["fur"])
    ellipsoid("front_ear_inner", (-0.62, -0.43, 5.30), (0.40, 0.06, 0.44), colors["ear"])
    ellipsoid("face", (-0.28, -0.58, 4.62), (0.62, 0.10, 0.72), colors["fur"])
    ellipsoid("muzzle", (-0.82, -0.70, 4.42), (0.35, 0.08, 0.30), colors["muzzle"])
    ellipsoid("nose", (-1.12, -0.78, 4.43), (0.10, 0.06, 0.10), colors["nose"], segments=6, rings=3)
    ellipsoid("eye", (-0.55, -0.76, 4.82), (0.10, 0.05, 0.14), colors["eye"], segments=6, rings=3)
    ellipsoid("scarf", (-0.16, -0.52, 3.84), (0.66, 0.10, 0.22), colors["scarf"])
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
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 48
    scene.render.resolution_y = 48
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = True
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = False
    scene.display.shading.show_cavity = False
    scene.display.shading.show_specular_highlight = False
    scene.display.shading.show_object_outline = True
    scene.display.shading.object_outline_color = PALETTE["outline"][:3]
    scene.display.render_aa = "OFF"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "Medium High Contrast"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    make_camera()


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
    args = parser.parse_args(arguments)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for pose_name in ("neutral", "contact"):
        render_pose(pose_name, out)
    manifest = {
        "version": 1,
        "goal": (
            "Test whether one fixed low-poly model can render recognizable "
            "neutral and contact poses directly at native sprite resolution."
        ),
        "blender": bpy.app.version_string,
        "resolution": [48, 48],
        "camera": {"type": "orthographic", "scale": 6.3},
        "antialiasing": "OFF",
        "poses": POSES,
        "palette": PALETTE,
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
