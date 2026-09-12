"""A local foot-shape and toe-articulation study from the accepted packed master.

Blender owns deformation and rendering. No topology, UV, material, automatic
binding, or geometry outside the measured lower-leg region is changed.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rig_support import inherited_head, point_bone, project, reset, rotated_bone, two_bone_joint
from inspect_run_master import MASTER_SHA256


SIDES = ('left', 'right')
POSES = {1: 'Neutral support', 9: 'Landing', 17: 'Compression',
         25: 'Push-off', 33: 'Folded recovery'}
# Root Y/Z, foot rotation relative to its new rest direction, absolute toe
# rotation, left toe-base Y, and toe clearance. This is a pose test, not a run.
KEYS = {
    1: (0, -.06, 0, 0, -.82, 0),
    9: (.03, -.075, .06, 0, -1.00, 0),
    17: (-.10, -.20, -.18, 0, -1.00, 0),
    25: (-.48, -.035, .60, .40, -1.00, 0),
    33: (-.26, -.065, 1.00, .82, -.60, .30),
}


def smooth(a, b, value):
    t = max(0, min(1, (value-a)/(b-a)))
    return t*t*(3-2*t)


def reshape(co, center_x):
    x, y, z = co
    rear = smooth(-.86, -.32, y)
    local = 1-smooth(.34, .72, z)
    return Vector((center_x+(x-center_x)*(1-.22*rear*local),
                   y+.035*rear*local, z+.20*rear*local))


def state(obj, rig):
    return {
        'vertices': [list(v.co) for v in obj.data.vertices],
        'weights': [{obj.vertex_groups[g.group].name: g.weight for g in v.groups}
                    for v in obj.data.vertices],
        'bones': {b.name: {'head': list(b.head_local), 'tail': list(b.tail_local),
                           'length': b.length, 'parent': b.parent.name if b.parent else None}
                  for b in rig.data.bones},
        'faces': [list(p.vertices) for p in obj.data.polygons],
        'uv_sha256': hashlib.sha256(b''.join(struct.pack('<2f', *d.uv)
                                            for l in obj.data.uv_layers for d in l.data)).hexdigest(),
        'images': {im.name: hashlib.sha256(im.packed_file.data).hexdigest()
                   for im in bpy.data.images if im.packed_file},
    }


def revise(obj, rig, original):
    region = []
    for vertex in obj.data.vertices:
        weights = original['weights'][vertex.index]
        side = 'left' if vertex.co.x < .17 else 'right'
        leg = sum(weights.get(f'{part}.{side}', 0) for part in ('thigh', 'shin', 'foot'))
        if vertex.co.z >= .72 or leg < .99:
            continue
        region.append(vertex.index)
        vertex.co = reshape(vertex.co, -.43 if side == 'left' else .69)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for side in SIDES:
        foot = rig.data.edit_bones[f'foot.{side}']
        hock = reshape(foot.head.copy(), foot.head.x)
        toe_base = Vector((foot.head.x, -.82, .12))
        toe_tip = Vector((foot.head.x, -1.06, .12))
        rig.data.edit_bones[f'shin.{side}'].tail = hock
        foot.head, foot.tail = hock, toe_base
        foot.use_connect = True
        rig.data.edit_bones[f'shin.{side}'].use_connect = True
        toe = rig.data.edit_bones.new(f'toe.{side}')
        toe.head, toe.tail, toe.parent, toe.use_connect = toe_base, toe_tip, foot, True
        toe.align_roll(Vector((0, 0, 1)))
    bpy.ops.object.mode_set(mode='OBJECT')
    for side in SIDES:
        obj.vertex_groups.new(name=f'toe.{side}')
    for index in region:
        x, y, z = original['vertices'][index]
        side = 'left' if x < .17 else 'right'
        old = original['weights'][index]
        foot, shin, toe = (f'{part}.{side}' for part in ('foot', 'shin', 'toe'))
        amount = old.get(foot, 0)+old.get(shin, 0)
        toes = (1-smooth(-.88, -.70, y))*(1-smooth(.28, .42, z))
        shin_fraction = smooth(.24, .56, z)
        blend = 1-smooth(.50, .72, z)
        revised = {foot: amount*(1-toes)*(1-shin_fraction),
                   shin: amount*(1-toes)*shin_fraction, toe: amount*toes}
        for name, weight in revised.items():
            value = old.get(name, 0)*(1-blend)+weight*blend
            obj.vertex_groups[name].remove([index])
            if value > 0:
                obj.vertex_groups[name].add([index], value, 'REPLACE')
    bpy.context.view_layer.update()
    return region


def camera(scene, view):
    target = Vector((.17, -.3, 2.13))
    location = {'game': (-10, -7, 2.13), 'side': (-10, -.3, 2.13),
                'front': (.17, -12, 2.13)}[view]
    scene.camera.location = location
    scene.camera.rotation_euler = (target-scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 5.2
    bpy.context.view_layer.update()


def render(scene, path):
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def foot_geometry(obj, rig):
    result = {}
    for side in SIDES:
        group = obj.vertex_groups[f'toe.{side}'].index
        # The blend boundary can carry tiny shin/foot influences. It belongs
        # to deformation diagnostics, not the independently planted toe patch.
        toes = [v.index for v in obj.data.vertices
                if any(g.group == group and g.weight == 1.0 for g in v.groups)]
        if len(toes) < 100:
            raise ValueError(f'{side} has no substantial independent toe patch')
        result[side] = {'toes': toes,
                        'base': rig.data.bones[f'toe.{side}'].head_local.copy(),
                        'hock': rig.data.bones[f'foot.{side}'].head_local.copy()}
    return result


def parameters(frame):
    if not 1 <= frame <= 33:
        raise ValueError('Study frame must be within 1..33')
    first = min(25, 1+8*int((frame-1)//8))
    t = smooth(first, first+8, frame)
    return tuple(a+(b-a)*t for a, b in zip(KEYS[first], KEYS[first+8]))


def pose(obj, rig, geometry, frame):
    reset(rig)
    dy, dz, pitch, toe_pitch, toe_y, clearance = parameters(frame)
    root = rig.data.bones['root']
    rotated_bone(rig, 'root', root.head_local+Vector((0, dy, dz)), Quaternion((1, 0, 0), 0))
    rotated_bone(rig, 'spine', inherited_head(rig, 'spine'), Quaternion((1, 0, 0), .055))
    rotated_bone(rig, 'head', inherited_head(rig, 'head'), Quaternion((1, 0, 0), 0))
    for side, sign in (('left', -1), ('right', 1)):
        geo = geometry[side]
        foot_rotation = Quaternion((1, 0, 0), pitch if side == 'left' else .12)
        toe_rotation = Quaternion((1, 0, 0), toe_pitch if side == 'left' else 0)
        base = geo['base'].copy()
        # During roll, preserve the front contact location rather than rotating
        # the pad into the floor. The pivot is an actual lowest toe vertex.
        support = min(geo['toes'], key=lambda i: (toe_rotation @ (obj.data.vertices[i].co-geo['base'])).z)
        offset = toe_rotation @ (obj.data.vertices[support].co-geo['base'])
        base.z = (clearance if side == 'left' else 0)-offset.z
        base.y = toe_y if side == 'left' else -1.15
        if side == 'left' and frame <= 25:
            base.y -= offset.y-(obj.data.vertices[support].co-geo['base']).y
        hock = base+foot_rotation @ (geo['hock']-geo['base'])
        hip = inherited_head(rig, f'thigh.{side}')
        try:
            knee = two_bone_joint(hip, hock, rig.data.bones[f'thigh.{side}'].length,
                                  rig.data.bones[f'shin.{side}'].length, Vector((sign*.12, -1, 0)))
        except ValueError as error:
            raise ValueError(f'Frame {frame}, {side}: {error}') from error
        point_bone(rig, f'thigh.{side}', hip, knee-hip)
        point_bone(rig, f'shin.{side}', knee, hock-knee)
        rotated_bone(rig, f'foot.{side}', hock, foot_rotation)
        rotated_bone(rig, f'toe.{side}', base, toe_rotation)
        # Keep the feet visible while retaining relaxed, bent arms.
        shoulder = inherited_head(rig, f'upper_arm.{side}')
        upper = Vector((sign*.22, .08, -1)).normalized()
        lower = Vector((sign*.12, -.85, -.65)).normalized()
        point_bone(rig, f'upper_arm.{side}', shoulder, upper)
        point_bone(rig, f'forearm.{side}', rig.pose.bones[f'upper_arm.{side}'].tail.copy(), lower)
        point_bone(rig, f'hand.{side}', rig.pose.bones[f'forearm.{side}'].tail.copy(), lower)


def measure(scene, obj, rig, geometry, frame, region, rest_edges):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    coordinates = [evaluated.matrix_world @ v.co for v in mesh.vertices]
    bones = {b.name: [list(b.head), list(b.tail)] for b in rig.pose.bones}
    ratios = sorted((coordinates[a]-coordinates[b]).length/length for a, b, length in rest_edges)
    result = {
        'frame': frame, 'lowest_z': min(v.z for v in coordinates), 'bones': bones,
        'pixels': {name: [project(scene, Vector(p)) for p in ends] for name, ends in bones.items()},
        'toe_lowest_z': {side: min(coordinates[i].z for i in geo['toes']) for side, geo in geometry.items()},
        'toe_patch': {side: [list(coordinates[i]) for i in geo['toes']] for side, geo in geometry.items()},
        'local_edge_ratio': {'min': ratios[0], 'p01': ratios[len(ratios)//100],
                             'p99': ratios[len(ratios)*99//100], 'max': ratios[-1]},
        'minimum_local_face_area': min(p.area for p in mesh.polygons if any(i in region for i in p.vertices)),
    }
    evaluated.to_mesh_clear()
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--master', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if hashlib.sha256(args.master.read_bytes()).hexdigest() != MASTER_SHA256:
        raise ValueError('Source differs from accepted master')
    args.out.mkdir(parents=True, exist_ok=False)
    bpy.ops.wm.open_mainfile(filepath=str(args.master))
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'foot-study.blend'))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    original_action = rig.animation_data.action
    original_action.use_fake_user = True
    rig.animation_data.action = None
    reset(rig)
    original = state(obj, rig)
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.film_transparent = True
    scene.eevee.taa_render_samples = 48
    comparison = {}
    for view in ('front', 'side', 'game'):
        camera(scene, view)
        comparison[f'original-{view}'] = {b.name: [project(scene, b.head), project(scene, b.tail)] for b in rig.pose.bones}
        render(scene, args.out/f'original-{view}.png')
    region = revise(obj, rig, original)
    revised = state(obj, rig)
    for invariant in ('faces', 'uv_sha256', 'images'):
        if original[invariant] != revised[invariant]:
            raise ValueError(f'Foot study changed {invariant}')
    changes = {field: [i for i, (a, b) in enumerate(zip(original[field], revised[field])) if a != b]
               for field in ('vertices', 'weights')}
    if not all(set(indices) <= set(region) for indices in changes.values()):
        raise ValueError('Edits escaped lower-leg region')
    region_set = set(region)
    outside_hashes = [hashlib.sha256(json.dumps(
        [(snapshot['vertices'][i], snapshot['weights'][i]) for i in range(len(obj.data.vertices))
         if i not in region_set], sort_keys=True).encode()).hexdigest() for snapshot in (original, revised)]
    for view in ('front', 'side', 'game'):
        camera(scene, view)
        comparison[f'revised-{view}'] = {b.name: [project(scene, b.head), project(scene, b.tail)] for b in rig.pose.bones}
        render(scene, args.out/f'revised-{view}.png')
    geometry = foot_geometry(obj, rig)
    rig.animation_data.action = bpy.data.actions.new('Foot_support_articulation_01')
    for bone in rig.pose.bones:
        bone.rotation_mode = 'QUATERNION'
    previous = {}
    for index in range(129):
        frame = 1+index/4
        pose(obj, rig, geometry, frame)
        for bone in rig.pose.bones:
            if bone.name in previous and bone.rotation_quaternion.dot(previous[bone.name]) < 0:
                bone.rotation_quaternion.negate()
            previous[bone.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert('location', frame=frame, group=bone.name)
            bone.keyframe_insert('rotation_quaternion', frame=frame, group=bone.name)
    for curve in rig.animation_data.action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
    for marker in list(scene.timeline_markers):
        scene.timeline_markers.remove(marker)
    for frame, name in POSES.items():
        scene.timeline_markers.new(name, frame=frame)
    scene.frame_start, scene.frame_end, scene.render.fps = 1, 33, 24
    camera(scene, 'game')
    scene.frame_set(17)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'foot-study.blend'))
    # Reopen before all motion checks and final renders.
    bpy.ops.wm.open_mainfile(filepath=str(args.out/'foot-study.blend'))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    if state(obj, rig) != revised:
        raise ValueError('Saved copy differs from authored source data')
    region_set = set(region)
    edges = [(e.vertices[0], e.vertices[1], (obj.data.vertices[e.vertices[0]].co-obj.data.vertices[e.vertices[1]].co).length)
             for e in obj.data.edges if any(i in region_set for i in e.vertices)]
    checks = []
    for index in range(257):
        frame = 1+index/8
        scene.frame_set(int(frame), subframe=frame-int(frame))
        check = measure(scene, obj, rig, geometry, frame, region_set, edges)
        # Dense measurements retain patch bounds; full points are only needed
        # at the three support poses to verify the planted area, not just a tip.
        if frame not in (9, 13, 17):
            del check['toe_patch']
        checks.append(check)
    for view in ('side', 'game'):
        camera(scene, view)
        (args.out/f'frames-{view}').mkdir()
        for frame in (POSES if args.preview else range(1, 34)):
            scene.frame_set(frame)
            render(scene, args.out/f'frames-{view}'/f'pose-{frame:02}.png')
    camera(scene, 'side')
    side_pixels = {}
    for frame in range(1, 34):
        scene.frame_set(frame)
        side_pixels[frame] = {b.name: [project(scene, b.head), project(scene, b.tail)] for b in rig.pose.bones}
    report = {
        'master_sha256': MASTER_SHA256, 'saved_copy_verified': True,
        'status': 'awaiting_visual_review', 'action': rig.animation_data.action.name,
        'poses': POSES, 'fps': 24, 'ground_z': 0, 'ortho_scale': 5.2,
        'ground_row_512': project(scene, Vector((0, 0, 0)))[1],
        'region_vertex_indices': region, 'changed_vertex_indices': changes['vertices'],
        'outside_region_sha256_before_after': outside_hashes,
        'changed_weight_indices': changes['weights'],
        'original_region_bounds': [[min(original['vertices'][i][axis] for i in region) for axis in range(3)],
                                   [max(original['vertices'][i][axis] for i in region) for axis in range(3)]],
        'changed_bones': [name for name in revised['bones'] if original['bones'].get(name) != revised['bones'][name]],
        'original_bones': original['bones'], 'rest_bones': revised['bones'],
        'topology_unchanged': True, 'uv_sha256': revised['uv_sha256'], 'images': revised['images'],
        'vertex_count': len(obj.data.vertices), 'triangles': len(obj.data.polygons),
        'max_weight_sum_error': max(abs(sum(w.values())-1) for w in revised['weights']),
        'comparison_pixels': comparison, 'side_pixels': side_pixels, 'checks': checks,
    }
    (args.out/'study.json').write_text(json.dumps(report, indent=2)+'\n')
    for name in ('foot_study.py', 'rig_support.py', 'inspect_run_master.py'):
        shutil.copyfile(Path(__file__).with_name(name), args.out/name)
    print(json.dumps({'out': str(args.out), 'changed_vertices': len(changes['vertices']),
                      'changed_weights': len(changes['weights']), 'changed_bones': report['changed_bones'],
                      'lowest_z': min(c['lowest_z'] for c in checks)}), flush=True)


if __name__ == '__main__':
    main()
