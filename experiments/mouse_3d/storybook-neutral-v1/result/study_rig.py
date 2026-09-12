"""Authored controls and provisional materials for the inspected neutral mesh.

Landmarks are estimates in the normalized Blender coordinate frame. These
controls are for two deformation checks, not a production facial/body rig.
"""

import math
import bpy
from mathutils import Vector


def smooth(a, b, value):
    t = min(1, max(0, (value-a)/(b-a)))
    return t*t*(3-2*t)


def mix(a, b, amount):
    return tuple(x+(y-x)*amount for x, y in zip(a, b))


def study_colors(obj):
    base, cream, pink = (0.44, 0.205, 0.064), (0.82, 0.64, 0.39), (0.68, 0.285, 0.185)
    white, iris, pupil = (0.94, 0.875, 0.70), (0.19, 0.077, 0.022), (0.008, 0.005, 0.003)
    colors = obj.data.color_attributes.new(name='ReferenceColor', type='FLOAT_COLOR', domain='POINT')
    for vertex, item in zip(obj.data.vertices, colors.data):
        x, y, z = vertex.co
        rgb = base
        front = 1-smooth(-0.85, -0.48, y)
        belly = 1-smooth(0.72, 1.03, math.hypot((x-0.17)/0.31, (z-1.94)/0.66))
        rgb = mix(rgb, cream, belly*front)
        if z > 2.68:
            cheek = (1-smooth(3.02, 3.18, z))*smooth(2.68, 2.85, z)
            cheek *= 1-smooth(0.48, 0.70, abs(x-0.18))
            rgb = mix(rgb, cream, cheek*front)
            for cx, cz in ((-0.10, 3.215), (0.475, 3.225)):
                u, v = (x-cx)/0.185, (z-cz)/0.255
                radius = math.hypot(u, v)
                patch = (1-smooth(0.86, 1.05, radius))*front
                eye_color = white
                iris_radius = math.hypot((x-cx-0.025)/0.113, (z-cz-0.025)/0.172)
                eye_color = mix(eye_color, iris, 1-smooth(0.89, 1.01, iris_radius))
                pupil_radius = math.hypot((x-cx-0.03)/0.070, (z-cz-0.031)/0.135)
                eye_color = mix(eye_color, pupil, 1-smooth(0.88, 1.03, pupil_radius))
                glint = math.hypot((x-cx+0.012)/0.026, (z-cz-0.099)/0.036)
                eye_color = mix(eye_color, (1.0, 0.97, 0.86), 1-smooth(0.65, 1.05, glint))
                rgb = mix(rgb, eye_color, patch)
                brow_z = cz+0.31-1.1*(x-cx)**2
                brow = math.exp(-((z-brow_z)/0.026)**2)*(1-smooth(0.14, 0.20, abs(x-cx)))*front
                rgb = mix(rgb, (0.19, 0.080, 0.022), brow)
            nose = 1-smooth(0.77, 1.05, math.hypot((x-0.18)/0.135, (z-2.995)/0.092))
            rgb = mix(rgb, (0.43, 0.115, 0.060), nose*(1-smooth(-1.10, -1.04, y)))
        if z > 3.32:
            for cx, cz in ((-0.78, 3.82), (1.01, 3.85)):
                radius = math.hypot((x-cx)/0.43, (z-cz)/0.47)
                bowl = (1-smooth(0.72, 1.02, radius))*(1-smooth(-0.68, -0.40, y))
                rgb = mix(rgb, pink, bowl)
        if z < 0.35:
            rgb = mix(rgb, (0.66, 0.40, 0.18), (1-smooth(0.18, 0.34, z))*front)
        if y > 0.20:
            tuft = smooth(0.85, 1.25, -x)*smooth(0.90, 1.40, z)
            rgb = mix(rgb, cream, tuft)
        item.color = (*rgb, 1)
    return {'method': 'Authored 3D-space study colors aligned to measured face features; no generated texture maps',
            'eye_centers_xz': [[-0.10, 3.215], [0.475, 3.225]], 'nose_center_xz': [0.18, 2.995]}


