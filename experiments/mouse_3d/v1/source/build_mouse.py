"""Create, rig and render one real 3D mouse; run with Blender 4.0's Python.

No network or provider calls. The .blend retains its armature and editable mesh
parts. Renders use the same model, materials, lights, camera and origin.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from motion import FPS, FRAMES, pose_at, rest_bones

REST = rest_bones()
PARTS = []


def material(name, color, roughness=0.8, texture=0.0, metallic=0.0):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1)
    result.use_nodes = True
    nodes, links = result.node_tree.nodes, result.node_tree.links
    shader = nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    if texture:
        noise = nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 75
        noise.inputs["Detail"].default_value = 2
        ramp = nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].position = 0.16
        ramp.color_ramp.elements[0].color = (*(c * 0.78 for c in color), 1)
        ramp.color_ramp.elements[1].position = 0.86
        ramp.color_ramp.elements[1].color = (*(min(1, c * 1.12) for c in color), 1)
        links.new(noise.outputs["Fac"], ramp.inputs[0])
        links.new(ramp.outputs["Color"], shader.inputs["Base Color"])
        bump = nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = texture
        bump.inputs["Distance"].default_value = 0.009
        links.new(noise.outputs["Fac"], bump.inputs["Height"])
        links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    return result


def finish(obj, name, mat, weights, subdiv=0):
    obj.name = name
    obj.data.materials.append(mat)
    if obj.type == "MESH":
        for poly in obj.data.polygons:
            poly.use_smooth = True
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.select_set(False)
    if isinstance(weights, str):
        group = obj.vertex_groups.new(name=weights)
        group.add(list(range(len(obj.data.vertices))), 1, "REPLACE")
    else:
        groups = {}
        for vertex in obj.data.vertices:
            assigned = weights(vertex.co)
            if abs(sum(assigned.values()) - 1) > 1e-6:
                raise ValueError(f"unnormalized skin weights: {name}")
            for bone, amount in assigned.items():
                if amount <= 0:
                    continue
                if bone not in groups:
                    groups[bone] = obj.vertex_groups.new(name=bone)
                groups[bone].add([vertex.index], amount, "REPLACE")
    modifier = obj.modifiers.new("Mouse skeleton", "ARMATURE")
    modifier.object = bpy.data.objects["Mouse_Rig"]
    modifier.use_deform_preserve_volume = True
    if subdiv:
        modifier = obj.modifiers.new("Surface smoothing", "SUBSURF")
        modifier.levels = subdiv
    PARTS.append(obj)
    return obj


def mesh(name, vertices, faces, mat, weights, subdiv=0):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    return finish(obj, name, mat, weights, subdiv)


def sphere(name, center, scale, mat, bone, rotation=(0, 0, 0)):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, location=center)
    obj = bpy.context.object
    obj.scale = scale
    obj.rotation_euler = rotation
    return finish(obj, name, mat, bone)


def box(name, center, scale, mat, bone, bevel=0.035):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    modifier = obj.modifiers.new("Rounded edges", "BEVEL")
    modifier.width = bevel
    modifier.segments = 3
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    return finish(obj, name, mat, bone)


def tube(name, points, radius, mat, weights, cyclic=False):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 12
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    spline = curve.splines.new("BEZIER")
    spline.bezier_points.add(len(points) - 1)
    for point, co in zip(spline.bezier_points, points):
        point.co = co
        point.handle_left_type = "AUTO"
        point.handle_right_type = "AUTO"
    spline.use_cyclic_u = cyclic
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target="MESH")
    return finish(bpy.context.object, name, mat, weights)


def loft(name, rings, mat, weights, segments=32, subdiv=1, caps=True):
    """Rings have center, x radius and y radius; their normals point up."""
    vertices = []
    for center, rx, ry in rings:
        for j in range(segments):
            a = j * math.tau / segments
            vertices.append((center[0] + rx * math.cos(a),
                             center[1] + ry * math.sin(a), center[2]))
    faces = []
    for i in range(len(rings) - 1):
        for j in range(segments):
            faces.append((i * segments + j, i * segments + (j + 1) % segments,
                          (i + 1) * segments + (j + 1) % segments, (i + 1) * segments + j))
    if caps:
        faces.extend([tuple(reversed(range(segments))),
                      tuple((len(rings) - 1) * segments + j for j in range(segments))])
    return mesh(name, vertices, faces, mat, weights, subdiv)


def bone_matrix(start, end):
    direction = (Vector(end) - Vector(start)).normalized()
    across = Vector((0, 1, 0))
    across = (across - direction * across.dot(direction)).normalized()
    normal = across.cross(direction)
    result = Matrix((across, direction, normal)).transposed().to_4x4()
    result.translation = start
    return result


def parent_for(name):
    if name == "root":
        return None
    if name in ("torso", "tail.0") or name.startswith("thigh."):
        return "root"
    if name == "head" or name.startswith(("upper_arm.", "coat.")):
        return "torso"
    part, side = name.split(".")
    if part == "tail":
        return f"tail.{int(side) - 1}"
    return {"shin": "thigh", "foot": "shin", "forearm": "upper_arm", "hand": "forearm"}[part] + "." + side


def create_rig():
    data = bpy.data.armatures.new("Mouse skeleton")
    rig = bpy.data.objects.new("Mouse_Rig", data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    rig.show_in_front = True
    bpy.ops.object.mode_set(mode="EDIT")
    for name, (start, end) in REST.items():
        bone = data.edit_bones.new(name)
        # A newly created edit bone has zero length. Its matrix setter cannot
        # establish direction until endpoints exist; setting length afterward
        # silently yields a vertical bind bone with the wrong skin transform.
        bone.head, bone.tail = start, end
        bone.align_roll(bone_matrix(start, end).col[2].to_3d())
        parent = parent_for(name)
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    for name, (start, end) in REST.items():
        expected = bone_matrix(start, end)
        actual = data.bones[name].matrix_local
        if max(abs(actual[i][j] - expected[i][j]) for i in range(4) for j in range(4)) > 1e-5:
            raise ValueError(f"Bind matrix does not match authored mesh coordinates: {name}")
    rig.select_set(False)
    for bone in rig.pose.bones:
        bone.rotation_mode = "QUATERNION"
    rig["description"] = "One skinned mouse. Run frames 1-24 at 24 fps; frame 25 closes the loop."
    return rig


def limb_weights(side, upper, lower, joint_z, width=0.18):
    def weights(co):
        amount = min(1, max(0, (co.z - joint_z + width / 2) / width))
        return {f"{upper}.{side}": amount, f"{lower}.{side}": 1 - amount}
    return weights


def build_limbs(colors):
    for side in ("far", "near"):
        hip, knee = [Vector(p) for p in REST[f"thigh.{side}"]]
        ankle = Vector(REST[f"shin.{side}"][1])
        rings = []
        for start, end, widths in ((hip, knee, (0.235, 0.265, 0.24)),
                                   (knee, ankle, (0.24, 0.19, 0.155))):
            for t, width in zip((0, 0.5, 1), widths):
                rings.append((tuple(start.lerp(end, t)), width, width * 0.92))
        # Shared knee ring keeps the trousers a continuous weighted surface.
        rings.pop(3)
        rings.reverse()
        weights = limb_weights(side, "thigh", "shin", knee.z, 0.28)
        loft(f"Trousers_{side}", rings, colors["trousers"], weights, subdiv=2)
        for t in (0.22, 0.36):
            c = ankle.lerp(knee, t)
            tube(f"Trouser_fold_{side}_{t}", [(c.x - 0.13, c.y - 0.16, c.z + 0.035),
                 (c.x, c.y - 0.185, c.z), (c.x + 0.12, c.y - 0.15, c.z - 0.025)],
                 0.012, colors["trouser_light"], f"shin.{side}")
        shaft_rings = [(tuple(ankle + Vector((0, 0, dz))), rx, ry)
                      for dz, rx, ry in ((-0.1, 0.18, 0.19), (0, 0.20, 0.21),
                                        (0.22, 0.20, 0.20), (0.34, 0.225, 0.22),
                                        (0.37, 0.225, 0.22))]
        loft(f"Boot_shaft_{side}", shaft_rings, colors["leather"], f"shin.{side}")
        tube(f"Boot_cuff_{side}", [(ankle.x + 0.223 * math.cos(a), ankle.y + 0.22 * math.sin(a), ankle.z + 0.35)
             for a in [j * math.tau / 24 for j in range(24)]], 0.025, colors["leather_light"], f"shin.{side}", True)
        vertices, faces = [], []
        sections = [(-0.22, 0.025, -0.10, 0.02), (-0.17, 0.19, -0.075, 0.14),
                    (-0.05, 0.22, -0.045, 0.195), (0.15, 0.25, -0.09, 0.155),
                    (0.36, 0.25, -0.12, 0.125), (0.55, 0.17, -0.13, 0.085),
                    (0.62, 0.02, -0.13, 0.015)]
        for x, ry, z, rz in sections:
            vertices.extend([(ankle.x + x, ankle.y + ry * math.cos(j * math.tau / 24),
                              ankle.z + z + rz * math.sin(j * math.tau / 24)) for j in range(24)])
        for i in range(len(sections) - 1):
            faces.extend([(i * 24 + j, i * 24 + (j + 1) % 24, (i + 1) * 24 + (j + 1) % 24,
                           (i + 1) * 24 + j) for j in range(24)])
        mesh(f"Boot_foot_{side}", vertices, faces, colors["leather"], f"foot.{side}", 2)
        sole = [((ankle.x + 0.20, ankle.y, ankle.z + dz), 0.43, 0.25)
                for dz in (-0.25, -0.23, -0.17)]
        loft(f"Boot_sole_{side}", sole, colors["sole"], f"foot.{side}", subdiv=0)
        tube(f"Welt_{side}", [(ankle.x + 0.20 + 0.417 * math.cos(a), ankle.y + 0.244 * math.sin(a), ankle.z - 0.175)
             for a in [j * math.tau / 32 for j in range(32)]], 0.012, colors["leather_light"], f"foot.{side}", True)
        for dz in (0.08, 0.21):
            tube(f"Boot_seam_{side}_{dz}", [(ankle.x - 0.16, ankle.y - 0.135, ankle.z + dz),
                 (ankle.x - 0.19, ankle.y, ankle.z + dz + 0.025),
                 (ankle.x - 0.16, ankle.y + 0.135, ankle.z + dz)], 0.009, colors["leather_light"], f"shin.{side}")
        shoulder, elbow = [Vector(p) for p in REST[f"upper_arm.{side}"]]
        wrist = Vector(REST[f"forearm.{side}"][1])
        sleeve = [(tuple(shoulder), 0.245, 0.23),
                  (tuple(shoulder.lerp(elbow, 0.3)), 0.245, 0.215),
                  (tuple(elbow), 0.18, 0.18),
                  (tuple(elbow.lerp(wrist, 0.55)), 0.18, 0.16),
                  (tuple(wrist + Vector((0, 0, 0.08))), 0.155, 0.15)]
        sleeve.reverse()
        loft(f"Sleeve_{side}", sleeve, colors["green"],
             limb_weights(side, "upper_arm", "forearm", elbow.z, 0.22), subdiv=2)
        cuff = [(tuple(wrist + Vector((0, 0, z))), 0.17, 0.16) for z in (0.035, 0.07, 0.14)]
        loft(f"Sleeve_cuff_{side}", cuff, colors["green_dark"], f"forearm.{side}")
        tube(f"Cuff_piping_{side}", [(wrist.x + 0.165 * math.cos(a), wrist.y + 0.158 * math.sin(a), wrist.z + 0.055)
             for a in [j * math.tau / 20 for j in range(20)]], 0.012, colors["trim"], f"forearm.{side}", True)
        sphere(f"Paw_{side}", tuple(wrist + Vector((0.055, 0, -0.08))), (0.16, 0.145, 0.19), colors["fur"], f"hand.{side}")
        sphere(f"Thumb_{side}", tuple(wrist + Vector((0.155, -0.06, -0.03))), (0.083, 0.087, 0.115), colors["muzzle"], f"hand.{side}")
        for i in range(3):
            sphere(f"Knuckle_{side}_{i}", tuple(wrist + Vector((0.075, -0.122, -0.01 - i * 0.065))),
                   (0.079, 0.047, 0.04), colors["muzzle"], f"hand.{side}")


def coat_weights(co):
    lower = min(1, max(0, (1.98 - co.z) / 0.48))
    side = "near" if co.y < 0 else "far"
    return {"torso": 1 - lower, f"coat.{side}": lower}


def build_clothes(colors):
    sphere("Underlying_body", (0.05, 0, 2.17), (0.40, 0.325, 0.53), colors["fur"], "torso")
    sphere("Pelvis", (0, 0, 1.63), (0.34, 0.36, 0.32), colors["trousers"], "root")
    levels = [(1.29, -0.18, 0.63, 0.49), (1.36, -0.16, 0.63, 0.49),
              (1.65, -0.09, 0.55, 0.46), (1.98, 0, 0.435, 0.375),
              (2.26, 0.065, 0.46, 0.40), (2.50, 0.14, 0.42, 0.42),
              (2.69, 0.18, 0.26, 0.28)]
    vertices, faces = [], []
    segments = 48
    for z, center, rx, ry in levels:
        for j in range(segments + 1):
            angle = 0.18 + (math.tau - 0.36) * j / segments
            hem = max(0, (1.70 - z) / 0.41)
            vertices.append((center + rx * math.cos(angle), ry * math.sin(angle),
                             z + hem * (0.12 * math.cos(angle) + 0.045 * math.cos(3 * angle))))
    for i in range(len(levels) - 1):
        faces.extend([(i * (segments + 1) + j, i * (segments + 1) + j + 1,
                       (i + 1) * (segments + 1) + j + 1, (i + 1) * (segments + 1) + j)
                      for j in range(segments)])
    coat = mesh("Coat_continuous_shell", vertices, faces, colors["green"], coat_weights, 2)
    solid = coat.modifiers.new("Cloth thickness", "SOLIDIFY")
    solid.thickness = 0.035
    tube("Coat_hem_piping", vertices[:segments + 1], 0.018, colors["trim"], coat_weights)
    for edge in (0, segments):
        tube(f"Coat_front_edge_{edge}", [vertices[i * (segments + 1) + edge] for i in range(len(levels))],
             0.017, colors["trim"], coat_weights)
    for side, sign in (("near", -1), ("far", 1)):
        y = sign * 0.405
        tube(f"Shoulder_seam_{side}", [(-0.07, y * 0.83, 2.66),
             (-0.20, y, 2.52), (-0.27, y, 2.35)], 0.011, colors["green_dark"], "torso")
        pocket = box(f"Pocket_{side}", (-0.07, sign * 0.467, 1.70), (0.29, 0.035, 0.23),
                     colors["green_dark"], coat_weights, 0.035)
        tube(f"Pocket_lip_{side}", [(-0.21, sign * 0.49, 1.82),
             (-0.07, sign * 0.493, 1.79), (0.07, sign * 0.48, 1.82)],
             0.024, colors["green_light"], coat_weights)
        tube(f"Lapel_{side}", [(0.41, sign * 0.21, 2.64),
             (0.49, sign * 0.24, 2.46), (0.49, sign * 0.17, 2.25)],
             0.045, colors["green_light"], "torso")
    loft("Belt", [((0, 0, z), 0.455, 0.401) for z in (1.91, 1.93, 2.045, 2.06)],
         colors["leather"], "torso", subdiv=1)
    box("Buckle_brass", (0.26, -0.343, 1.99), (0.20, 0.065, 0.17), colors["brass"], "torso", 0.025)
    box("Buckle_inset", (0.26, -0.385, 1.99), (0.126, 0.022, 0.101), colors["leather"], "torso", 0.013)
    tube("Buckle_pin", [(0.20, -0.406, 1.99), (0.31, -0.406, 1.99)], 0.011, colors["brass"], "torso")
    for z in (2.17, 2.34):
        sphere(f"Coat_button_{z}", (0.495, -0.145, z), (0.034, 0.025, 0.034), colors["brass"], "torso")
    loft("Scarf_wrap", [((0.21, 0, z), rx, ry) for z, rx, ry in
         ((2.66, 0.28, 0.28), (2.73, 0.325, 0.30), (2.81, 0.31, 0.28), (2.87, 0.26, 0.255))],
         colors["red"], "head", subdiv=2)
    sphere("Scarf_knot", (0.46, -0.19, 2.70), (0.16, 0.135, 0.13), colors["red_light"], "head")
    mesh("Scarf_end", [(0.40, -0.27, 2.70), (0.55, -0.23, 2.70), (0.50, -0.28, 2.40),
                      (0.38, -0.29, 2.34), (0.35, -0.30, 2.42)], [(0, 1, 2, 3, 4)], colors["red"], "torso")


def build_head(colors):
    sphere("Head_fur", (0.25, 0, 3.27), (0.49, 0.35, 0.52), colors["fur"], "head")
    sphere("Muzzle", (0.66, 0, 3.16), (0.40, 0.245, 0.21), colors["muzzle"], "head", (0, -0.13, 0))
    sphere("Lower_jaw", (0.64, 0, 3.06), (0.285, 0.205, 0.115), colors["muzzle"], "head")
    vertices, faces = [], []
    rows, count = 16, 48
    for i in range(rows + 1):
        theta = 0.98 + (math.pi - 0.985) * i / rows
        for j in range(count):
            phi = j * math.tau / count
            vertices.append((0.16 + 0.61 * math.cos(theta),
                             0.425 * math.sin(theta) * math.sin(phi),
                             3.26 + 0.68 * math.sin(theta) * math.cos(phi)))
    for i in range(rows):
        faces.extend([(i * count + j, i * count + (j + 1) % count, (i + 1) * count + (j + 1) % count,
                       (i + 1) * count + j) for j in range(count)])
    hood = mesh("Hood_shell", vertices, faces, colors["green"], "head", 2)
    solid = hood.modifiers.new("Hood fabric thickness", "SOLIDIFY")
    solid.thickness = 0.045
    tube("Hood_opening_shadow", vertices[:count], 0.060, colors["green_dark"], "head", True)
    tube("Hood_gold_green_binding", [(x + 0.032, y, z) for x, y, z in vertices[:count]],
         0.026, colors["trim"], "head", True)
    tube("Hood_center_seam", [(0.15, 0, 3.94), (-0.20, 0, 3.80),
         (-0.44, 0, 3.39), (-0.41, 0, 3.07)], 0.011, colors["green_light"], "head")
    for side, sign in (("far", 1), ("near", -1)):
        sphere(f"Ear_outer_{side}", (-0.13, sign * 0.38, 3.79), (0.385, 0.12, 0.415), colors["fur_dark"], "head", (0, -0.18, 0))
        sphere(f"Ear_fur_rim_{side}", (-0.11, sign * 0.45, 3.80), (0.346, 0.066, 0.372), colors["fur"], "head", (0, -0.18, 0))
        sphere(f"Ear_inner_{side}", (-0.10, sign * 0.493, 3.80), (0.282, 0.032, 0.311), colors["ear"], "head", (0, -0.18, 0))
        sphere(f"Ear_inner_soft_{side}", (-0.11, sign * 0.516, 3.73), (0.196, 0.013, 0.208), colors["ear_light"], "head")
        sphere(f"Cheek_{side}", (0.54, sign * 0.235, 3.145), (0.265, 0.15, 0.195), colors["muzzle"], "head")
        sphere(f"Eye_patch_{side}", (0.536, sign * 0.286, 3.416), (0.147, 0.06, 0.19), colors["muzzle"], "head", (0, -0.18, 0))
        sphere(f"Eye_liner_{side}", (0.574, sign * 0.330, 3.433), (0.077, 0.037, 0.128), colors["fur_dark"], "head", (0, -0.18, 0))
        sphere(f"Eye_{side}", (0.592, sign * 0.353, 3.442), (0.048, 0.022, 0.091), colors["eye"], "head")
        sphere(f"Eye_glint_{side}", (0.609, sign * 0.372, 3.48), (0.020, 0.008, 0.027), colors["white"], "head")
        tube(f"Brow_{side}", [(0.49, sign * 0.316, 3.58), (0.55, sign * 0.326, 3.61),
             (0.615, sign * 0.30, 3.57)], 0.024, colors["fur_dark"], "head")
        tube(f"Smile_{side}", [(0.58, sign * 0.352, 3.079), (0.75, sign * 0.234, 3.07),
             (0.94, sign * 0.126, 3.14)], 0.011, colors["fur_dark"], "head")
        for i in range(3):
            tube(f"Whisker_{side}_{i}", [(0.84, sign * 0.192, 3.17 - i * 0.035),
                 (1.04, sign * 0.36, 3.23 - i * 0.105),
                 (1.27, sign * 0.45, 3.30 - i * 0.17)], 0.006, colors["whisker"], "head")
        for i in range(3):
            sphere(f"Whisker_dot_{side}_{i}", (0.81 + 0.045 * i, sign * (0.228 - 0.018 * i), 3.18),
                   (0.012, 0.01, 0.012), colors["fur_dark"], "head")
    sphere("Nose", (1.035, 0, 3.204), (0.095, 0.121, 0.08), colors["nose"], "head")
    sphere("Nose_highlight", (1.047, -0.052, 3.251), (0.040, 0.037, 0.015), colors["ear_light"], "head")


def build_tail(colors):
    points = [Vector(REST[f"tail.{i}"][0]) for i in range(4)] + [Vector(REST["tail.3"][1])]
    vertices, faces = [], []
    count = 12
    for i in range(33):
        t = i / 8
        segment = min(3, int(t))
        p = points[segment].lerp(points[segment + 1], t - segment)
        direction = (points[segment + 1] - points[segment]).normalized()
        a = Vector((0, 1, 0))
        b = direction.cross(a)
        radius = 0.074 * (1 - i / 35) ** 0.85
        for j in range(count):
            vertices.append(tuple(p + radius * (math.cos(j * math.tau / count) * a + math.sin(j * math.tau / count) * b)))
    for i in range(32):
        faces.extend([(i * count + j, i * count + (j + 1) % count,
                       (i + 1) * count + (j + 1) % count, (i + 1) * count + j) for j in range(count)])
    def weights(co):
        distances = [((co - (points[i] + points[i + 1]) / 2).length, i) for i in range(4)]
        closest = sorted(distances)[:2]
        raw = [1 / max(0.03, d) ** 4 for d, _ in closest]
        return {f"tail.{i}": w / sum(raw) for w, (_, i) in zip(raw, closest)}
    mesh("Tail_tapered", vertices, faces, colors["tail"], weights, 2)


def animate(rig):
    records = []
    for frame in range(1, FRAMES + 2):
        pose = pose_at((frame - 1) / FRAMES)
        bpy.context.scene.frame_set(frame)
        for name, (start, end) in pose["bones"].items():
            bone = rig.pose.bones[name]
            bone.matrix = bone_matrix(start, end)
            bpy.context.view_layer.update()
            for channel in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(data_path=channel, frame=frame, group=name)
        records.append({"frame": frame, **pose})
    rig.animation_data.action.name = "Mouse_Run_24fps"
    for curve in rig.animation_data.action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
    return records


def aim(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def configure_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = 64
    scene.eevee.use_gtao = True
    scene.eevee.gtao_distance = 0.3
    scene.eevee.gtao_factor = 1.15
    scene.eevee.use_soft_shadows = True
    scene.eevee.shadow_cube_size = "2048"
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "Medium High Contrast"
    scene.view_settings.exposure = 0
    scene.world.color = (0.27, 0.27, 0.27)
    scene.render.fps = FPS
    scene.frame_start, scene.frame_end = 1, FRAMES
    bpy.ops.object.camera_add(location=(6.0, -20, 5.0))
    camera = bpy.context.object
    camera.name = "Sprite_Camera"
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 5.25
    aim(camera, (-0.25, 0, 2.0))
    scene.camera = camera
    for name, location, energy, size, color in (
            ("Key_softbox", (2, -5, 7), 500, 4.0, (1.0, 0.86, 0.69)),
            ("Cool_fill", (-3, -2, 3.5), 150, 3.0, (0.65, 0.79, 1.0)),
            ("Rim", (-2, 4, 5.5), 650, 3.0, (1.0, 0.79, 0.50))):
        bpy.ops.object.light_add(type="AREA", location=location)
        light = bpy.context.object
        light.name = name
        light.data.energy, light.data.shape, light.data.size = energy, "DISK", size
        light.data.color = color
        light.data.use_shadow = True
        light.data.use_contact_shadow = True
        aim(light, (0, 0, 2))
    return scene


def render(scene, path, frame, camera_location=None, scale=None):
    scene.frame_set(frame)
    if camera_location:
        scene.camera.location = camera_location
        aim(scene.camera, (-0.25, 0, 2.0))
    if scale:
        scene.camera.data.ortho_scale = scale
    path.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def export_gltf(scene, out):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in scene.objects:
        if obj.type in ("MESH", "ARMATURE"):
            obj.select_set(True)
    scene.frame_end = FRAMES + 1
    bpy.ops.export_scene.gltf(filepath=str(out / "mouse.glb"), use_selection=True,
                              export_format="GLB", export_animations=True,
                              export_frame_range=True, export_force_sampling=True,
                              export_anim_slide_to_zero=True,
                              export_skins=True, export_yup=True)
    scene.frame_end = FRAMES


def verify_geometry(rig):
    data = []
    for frame in range(1, FRAMES + 1):
        bpy.context.scene.frame_set(frame)
        expected = pose_at((frame - 1) / FRAMES)
        error = 0
        for name in expected["bones"]:
            actual = rig.pose.bones[name]
            # Tail poses rotate rigid segments; their authored endpoint spacing
            # is allowed to vary slightly. Limb controls remain fixed length.
            for got, wanted in zip((actual.head, actual.tail), expected["bones"][name]):
                if not name.startswith(("tail.", "coat.")):
                    error = max(error, (got - Vector(wanted)).length)
        if error > 1e-4:
            raise ValueError(f"Rig does not reproduce pose at frame {frame}: {error}")
        dependency_graph = bpy.context.evaluated_depsgraph_get()
        soles = {}
        for side in ("near", "far"):
            obj = bpy.data.objects[f"Boot_sole_{side}"].evaluated_get(dependency_graph)
            evaluated = obj.to_mesh()
            lowest = min((obj.matrix_world @ vertex.co).z for vertex in evaluated.vertices)
            highest = max((obj.matrix_world @ vertex.co).z for vertex in evaluated.vertices)
            obj.to_mesh_clear()
            if lowest < -1e-5:
                raise ValueError(f"Sole below ground at {frame}/{side}: {lowest}")
            if expected["feet"][side]["planted"] and abs(lowest) > 1e-5:
                raise ValueError(f"Support sole lost contact at {frame}/{side}: {lowest}")
            if expected["feet"][side]["planted"] and abs(highest - 0.08) > 1e-5:
                raise ValueError(f"Support sole rotated away from horizontal at {frame}/{side}")
            soles[side] = lowest
        data.append({"frame": frame, "maximum_joint_error": error, "sole_minimum_z": soles})
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--preview", action="store_true")
    parser.add_argument("--export-only", action="store_true", help="Re-export GLB from the retained .blend without changing render PNGs")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if args.export_only:
        manifest = json.loads((args.out / "manifest.json").read_text())
        bpy.ops.wm.open_mainfile(filepath=str(args.out / "mouse.blend"))
        export_gltf(bpy.context.scene, args.out)
        manifest["glb_export_script_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        manifest["glb_animation_time_range"] = [0, 1]
        (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        return
    if args.out.exists():
        raise ValueError("Output already exists; keep earlier artifacts and choose a new directory")
    args.out.mkdir(parents=True)
    scene = configure_scene()
    rig = create_rig()
    colors = {name: material(name, color, rough, texture, metal) for name, color, rough, texture, metal in (
        ("green", (0.17, 0.285, 0.105), 0.9, 0.18, 0),
        ("green_light", (0.24, 0.36, 0.14), 0.9, 0.12, 0),
        ("green_dark", (0.065, 0.12, 0.039), 0.9, 0.12, 0),
        ("trim", (0.46, 0.43, 0.18), 0.83, 0, 0),
        ("fur", (0.50, 0.25, 0.105), 0.85, 0.08, 0),
        ("fur_dark", (0.17, 0.07, 0.037), 0.9, 0, 0),
        ("muzzle", (0.83, 0.61, 0.35), 0.9, 0.04, 0),
        ("ear", (0.53, 0.245, 0.19), 0.9, 0.05, 0),
        ("ear_light", (0.79, 0.43, 0.32), 0.8, 0, 0),
        ("nose", (0.31, 0.085, 0.059), 0.4, 0, 0),
        ("eye", (0.013, 0.007, 0.004), 0.23, 0, 0),
        ("white", (1.0, 0.93, 0.75), 0.3, 0, 0),
        ("whisker", (0.56, 0.37, 0.20), 0.85, 0, 0),
        ("red", (0.48, 0.041, 0.018), 0.9, 0.1, 0),
        ("red_light", (0.67, 0.08, 0.026), 0.9, 0.1, 0),
        ("trousers", (0.205, 0.13, 0.07), 0.9, 0.17, 0),
        ("trouser_light", (0.27, 0.18, 0.10), 0.9, 0.1, 0),
        ("leather", (0.24, 0.074, 0.028), 0.52, 0.10, 0),
        ("leather_light", (0.44, 0.21, 0.068), 0.62, 0.06, 0),
        ("sole", (0.07, 0.029, 0.015), 0.8, 0, 0),
        ("brass", (0.62, 0.39, 0.095), 0.42, 0, 0.5),
        ("tail", (0.40, 0.18, 0.105), 0.88, 0.08, 0),
    )}
    build_limbs(colors)
    build_clothes(colors)
    build_head(colors)
    build_tail(colors)
    records = animate(rig)
    verification = verify_geometry(rig)
    scene.frame_set(4)
    for obj in PARTS:
        obj.parent = rig
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out / "mouse.blend"))
    if args.preview:
        for frame in (1, 8, 16):
            render(scene, args.out / f"preview-{frame:02}.png", frame)
    else:
        for frame in range(1, FRAMES + 1):
            render(scene, args.out / "frames" / f"run-{frame:02}.png", frame)
        for i in range(16):
            angle = -math.pi / 2 + i * math.tau / 16
            render(scene, args.out / "turntable" / f"view-{i:02}.png", 4,
                   (15 * math.cos(angle), 15 * math.sin(angle), 4.6), 5.25)
        render(scene, args.out / "profile.png", 4, (0, -20, 2.0), 5.25)
        render(scene, args.out / "hero.png", 4, (6, -20, 5), 5.25)
        export_gltf(scene, args.out)
    manifest = {"version": 1, "blender": bpy.app.version_string, "fps": FPS,
                "frame_count": FRAMES, "resolution": [512, 512],
                "orthographic_scale": 5.25, "camera_location": [6, -20, 5],
                "camera_target": [-0.25, 0, 2.0], "origin": [0, 0, 0],
                "source": "interactive-run-source-v1.png (appearance reference only)",
                "method": "Authored geometry, vertex weights and baked armature animation; no generated frames",
                "model_meshes": len(PARTS), "bones": len(REST),
                "geometry_checks": verification, "poses": records,
                "script_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in (Path(__file__), Path(__file__).with_name("motion.py"))}}
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Completed mouse asset: {args.out}", flush=True)


if __name__ == "__main__":
    main()
