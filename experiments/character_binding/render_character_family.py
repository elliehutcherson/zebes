"""Render reusable biped, quadruped, and flyer character-family specimens.

Blender Python is the offline authoring adapter. Body-plan topology, reusable
poses, camera, palette roles, and species parameters are data-driven JSON; no
Python or Blender dependency enters the engine.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

SCHEMA_VERSION = 1
BODY_PLANS = {"biped", "quadruped", "flyer"}
POSES = ("neutral", "action")
REQUIRED_COLORS = (
    "outline",
    "primary",
    "primary_dark",
    "secondary",
    "accent",
    "skin",
    "detail",
)
BODY_PLAN_PARAMETERS = {
    "biped": {
        "head_size",
        "torso_width",
        "torso_height",
        "arm_length",
        "leg_length",
        "limb_thickness",
        "ears",
        "ear_size",
        "muzzle",
        "tail",
        "tail_length",
        "coat",
        "scarf",
    },
    "quadruped": {
        "body_length",
        "body_height",
        "body_z",
        "head_height",
        "head_size",
        "leg_length",
        "leg_thickness",
        "ears",
        "ear_size",
        "tail",
        "tail_length",
        "face_stripe",
    },
    "flyer": {"ear_size", "wing_span"},
}


def color(hex_value: str) -> tuple[float, float, float, float]:
    if not isinstance(hex_value, str) or len(hex_value) != 7 or not hex_value.startswith("#"):
        raise ValueError(f"invalid RGB color {hex_value!r}")
    return tuple(int(hex_value[index : index + 2], 16) / 255.0 for index in (1, 3, 5)) + (1.0,)


def load_spec(path: Path) -> dict:
    document = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "schema_version",
        "name",
        "body_plan",
        "camera",
        "palette",
        "parameters",
    }
    if set(document) != required:
        raise ValueError(
            f"{path}: expected exactly {sorted(required)}, got {sorted(document)}"
        )
    if document["schema_version"] != SCHEMA_VERSION:
        raise ValueError(
            f"{path}: unsupported schema version {document['schema_version']!r}"
        )
    if document["body_plan"] not in BODY_PLANS:
        raise ValueError(f"{path}: unknown body plan {document['body_plan']!r}")
    name = document["name"]
    if (
        not isinstance(name, str)
        or not name
        or name != name.lower()
        or not name.replace("_", "").isalnum()
    ):
        raise ValueError(f"{path}: name must be a lowercase identifier")
    if set(document["camera"]) != {"scale", "center_z"}:
        raise ValueError(f"{path}: camera requires scale and center_z")
    if document["camera"]["scale"] <= 0:
        raise ValueError(f"{path}: camera scale must be positive")
    if set(document["palette"]) != set(REQUIRED_COLORS):
        raise ValueError(
            f"{path}: palette roles must be exactly {sorted(REQUIRED_COLORS)}"
        )
    for value in document["palette"].values():
        color(value)
    if not isinstance(document["parameters"], dict):
        raise ValueError(f"{path}: parameters must be an object")
    expected_parameters = BODY_PLAN_PARAMETERS[document["body_plan"]]
    if set(document["parameters"]) != expected_parameters:
        raise ValueError(
            f"{path}: {document['body_plan']} parameters must be exactly "
            f"{sorted(expected_parameters)}"
        )
    return document


def material(name: str, rgba):
    result = bpy.data.materials.new(name)
    result.diffuse_color = rgba
    result.use_nodes = True
    nodes = result.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = rgba
    result.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    return result


def apply_material(obj, value):
    obj.data.materials.append(value)
    obj.color = value.diffuse_color


def add_outline(obj, materials, factor=1.10):
    outline = obj.copy()
    outline.data = obj.data.copy()
    outline.data.materials.clear()
    outline.name = f"{obj.name}_outline"
    outline.scale = tuple(component * factor for component in obj.scale)
    outline.location = obj.location.copy()
    outline.location.y += 0.08
    bpy.context.collection.objects.link(outline)
    apply_material(outline, materials["outline"])


def ellipsoid(name, location, scale, value, materials, outlined=True, segments=8, rings=4):
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
        add_outline(obj, materials)
    return obj


def cube(name, location, scale, value, materials, outlined=True):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    apply_material(obj, value)
    if outlined:
        add_outline(obj, materials, 1.12)
    return obj


def segment(name, start, end, radius, depth, value, materials, outlined=True):
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
    if outlined:
        add_outline(obj, materials)
    return obj


def prism(name, points, depth, thickness, value, materials, outlined=True):
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
    if outlined:
        add_outline(obj, materials, 1.08)
    return obj


def palette_materials(spec: dict) -> dict:
    return {
        name: material(name, color(value))
        for name, value in spec["palette"].items()
    }


def add_ears(style, head, size, depth, materials):
    if style == "none":
        return
    x, z = head
    if style == "round":
        for suffix, offset_x, offset_y in (("rear", -0.45, 0.20), ("front", 0.42, -0.18)):
            ellipsoid(
                f"{suffix}_ear",
                (x + offset_x, depth + offset_y, z + 0.58),
                (size, 0.22, size),
                materials["secondary"],
                materials,
            )
            ellipsoid(
                f"{suffix}_ear_inner",
                (x + offset_x, depth + offset_y - 0.18, z + 0.58),
                (size * 0.62, 0.05, size * 0.62),
                materials["accent"],
                materials,
                outlined=False,
            )
        return
    if style == "long":
        for suffix, offset_x in (("rear", -0.30), ("front", 0.28)):
            ellipsoid(
                f"{suffix}_ear",
                (x + offset_x, depth, z + 1.00),
                (size * 0.42, 0.20, size * 1.15),
                materials["secondary"],
                materials,
            )
        return
    if style == "pointed":
        for suffix, offset_x in (("rear", -0.32), ("front", 0.30)):
            prism(
                f"{suffix}_ear",
                ((x + offset_x - size * 0.35, z + 0.30),
                 (x + offset_x, z + size * 1.35),
                 (x + offset_x + size * 0.35, z + 0.30)),
                depth,
                0.18,
                materials["secondary"],
                materials,
            )
        return
    raise ValueError(f"unsupported ear style {style!r}")


def add_tail(style, base, length, depth, materials):
    if style == "none":
        return
    x, z = base
    if style == "round":
        ellipsoid("tail", (x, depth, z), (length, 0.30, length), materials["secondary"], materials)
        return
    if style == "brush":
        segment("tail_base", (x, z), (x - length * 0.55, z + 0.18), length * 0.18, depth,
                materials["secondary"], materials)
        segment("tail_tip", (x - length * 0.55, z + 0.18), (x - length, z + 0.45),
                length * 0.25, depth, materials["secondary"], materials)
        return
    if style == "thin":
        segment("tail_base", (x, z), (x - length * 0.55, z - 0.16), 0.09, depth,
                materials["secondary"], materials)
        segment("tail_tip", (x - length * 0.55, z - 0.16), (x - length, z + 0.20), 0.07,
                depth, materials["secondary"], materials)
        return
    raise ValueError(f"unsupported tail style {style!r}")


def biped_joints(parameters, pose):
    leg = parameters["leg_length"]
    arm = parameters["arm_length"]
    hip_z = 1.65
    shoulder_z = 3.45
    if pose == "neutral":
        return {
            "front_arm": ((0.46, shoulder_z), (0.72, shoulder_z - arm * 0.55),
                          (0.78, shoulder_z - arm)),
            "rear_arm": ((-0.42, shoulder_z), (-0.68, shoulder_z - arm * 0.55),
                         (-0.74, shoulder_z - arm)),
            "front_leg": ((0.28, hip_z), (0.32, hip_z - leg * 0.52),
                          (0.36, hip_z - leg), (0.78, hip_z - leg - 0.10)),
            "rear_leg": ((-0.28, hip_z), (-0.32, hip_z - leg * 0.52),
                         (-0.36, hip_z - leg), (-0.70, hip_z - leg - 0.10)),
        }
    return {
        "front_arm": ((0.50, shoulder_z), (-0.10, shoulder_z - 0.18),
                      (-0.88, shoulder_z - 0.42)),
        "rear_arm": ((-0.38, shoulder_z), (0.28, shoulder_z - 0.14),
                     (0.96, shoulder_z - 0.40)),
        "front_leg": ((0.30, hip_z), (0.82, hip_z - leg * 0.45),
                      (1.28, hip_z - leg), (1.70, hip_z - leg - 0.10)),
        "rear_leg": ((-0.26, hip_z), (-0.76, hip_z - leg * 0.40),
                     (-1.05, hip_z - leg * 0.78), (-1.34, hip_z - leg * 0.86)),
    }


def add_biped_limb(name, joints, radius, depth, materials, leg=False):
    first, second, third = joints[:3]
    value = materials["primary_dark"] if depth > 0 else materials["primary"]
    segment(f"{name}_upper", first, second, radius, depth, value, materials)
    segment(f"{name}_lower", second, third, radius * 0.82, depth, value, materials)
    if leg:
        toe = joints[3]
        segment(f"{name}_foot", third, toe, radius * 0.92, depth - 0.02,
                materials["secondary"], materials)
    else:
        ellipsoid(f"{name}_hand", (third[0], depth - 0.02, third[1]),
                  (radius, 0.15, radius), materials["skin"], materials)


def build_biped(spec, pose, materials):
    parameters = spec["parameters"]
    joints = biped_joints(parameters, pose)
    limb_radius = parameters["limb_thickness"]
    add_biped_limb("rear_arm", joints["rear_arm"], limb_radius, 0.34, materials)
    add_biped_limb("rear_leg", joints["rear_leg"], limb_radius * 1.18, 0.30, materials, leg=True)
    add_tail(parameters["tail"], (-0.72, 2.10), parameters["tail_length"], 0.40, materials)

    torso_width = parameters["torso_width"]
    torso_height = parameters["torso_height"]
    ellipsoid("torso", (0.0, 0.0, 2.65), (torso_width, 0.50, torso_height),
              materials["primary"], materials)
    if parameters["coat"]:
        prism("coat", ((-torso_width, 3.15), (torso_width, 3.15),
                       (torso_width * 1.22, 1.48), (-torso_width * 1.22, 1.48)),
              0.0, 0.86, materials["primary"], materials)
        cube("belt", (0.0, -0.46, 2.42), (torso_width, 0.07, 0.09),
             materials["secondary"], materials)

    add_biped_limb("front_leg", joints["front_leg"], limb_radius * 1.18, -0.38, materials, leg=True)
    add_biped_limb("front_arm", joints["front_arm"], limb_radius, -0.42, materials)

    head = (0.12, 4.62)
    head_size = parameters["head_size"]
    ellipsoid("head", (head[0], 0.0, head[1]), (head_size, 0.62, head_size),
              materials["secondary"], materials)
    add_ears(parameters["ears"], head, parameters["ear_size"], -0.16, materials)
    if parameters["muzzle"]:
        ellipsoid("muzzle", (head[0] + head_size * 0.72, -0.70, head[1] - 0.18),
                  (head_size * 0.42, 0.08, head_size * 0.32), materials["skin"], materials)
        ellipsoid("nose", (head[0] + head_size * 1.08, -0.78, head[1] - 0.16),
                  (0.10, 0.05, 0.10), materials["accent"], materials, segments=6, rings=3)
    ellipsoid("eye", (head[0] + head_size * 0.34, -0.76, head[1] + 0.16),
              (0.11, 0.05, 0.15), materials["detail"], materials, segments=6, rings=3)
    if parameters["scarf"]:
        ellipsoid("scarf", (0.04, -0.52, 3.82), (0.72, 0.09, 0.22),
                  materials["accent"], materials)


def quadruped_joints(parameters, pose):
    body_z = parameters["body_z"]
    leg = parameters["leg_length"]
    front_x = parameters["body_length"] * 0.58
    rear_x = -parameters["body_length"] * 0.58
    if pose == "neutral":
        return {
            "front_near": ((front_x, body_z), (front_x + 0.04, body_z - leg * 0.55),
                           (front_x + 0.08, body_z - leg)),
            "front_far": ((front_x - 0.10, body_z), (front_x - 0.18, body_z - leg * 0.55),
                          (front_x - 0.22, body_z - leg)),
            "rear_near": ((rear_x, body_z), (rear_x + 0.06, body_z - leg * 0.55),
                          (rear_x + 0.10, body_z - leg)),
            "rear_far": ((rear_x + 0.12, body_z), (rear_x + 0.22, body_z - leg * 0.55),
                         (rear_x + 0.26, body_z - leg)),
        }
    return {
        "front_near": ((front_x, body_z), (front_x + 0.42, body_z - leg * 0.46),
                       (front_x + 0.72, body_z - leg * 0.90)),
        "front_far": ((front_x - 0.10, body_z), (front_x - 0.40, body_z - leg * 0.52),
                      (front_x - 0.62, body_z - leg * 0.88)),
        "rear_near": ((rear_x, body_z), (rear_x - 0.42, body_z - leg * 0.42),
                      (rear_x - 0.70, body_z - leg * 0.88)),
        "rear_far": ((rear_x + 0.12, body_z), (rear_x + 0.42, body_z - leg * 0.50),
                     (rear_x + 0.64, body_z - leg * 0.90)),
    }


def add_quadruped_leg(name, joints, radius, depth, value, materials):
    hip, knee, paw = joints
    segment(f"{name}_upper", hip, knee, radius, depth, value, materials)
    segment(f"{name}_lower", knee, paw, radius * 0.78, depth, value, materials)
    ellipsoid(f"{name}_paw", (paw[0] + 0.12, depth - 0.02, paw[1]),
              (radius * 1.15, 0.16, radius * 0.72), value, materials)


def build_quadruped(spec, pose, materials):
    parameters = spec["parameters"]
    joints = quadruped_joints(parameters, pose)
    radius = parameters["leg_thickness"]
    for name in ("front_far", "rear_far"):
        add_quadruped_leg(name, joints[name], radius, 0.34,
                          materials["primary_dark"], materials)

    length = parameters["body_length"]
    body_z = parameters["body_z"]
    ellipsoid("body", (0.0, 0.0, body_z + 0.30), (length, 0.58, parameters["body_height"]),
              materials["primary"], materials, segments=10, rings=5)
    head = (length * 0.92, body_z + parameters["head_height"])
    head_size = parameters["head_size"]
    ellipsoid("head", (head[0], -0.10, head[1]), (head_size, 0.52, head_size),
              materials["secondary"], materials)
    ellipsoid("muzzle", (head[0] + head_size * 0.76, -0.62, head[1] - 0.18),
              (head_size * 0.52, 0.10, head_size * 0.34), materials["skin"], materials)
    ellipsoid("eye", (head[0] + head_size * 0.30, -0.66, head[1] + 0.18),
              (0.10, 0.05, 0.13), materials["detail"], materials, segments=6, rings=3)
    add_ears(parameters["ears"], head, parameters["ear_size"], -0.12, materials)
    add_tail(parameters["tail"], (-length * 0.92, body_z + 0.52),
             parameters["tail_length"], 0.22, materials)

    if parameters.get("face_stripe", False):
        prism("face_stripe", ((head[0] + 0.06, head[1] + 0.55),
                              (head[0] + 0.24, head[1] + 0.52),
                              (head[0] + 0.48, head[1] - 0.38),
                              (head[0] + 0.30, head[1] - 0.42)),
              -0.66, 0.04, materials["skin"], materials, outlined=False)

    for name in ("front_near", "rear_near"):
        add_quadruped_leg(name, joints[name], radius, -0.36,
                          materials["secondary"], materials)


def build_flyer(spec, pose, materials):
    parameters = spec["parameters"]
    body_z = 3.10
    ellipsoid("body", (0.0, 0.0, body_z), (0.58, 0.46, 0.92),
              materials["primary"], materials)
    head = (0.20, 4.15)
    ellipsoid("head", (head[0], -0.08, head[1]), (0.68, 0.48, 0.66),
              materials["secondary"], materials)
    add_ears("pointed", head, parameters["ear_size"], -0.10, materials)
    ellipsoid("eye", (0.48, -0.60, 4.28), (0.10, 0.04, 0.13),
              materials["detail"], materials, segments=6, rings=3)
    ellipsoid("muzzle", (0.66, -0.58, 4.00), (0.26, 0.08, 0.20),
              materials["skin"], materials)

    span = parameters["wing_span"]
    if pose == "neutral":
        left_tip = (-span, 3.62)
        right_tip = (span, 3.62)
        left_low = (-span * 0.58, 2.35)
        right_low = (span * 0.58, 2.35)
    else:
        left_tip = (-span * 0.82, 1.42)
        right_tip = (span * 0.82, 1.42)
        left_low = (-span * 0.35, 2.25)
        right_low = (span * 0.35, 2.25)
    prism("rear_wing", ((-0.35, 3.55), left_tip, left_low, (-0.30, 2.72)),
          0.28, 0.12, materials["primary_dark"], materials)
    prism("front_wing", ((0.35, 3.55), right_tip, right_low, (0.30, 2.72)),
          -0.30, 0.12, materials["primary"], materials)
    segment("left_wing_bone", (-0.35, 3.55), left_tip, 0.09, -0.34,
            materials["secondary"], materials)
    segment("right_wing_bone", (0.35, 3.55), right_tip, 0.09, -0.36,
            materials["secondary"], materials)
    ellipsoid("left_foot", (-0.24, -0.12, 2.18), (0.14, 0.12, 0.24),
              materials["accent"], materials)
    ellipsoid("right_foot", (0.28, -0.14, 2.18), (0.14, 0.12, 0.24),
              materials["accent"], materials)


def configure_scene(spec):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in tuple(bpy.data.materials):
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

    center_z = spec["camera"]["center_z"]
    bpy.ops.object.camera_add(location=(0.0, -14.0, center_z))
    camera = bpy.context.object
    direction = Vector((0.0, 0.0, center_z)) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = spec["camera"]["scale"]
    scene.camera = camera


def build_character(spec, pose, materials):
    if spec["body_plan"] == "biped":
        build_biped(spec, pose, materials)
    elif spec["body_plan"] == "quadruped":
        build_quadruped(spec, pose, materials)
    else:
        build_flyer(spec, pose, materials)


def render(spec_path: Path, out: Path):
    spec = load_spec(spec_path)
    out.mkdir(parents=True, exist_ok=True)
    for pose in POSES:
        configure_scene(spec)
        materials = palette_materials(spec)
        build_character(spec, pose, materials)
        scene = bpy.context.scene
        scene.render.filepath = str(out / f"{pose}.png")
        bpy.ops.wm.save_as_mainfile(filepath=str(out / f"{pose}.blend"))
        bpy.ops.render.render(write_still=True)
        print(f"rendered {spec['name']} {pose}: {scene.render.filepath}")
    manifest = {
        "schema_version": 1,
        "goal": "Test reusable body plans across named character specimens.",
        "blender": bpy.app.version_string,
        "spec_path": str(spec_path),
        "spec": spec,
        "poses": list(POSES),
        "resolution": [48, 48],
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )


def main():
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--spec", action="append")
    sources.add_argument("--spec-dir")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(arguments)
    if args.spec_dir is not None:
        spec_paths = sorted(Path(args.spec_dir).glob("*.json"))
        if not spec_paths:
            raise ValueError(f"no character specs in {args.spec_dir}")
    else:
        spec_paths = [Path(path) for path in args.spec]
    out = Path(args.out)
    names = set()
    for spec_path in spec_paths:
        spec = load_spec(spec_path)
        if spec["name"] in names:
            raise ValueError(f"duplicate character name {spec['name']!r}")
        names.add(spec["name"])
        render(spec_path, out / spec["name"])


if __name__ == "__main__":
    main()