def bone_definitions():
    bones = {
        'root': ((0.17,-0.60,1.38), (0.17,-0.60,1.61), None),
        'spine': ((0.17,-0.60,1.38), (0.17,-0.65,2.47), 'root'),
        'neck': ((0.17,-0.65,2.47), (0.17,-0.62,2.78), 'spine'),
        'head': ((0.17,-0.62,2.78), (0.17,-0.55,3.71), 'neck'),
    }
    for side, shoulder, elbow, wrist, hand, hip, knee, ankle, toe in (
            ('left', (-0.26,-0.68,2.48), (-0.70,-0.65,1.98), (-1.04,-0.67,1.51), (-1.07,-0.72,1.26),
             (-0.16,-0.60,1.43), (-0.30,-0.49,0.82), (-0.43,-0.40,0.30), (-0.43,-0.93,0.14)),
            ('right', (0.63,-0.68,2.48), (1.01,-0.65,1.98), (1.31,-0.67,1.51), (1.33,-0.72,1.26),
             (0.48,-0.60,1.43), (0.63,-0.49,0.82), (0.69,-0.40,0.30), (0.71,-0.93,0.14))):
        bones[f'upper_arm.{side}'] = (shoulder, elbow, 'spine')
        bones[f'forearm.{side}'] = (elbow, wrist, f'upper_arm.{side}')
        bones[f'hand.{side}'] = (wrist, hand, f'forearm.{side}')
        bones[f'thigh.{side}'] = (hip, knee, 'root')
        bones[f'shin.{side}'] = (knee, ankle, f'thigh.{side}')
        bones[f'foot.{side}'] = (ankle, toe, f'shin.{side}')
    tail = [(0.05,-0.29,1.26), (-0.20,0.28,0.98), (-0.78,0.78,0.86), (-1.18,0.85,1.19), (-1.26,0.88,1.55)]
    for i in range(4):
        bones[f'tail.{i}'] = (tail[i], tail[i+1], 'root' if i == 0 else f'tail.{i-1}')
    return bones


def create_study_rig(obj):
    definitions = bone_definitions()
    data = bpy.data.armatures.new('Study skeleton')
    rig = bpy.data.objects.new('Mouse_Study_Rig', data)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for name, (head, tail, parent) in definitions.items():
        bone = data.edit_bones.new(name)
        bone.head, bone.tail = head, tail
        direction = (Vector(tail)-Vector(head)).normalized()
        across = Vector((0,1,0))
        across = (across-direction*across.dot(direction)).normalized()
        bone.align_roll(across.cross(direction))
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(True)
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    missing = sum(not vertex.groups for vertex in obj.data.vertices)
    if missing:
        raise ValueError(f'Automatic binding left {missing} vertices without weights')
    # A rigid head keeps all generated facial detail and ears together during
    # these body checks. Facial controls require a later deliberate mesh pass.
    head_group = obj.vertex_groups['head']
    rigid = [v.index for v in obj.data.vertices if v.co.z > 2.78]
    for group in obj.vertex_groups:
        group.remove(rigid)
    head_group.add(rigid, 1, 'REPLACE')
    for side in ('left','right'):
        rigid_foot = [v.index for v in obj.data.vertices if v.co.z < 0.34 and
                      ((v.co.x < 0.17) == (side == 'left'))]
        for group in obj.vertex_groups:
            group.remove(rigid_foot)
        obj.vertex_groups[f'foot.{side}'].add(rigid_foot, 1, 'REPLACE')
    for vertex in obj.data.vertices:
        ranked = sorted([(g.group, g.weight) for g in vertex.groups if g.weight > 1e-8], key=lambda item: -item[1])[:4]
        total = sum(weight for _, weight in ranked)
        if total == 0:
            raise ValueError('Binding produced a zero-weight vertex')
        for group in obj.vertex_groups:
            group.remove([vertex.index])
        for index, weight in ranked:
            obj.vertex_groups[index].add([vertex.index], weight/total, 'REPLACE')
    for modifier in obj.modifiers:
        if modifier.type == 'ARMATURE':
            modifier.use_deform_preserve_volume = True
    rig.show_in_front = True
    return rig, {'method': 'Blender automatic weights, rigid head and paws, strongest four influences normalized',
                 'unweighted_vertices': missing, 'bones': definitions,
                 'scope': 'Temporary body deformation checks; no facial or finger controls'}


def pose_study(rig, name):
    for bone in rig.pose.bones:
        bone.rotation_mode = 'XYZ'
        bone.rotation_euler = (0,0,0)
    if name == 'reach':
        for side, sign in (('left',1),('right',-1)):
            rig.pose.bones[f'upper_arm.{side}'].rotation_euler.x = sign*0.28
            rig.pose.bones[f'forearm.{side}'].rotation_euler.x = sign*0.85
    elif name == 'step':
        rig.pose.bones['thigh.right'].rotation_euler.z = 0.68
        rig.pose.bones['shin.right'].rotation_euler.z = -1.12
        rig.pose.bones['foot.right'].rotation_euler.z = 0.44
        rig.pose.bones['upper_arm.left'].rotation_euler.z = -0.35
        rig.pose.bones['forearm.left'].rotation_euler.x = 0.55
    elif name != 'neutral':
        raise ValueError(f'Unknown study pose: {name}')
    bpy.context.view_layer.update()
