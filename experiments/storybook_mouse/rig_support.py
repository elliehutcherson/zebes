"""Shared Blender rig operations for storybook inspection and animation.

This module owns posing, rigid-paw queries, and source-data fingerprints.
It does not select a run revision, open/save files, or author an action.
"""

import hashlib
import math
import struct

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Quaternion


def fingerprint(obj, rig):
    """Hash source data, including weights, independent of animation/scene state."""
    h = hashlib.sha256()
    for vertex in obj.data.vertices:
        h.update(struct.pack('<3f', *vertex.co))
        for group in vertex.groups:
            h.update(struct.pack('<If', group.group, group.weight))
    for polygon in obj.data.polygons:
        h.update(struct.pack('<3I', *polygon.vertices))
    for layer in obj.data.uv_layers:
        for item in layer.data:
            h.update(struct.pack('<2f', *item.uv))
    for bone in rig.data.bones:
        for row in bone.matrix_local:
            h.update(struct.pack('<4f', *row))
        h.update(struct.pack('<f', bone.length))
    return {'mesh_uv_weights_rest_rig_sha256': h.hexdigest(),
            'packed_images': {im.name: hashlib.sha256(im.packed_file.data).hexdigest()
                              for im in bpy.data.images if im.packed_file},
            'vertices': len(obj.data.vertices), 'triangles': len(obj.data.polygons),
            'linked_libraries': len(bpy.data.libraries)}


def update():
    bpy.context.view_layer.update()


def rotated_bone(rig, name, head, rotation):
    bone = rig.pose.bones[name]
    bone.matrix = (Matrix.Translation(head) @ rotation.to_matrix().to_4x4()
                   @ bone.bone.matrix_local.to_3x3().to_4x4())
    update()


def point_bone(rig, name, head, direction):
    rest = rig.data.bones[name]
    rotation = (rest.tail_local-rest.head_local).rotation_difference(direction.normalized())
    rotated_bone(rig, name, head, rotation)


def inherited_head(rig, name):
    bone = rig.pose.bones[name]
    if not bone.parent:
        return bone.bone.head_local.copy()
    return bone.parent.matrix @ bone.parent.bone.matrix_local.inverted() @ bone.bone.head_local


def two_bone_joint(start, end, first, second, pole):
    delta = end-start
    distance = delta.length
    if not abs(first-second) + 1e-5 < distance < first+second - 1e-5:
        raise ValueError(f'Unreachable paw: distance {distance:.5f}, lengths {first:.5f}/{second:.5f}')
    axis = delta / distance
    bend = (pole-axis*axis.dot(pole)).normalized()
    along = (first*first - second*second + distance*distance)/(2*distance)
    return start + axis*along + bend*math.sqrt(first*first-along*along)


def paw_geometry(obj, rig, support_pitch=None):
    result = {}
    for side in ('left', 'right'):
        index = obj.vertex_groups[f'foot.{side}'].index
        rigid = [v for v in obj.data.vertices if any(g.group == index and g.weight > .999 for g in v.groups)]
        sole = min(rigid, key=lambda v: v.co.z)
        low = [v for v in rigid if v.co.z < .10]
        heel = max(low, key=lambda v: v.co.y)
        toe = min(low, key=lambda v: v.co.y)
        ankle = rig.data.bones[f'foot.{side}'].head_local
        result[side] = {'vertices': rigid, 'ankle': ankle.copy(),
                        'markers': {'sole': sole.index, 'heel': heel.index, 'toe': toe.index}}
        if support_pitch:
            rotations = [Quaternion((1, 0, 0), support_pitch[0]+i*(support_pitch[1]-support_pitch[0])/100)
                         for i in range(101)]
            supports = {min(rigid, key=lambda v: (r @ v.co).z).index for r in rotations}
            if len(supports) != 1:
                raise ValueError(f'{side} needs articulated or rolling support over this pitch range')
            result[side]['markers']['pad'] = supports.pop()
            result[side]['pad'] = obj.data.vertices[result[side]['markers']['pad']].co.copy()
    return result


def project(scene, point):
    p = world_to_camera_view(scene, scene.camera, point)
    return [p.x*512, (1-p.y)*512]


def reset(rig):
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    update()
