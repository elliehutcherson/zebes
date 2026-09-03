"""Render the production mouse player frame sheets in Blender.

The committed source is imported/manual artwork: Blender supplies a deterministic
offline authoring surface, while the engine receives ordinary RGBA sheets.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

FRAME_SIZE = 48
CAMERA_SCALE = 7.0
CAMERA_CENTER_Z = 3.05
BLUEPRINT_ID = "1be81945-b011-4342-9109-a10c4040078c"
ASSET_IDS = {
    "idle-right": {
        "texture_id": "ac9cae75-e2c0-432e-b238-296aa264edd5",
        "sprite_id": "9cdadc72-2196-4820-b62a-29d133ebb30a",
        "recipe_id": "280929d1-3a5c-4e23-afe0-d6423b154bc1",
    },
    "idle-left": {
        "texture_id": "091eb6ea-d3a7-484c-986e-7b57afabd339",
        "sprite_id": "47bac0c0-9d23-42a8-8aab-c8744cf9c98b",
        "recipe_id": "4048d5a2-5f13-45ec-a779-230abd07cfa7",
    },
    "run-right": {
        "texture_id": "915316c1-83a6-4b9c-ab5a-b3a426a660e4",
        "sprite_id": "8c8eabf3-604b-46fe-876f-d8e28f6fa909",
        "recipe_id": "8ed8e459-62f4-4483-8ddf-aa3beadc10cf",
    },
    "run-left": {
        "texture_id": "64f85260-ba78-4b0a-aacb-19bd873c7543",
        "sprite_id": "76afab54-06b1-47b8-a879-64af36366aff",
        "recipe_id": "2a4912a6-cd16-49eb-8091-5def6da94f8d",
    },
    "airborne-right": {
        "texture_id": "eb3f7676-2772-4717-afa0-c0c292ecf814",
        "sprite_id": "de2f1e3b-276e-437c-9386-0129ad682f20",
        "recipe_id": "0ac7d4d1-2ab9-4663-a18a-b9c3010eb7bd",
    },
    "airborne-left": {
        "texture_id": "eeb4e4a1-0ae1-4154-bbbb-04026c6382eb",
        "sprite_id": "aa03d095-d3cf-4113-aa55-584006537d03",
        "recipe_id": "a167f00f-4a16-4842-b64a-17620aae6deb",
    },
}


PALETTE = {
    "outline": (0.035, 0.025, 0.025, 1.0),
    "fur_dark": (0.34, 0.15, 0.09, 1.0),
    "fur": (0.62, 0.34, 0.20, 1.0),
    "muzzle": (0.93, 0.79, 0.58, 1.0),
    "ear": (0.88, 0.48, 0.50, 1.0),
    "ear_light": (1.00, 0.67, 0.65, 1.0),
    "coat": (0.27, 0.40, 0.24, 1.0),
    "coat_light": (0.38, 0.50, 0.33, 1.0),
    "coat_dark": (0.15, 0.25, 0.14, 1.0),
    "scarf": (0.73, 0.035, 0.025, 1.0),
    "scarf_light": (0.95, 0.08, 0.04, 1.0),
    "leather": (0.33, 0.14, 0.06, 1.0),
    "boot": (0.20, 0.07, 0.035, 1.0),
    "boot_light": (0.48, 0.20, 0.07, 1.0),
    "eye": (0.015, 0.012, 0.01, 1.0),
    "nose": (0.76, 0.08, 0.12, 1.0),
}

NEUTRAL_LIMBS = {
    "front_arm": ((-0.62, 3.48), (-0.92, 2.85), (-0.98, 2.30)),
    "rear_arm": ((0.52, 3.44), (0.92, 2.86), (0.98, 2.30)),
    "front_leg": ((-0.34, 1.70), (-0.38, 1.10), (-0.42, 0.56), (-0.82, 0.42)),
    "rear_leg": ((0.34, 1.68), (0.38, 1.10), (0.42, 0.56), (0.76, 0.42)),
}

RUN_POSES = (
    {
        "front_arm": ((-0.62, 3.48), (0.18, 3.34), (1.02, 3.04)),
        "rear_arm": ((0.46, 3.42), (-0.48, 3.26), (-1.28, 2.94)),
        "front_leg": ((-0.34, 1.70), (-0.84, 1.12), (-1.32, 0.54), (-1.76, 0.42)),
        "rear_leg": ((0.30, 1.68), (0.76, 1.18), (1.04, 0.82), (1.36, 0.72)),
        "body_offset": 0.02,
    },
    {
        "front_arm": ((-0.62, 3.43), (0.04, 3.08), (0.70, 2.72)),
        "rear_arm": ((0.46, 3.38), (-0.34, 3.10), (-1.02, 2.72)),
        "front_leg": ((-0.34, 1.62), (-0.68, 1.00), (-1.00, 0.52), (-1.34, 0.42)),
        "rear_leg": ((0.30, 1.60), (0.66, 1.12), (0.72, 0.58), (0.94, 0.46)),
        "body_offset": -0.08,
    },
    {
        "front_arm": ((-0.62, 3.50), (-0.16, 2.94), (0.24, 2.46)),
        "rear_arm": ((0.46, 3.44), (0.00, 2.92), (-0.42, 2.48)),
        "front_leg": ((-0.34, 1.70), (-0.18, 1.14), (-0.06, 0.58), (-0.30, 0.46)),
        "rear_leg": ((0.30, 1.68), (0.72, 1.28), (1.02, 1.06), (0.80, 0.86)),
        "body_offset": 0.00,
    },
    {
        "front_arm": ((-0.62, 3.56), (-0.42, 2.96), (-0.22, 2.42)),
        "rear_arm": ((0.46, 3.50), (0.22, 2.94), (0.04, 2.42)),
        "front_leg": ((-0.34, 1.76), (0.04, 1.30), (0.36, 0.98), (0.58, 0.78)),
        "rear_leg": ((0.30, 1.74), (0.70, 1.24), (0.96, 0.88), (1.18, 0.74)),
        "body_offset": 0.08,
    },
)

AIRBORNE_POSES = (
    {
        "front_arm": ((-0.62, 3.48), (-0.94, 3.82), (-1.18, 4.12)),
        "rear_arm": ((0.46, 3.42), (0.80, 3.76), (1.06, 4.02)),
        "front_leg": ((-0.34, 1.70), (-0.74, 1.32), (-0.92, 0.96), (-1.18, 0.82)),
        "rear_leg": ((0.30, 1.68), (0.64, 1.34), (0.78, 1.02), (1.02, 0.88)),
        "body_offset": 0.00,
    },
    {
        "front_arm": ((-0.62, 3.48), (-1.02, 3.94), (-1.28, 4.26)),
        "rear_arm": ((0.46, 3.42), (0.88, 3.88), (1.16, 4.18)),
        "front_leg": ((-0.34, 1.70), (-0.64, 1.38), (-0.50, 1.06), (-0.82, 0.96)),
        "rear_leg": ((0.30, 1.68), (0.62, 1.36), (0.48, 1.06), (0.78, 0.96)),
        "body_offset": 0.26,
    },
    {
        "front_arm": ((-0.62, 3.48), (-0.92, 3.70), (-1.10, 3.84)),
        "rear_arm": ((0.46, 3.42), (0.78, 3.66), (0.98, 3.80)),
        "front_leg": ((-0.34, 1.70), (-0.72, 1.38), (-0.90, 1.12), (-1.20, 1.04)),
        "rear_leg": ((0.30, 1.68), (0.72, 1.40), (0.96, 1.18), (1.24, 1.10)),
        "body_offset": 0.20,
    },
    {
        "front_arm": ((-0.62, 3.48), (-0.80, 3.42), (-0.90, 3.20)),
        "rear_arm": ((0.46, 3.42), (0.66, 3.36), (0.78, 3.12)),
        "front_leg": ((-0.34, 1.70), (-0.84, 1.42), (-1.10, 1.04), (-1.40, 0.94)),
        "rear_leg": ((0.30, 1.68), (0.76, 1.42), (1.02, 1.10), (1.32, 1.02)),
        "body_offset": 0.08,
    },
)

CLIPS = {
    "idle-right": {
        "poses": tuple(("idle", index) for index in range(4)),
        "frames_per_cycle": (15, 15, 15, 15),
        "planted_frames": (True, True, True, True),
        "playback_mode": "loop",
    },
    "run-right": {
        "poses": tuple(("run", index) for index in range(8)),
        "frames_per_cycle": (4,) * 8,
        "planted_frames": (True, True, False, False, True, True, False, False),
        "playback_mode": "loop",
    },
    "airborne-right": {
        "poses": tuple(("airborne", index) for index in range(4)),
        "frames_per_cycle": (4, 4, 4, 4),
        "planted_frames": (False, False, False, False),
        "playback_mode": "hold-last",
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


def apply_material(obj, value) -> None:
    obj.data.materials.append(value)
    obj.color = value.diffuse_color


def add_outline(obj, colors, factor: float = 1.15) -> None:
    outline = obj.copy()
    outline.data = obj.data.copy()
    bpy.context.collection.objects.link(outline)
    outline.name = f"{obj.name}_outline"
    outline.scale = tuple(component * factor for component in obj.scale)
    outline.location.y += 0.08
    apply_material(outline, colors["outline"])


def ellipsoid(name, location, scale, value, colors, *, outlined=True, segments=8, rings=4):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    if outlined:
        add_outline(obj, colors)
    return obj


def cube(name, location, scale, value, colors, *, outlined=True):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    if outlined:
        add_outline(obj, colors)
    return obj


def segment(name, start, end, radius, depth, value, colors, *, outlined=True):
    start_point = Vector((start[0], depth, start[1]))
    end_point = Vector((end[0], depth, end[1]))
    direction = end_point - start_point
    midpoint = (start_point + end_point) / 2.0
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=8,
        radius=radius,
        depth=direction.length,
        location=midpoint,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
    apply_material(obj, value)
    if outlined:
        add_outline(obj, colors)
    return obj


def prism(name, points, depth, thickness, value, colors, *, outlined=True):
    vertices = [(x, depth - thickness / 2.0, z) for x, z in points]
    vertices += [(x, depth + thickness / 2.0, z) for x, z in points]
    count = len(points)
    faces = [tuple(range(count)), tuple(range(count, count * 2))]
    for index in range(count):
        next_index = (index + 1) % count
        faces.append((index, next_index, count + next_index, count + index))
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    apply_material(obj, value)
    if outlined:
        add_outline(obj, colors, 1.10)
    return obj


def shifted(points, amount):
    return tuple((x, z + amount) for x, z in points)


def mirror_scene() -> None:
    root = bpy.data.objects.new("left_facing_root", None)
    bpy.context.collection.objects.link(root)
    for obj in tuple(bpy.context.scene.objects):
        if obj.type == "MESH" and obj.parent is None:
            obj.parent = root
    root.scale.x = -1.0


def add_limb(name, joints, depth, sleeve, hand, boot, colors) -> None:
    if boot is None:
        shoulder, elbow, wrist = joints
        segment(f"{name}_upper", shoulder, elbow, 0.23, depth, sleeve, colors)
        segment(f"{name}_lower", elbow, wrist, 0.19, depth, sleeve, colors)
        cube(
            f"{name}_cuff",
            (wrist[0], depth - 0.03, wrist[1] + 0.13),
            (0.22, 0.16, 0.10),
            colors["coat_light"],
            colors,
        )
        ellipsoid(
            f"{name}_hand",
            (wrist[0], depth - 0.05, wrist[1]),
            (0.24, 0.19, 0.22),
            hand,
            colors,
        )
        return

    hip, knee, ankle, toe = joints
    segment(f"{name}_thigh", hip, knee, 0.32, depth, sleeve, colors)
    segment(f"{name}_shin", knee, ankle, 0.28, depth, sleeve, colors)
    segment(f"{name}_boot", ankle, toe, 0.27, depth - 0.03, boot, colors)
    ellipsoid(
        f"{name}_toe",
        (toe[0], depth - 0.05, toe[1]),
        (0.30, 0.21, 0.18),
        boot,
        colors,
    )
    cube(
        f"{name}_boot_highlight",
        (toe[0] - 0.04, depth - 0.25, toe[1] + 0.07),
        (0.19, 0.04, 0.04),
        colors["boot_light"],
        colors,
        outlined=False,
    )


def resolve_pose(kind: str, index: int):
    if kind == "idle":
        pose = {name: points for name, points in NEUTRAL_LIMBS.items()}
        pose["body_offset"] = (0.00, 0.035, 0.00, -0.025)[index]
        pose["breath"] = (0.00, 0.025, 0.00, -0.015)[index]
        return pose
    if kind == "airborne":
        return dict(AIRBORNE_POSES[index])
    base = RUN_POSES[index % 4]
    if index < 4:
        return dict(base)
    return {
        "front_arm": base["rear_arm"],
        "rear_arm": base["front_arm"],
        "front_leg": base["rear_leg"],
        "rear_leg": base["front_leg"],
        "body_offset": base["body_offset"],
    }


def build_mouse(kind: str, index: int, facing: str) -> None:
    colors = {name: material(name, rgba) for name, rgba in PALETTE.items()}
    pose = resolve_pose(kind, index)
    body_offset = pose["body_offset"]
    breath = pose.get("breath", 0.0)
    limb_offset = body_offset if kind == "airborne" else 0.0

    rear_arm = shifted(pose["rear_arm"], limb_offset)
    rear_leg = shifted(pose["rear_leg"], limb_offset)
    front_arm = shifted(pose["front_arm"], limb_offset)
    front_leg = shifted(pose["front_leg"], limb_offset)

    add_limb("rear_arm", rear_arm, 0.34, colors["coat_dark"], colors["fur"], None, colors)
    add_limb(
        "rear_leg",
        rear_leg,
        0.30,
        colors["leather"],
        colors["fur"],
        colors["boot"],
        colors,
    )

    tail_lift = 0.18 + (0.14 if kind == "run" else 0.0)
    segment(
        "tail_base",
        (0.72, 2.18 + body_offset),
        (1.28, 1.96 + body_offset + tail_lift),
        0.12,
        0.40,
        colors["fur_dark"],
        colors,
    )
    segment(
        "tail_tip",
        (1.28, 1.96 + body_offset + tail_lift),
        (1.68, 2.18 + body_offset + tail_lift),
        0.085,
        0.40,
        colors["fur"],
        colors,
    )

    body_z = 2.76 + body_offset
    ellipsoid(
        "torso",
        (0.0, 0.0, body_z),
        (0.94 + breath, 0.52, 1.10 + breath),
        colors["coat_dark"],
        colors,
    )
    prism(
        "coat_skirt",
        shifted(((-1.02, 3.22), (0.94, 3.22), (1.25, 1.48), (-1.30, 1.48)), body_offset),
        0.0,
        0.88,
        colors["coat"],
        colors,
    )
    prism(
        "coat_light_panel",
        shifted(((-0.92, 3.12), (-0.08, 3.12), (-0.18, 1.62), (-1.06, 1.58)), body_offset),
        -0.47,
        0.05,
        colors["coat_light"],
        colors,
        outlined=False,
    )
    prism(
        "left_lapel",
        shifted(((-0.68, 3.50), (-0.08, 3.10), (-0.34, 2.58), (-0.88, 3.12)), body_offset),
        -0.53,
        0.05,
        colors["coat_light"],
        colors,
    )
    prism(
        "right_lapel",
        shifted(((0.42, 3.50), (-0.08, 3.10), (0.24, 2.60), (0.74, 3.14)), body_offset),
        -0.52,
        0.05,
        colors["coat_dark"],
        colors,
    )
    cube(
        "belt",
        (0.0, -0.52, 2.43 + body_offset),
        (1.00, 0.07, 0.10),
        colors["leather"],
        colors,
    )
    cube(
        "belt_buckle",
        (-0.05, -0.62, 2.43 + body_offset),
        (0.13, 0.04, 0.14),
        colors["muzzle"],
        colors,
    )
    for pocket_x in (-0.66, 0.62):
        cube(
            f"pocket_{pocket_x}",
            (pocket_x, -0.57, 2.05 + body_offset),
            (0.25, 0.04, 0.08),
            colors["coat_dark"],
            colors,
            outlined=False,
        )

    add_limb(
        "front_leg",
        front_leg,
        -0.38,
        colors["leather"],
        colors["fur"],
        colors["boot"],
        colors,
    )
    add_limb("front_arm", front_arm, -0.42, colors["coat"], colors["fur"], None, colors)

    head_z = 4.62 + body_offset + breath * 0.35
    ellipsoid("hood", (-0.08, 0.0, head_z), (1.06, 0.68, 1.05), colors["coat_dark"], colors)
    ellipsoid(
        "hood_highlight",
        (-0.40, -0.56, head_z + 0.25),
        (0.58, 0.08, 0.68),
        colors["coat"],
        colors,
        outlined=False,
    )
    ellipsoid(
        "rear_ear",
        (0.96, 0.18, head_z + 0.66),
        (0.74, 0.24, 0.70),
        colors["fur_dark"],
        colors,
    )
    ellipsoid(
        "rear_ear_inner",
        (0.96, -0.08, head_z + 0.66),
        (0.50, 0.06, 0.48),
        colors["ear"],
        colors,
        outlined=False,
    )
    ellipsoid(
        "rear_ear_glint",
        (0.86, -0.15, head_z + 0.82),
        (0.22, 0.03, 0.20),
        colors["ear_light"],
        colors,
        outlined=False,
    )
    ellipsoid(
        "front_ear",
        (-0.96, -0.18, head_z + 0.68),
        (0.78, 0.22, 0.74),
        colors["fur_dark"],
        colors,
    )
    ellipsoid(
        "front_ear_inner",
        (-0.96, -0.43, head_z + 0.68),
        (0.52, 0.06, 0.50),
        colors["ear"],
        colors,
        outlined=False,
    )
    ellipsoid(
        "front_ear_glint",
        (-1.08, -0.50, head_z + 0.86),
        (0.23, 0.03, 0.21),
        colors["ear_light"],
        colors,
        outlined=False,
    )
    ellipsoid("face", (-0.30, -0.60, head_z), (0.78, 0.10, 0.75), colors["fur"], colors)
    prism(
        "cheek",
        shifted(((-0.82, 4.68), (-1.00, 4.32), (-0.66, 4.03), (-0.22, 4.16)), head_z - 4.62),
        -0.72,
        0.04,
        colors["muzzle"],
        colors,
        outlined=False,
    )
    ellipsoid(
        "muzzle",
        (-0.92, -0.74, head_z - 0.22),
        (0.42, 0.07, 0.33),
        colors["muzzle"],
        colors,
    )
    ellipsoid(
        "nose",
        (-1.29, -0.82, head_z - 0.20),
        (0.12, 0.05, 0.11),
        colors["nose"],
        colors,
        segments=6,
        rings=3,
    )
    ellipsoid(
        "eye_patch",
        (-0.60, -0.79, head_z + 0.20),
        (0.26, 0.035, 0.32),
        colors["fur_dark"],
        colors,
        outlined=False,
        segments=6,
        rings=3,
    )
    ellipsoid(
        "eye",
        (-0.64, -0.84, head_z + 0.20),
        (0.13, 0.025, 0.17),
        colors["eye"],
        colors,
        segments=6,
        rings=3,
    )
    cube(
        "eye_glint",
        (-0.68, -0.88, head_z + 0.27),
        (0.045, 0.015, 0.05),
        colors["muzzle"],
        colors,
        outlined=False,
    )
    segment(
        "brow",
        (-0.80, head_z + 0.42),
        (-0.43, head_z + 0.46),
        0.055,
        -0.83,
        colors["fur_dark"],
        colors,
        outlined=False,
    )
    segment(
        "whisker_upper",
        (-1.02, head_z - 0.13),
        (-1.56, head_z - 0.02),
        0.025,
        -0.84,
        colors["muzzle"],
        colors,
        outlined=False,
    )
    segment(
        "whisker_lower",
        (-1.02, head_z - 0.28),
        (-1.52, head_z - 0.38),
        0.025,
        -0.84,
        colors["muzzle"],
        colors,
        outlined=False,
    )

    ellipsoid(
        "scarf",
        (-0.16, -0.58, 3.84 + body_offset),
        (0.78, 0.09, 0.24),
        colors["scarf"],
        colors,
    )
    cube(
        "scarf_highlight",
        (-0.42, -0.69, 3.91 + body_offset),
        (0.30, 0.025, 0.055),
        colors["scarf_light"],
        colors,
        outlined=False,
    )
    scarf_swing = 0.28 if kind == "run" else 0.0
    prism(
        "scarf_tail",
        shifted(((-0.05, 3.82), (0.28, 3.75), (0.58 + scarf_swing, 3.18), (0.15, 3.08)), body_offset),
        -0.62,
        0.06,
        colors["scarf"],
        colors,
    )

    if facing == "right":
        mirror_scene()


def configure_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in tuple(bpy.data.materials):
        bpy.data.materials.remove(block)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = FRAME_SIZE
    scene.render.resolution_y = FRAME_SIZE
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

    bpy.ops.object.camera_add(location=(0.0, -14.0, CAMERA_CENTER_Z))
    camera = bpy.context.object
    direction = Vector((0.0, 0.0, CAMERA_CENTER_Z)) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = CAMERA_SCALE
    scene.camera = camera


def render_frame(kind: str, index: int, facing: str, path: Path) -> None:
    configure_scene()
    build_mouse(kind, index, facing)
    scene = bpy.context.scene
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def pack_images(paths: list[Path], destination: Path, columns: int) -> None:
    rows = math.ceil(len(paths) / columns)
    sheet = bpy.data.images.new(
        destination.stem,
        width=FRAME_SIZE * columns,
        height=FRAME_SIZE * rows,
        alpha=True,
        float_buffer=False,
    )
    sheet_pixels = [0.0] * (FRAME_SIZE * columns * FRAME_SIZE * rows * 4)
    for index, path in enumerate(paths):
        frame = bpy.data.images.load(str(path), check_existing=False)
        frame_pixels = list(frame.pixels)
        column = index % columns
        row = rows - 1 - index // columns
        for y in range(FRAME_SIZE):
            source = y * FRAME_SIZE * 4
            target = ((row * FRAME_SIZE + y) * FRAME_SIZE * columns + column * FRAME_SIZE) * 4
            sheet_pixels[target : target + FRAME_SIZE * 4] = frame_pixels[
                source : source + FRAME_SIZE * 4
            ]
        bpy.data.images.remove(frame)
    sheet.pixels = sheet_pixels
    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.filepath_raw = str(destination)
    sheet.file_format = "PNG"
    sheet.save()
    bpy.data.images.remove(sheet)


def render_clip(name: str, definition: dict, out: Path) -> dict:
    facing = "left" if name.endswith("left") else "right"
    frame_paths = []
    for frame_index, (kind, pose_index) in enumerate(definition["poses"]):
        frame_path = out / "frames" / name / f"{frame_index}.png"
        frame_path.parent.mkdir(parents=True, exist_ok=True)
        render_frame(kind, pose_index, facing, frame_path)
        frame_paths.append(frame_path)
    source_path = out / "source" / f"{name}.png"
    pack_images(frame_paths, source_path, len(frame_paths))
    return {
        "name": name,
        "source": str(source_path.relative_to(out)),
        "frame_count": len(frame_paths),
        "frames_per_cycle": list(definition["frames_per_cycle"]),
        "planted_frames": list(definition["planted_frames"]),
        "playback_mode": definition["playback_mode"],
    }


def main() -> None:
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    args = parser.parse_args(arguments)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    definitions = dict(CLIPS)
    for right_name, definition in tuple(CLIPS.items()):
        definitions[right_name.replace("-right", "-left")] = definition

    clips = []
    all_frames = []
    for name in (
        "idle-right",
        "idle-left",
        "run-right",
        "run-left",
        "airborne-right",
        "airborne-left",
    ):
        clips.append(render_clip(name, definitions[name], out))
        all_frames.extend(sorted((out / "frames" / name).glob("*.png")))

    pack_images(all_frames, out / "review" / "all-clips.png", 8)
    manifest = {
        "schema_version": 1,
        "name": "Mouse Player",
        "blender": bpy.app.version_string,
        "resolution": [FRAME_SIZE, FRAME_SIZE],
        "camera": {"type": "orthographic", "scale": CAMERA_SCALE},
        "palette": PALETTE,
        "clips": clips,
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )
    import_clips = []
    for clip in clips:
        state_key = clip["name"]
        import_clips.append(
            {
                "name": f"Mouse {state_key.replace('-', ' ').title()}",
                "state_key": state_key,
                "source": clip["source"],
                "playback_mode": clip["playback_mode"],
                "frames_per_cycle": clip["frames_per_cycle"],
                "planted_frames": clip["planted_frames"],
                **ASSET_IDS[state_key],
            }
        )
    import_manifest = {
        "schema_version": 1,
        "blueprint_id": BLUEPRINT_ID,
        "clips": import_clips,
    }
    (out / "import.json").write_text(
        json.dumps(import_manifest, indent=2, sort_keys=True), encoding="utf-8"
    )
    print(f"rendered {len(all_frames)} production mouse frames to {out}")


if __name__ == "__main__":
    main()
