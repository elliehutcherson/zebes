"""Load the positively reviewed foot study and author an editable closed run.

All deformation diagnostics evaluate Blender's saved action and actual mesh.
The original shape, texture, toe articulation and rest rig remain preserved.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Quaternion, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from animate_run import inherited_head, point_bone, project, rotated_bone, two_bone_joint
from foot_study import camera, foot_geometry, render, reset, smooth, state
import run_motion_v3 as motion

SIDES = (('left', 0., -1), ('right', .5, 1))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def repair_weights(obj, original):
    """Replace the measured rear-hock weight cliff with a broad local blend.

    The first loop stretches edges 13132/13374 by 5.44x where foot influence
    changes by .119 over .0093 units. Toe weights and all non-leg groups stay
    fixed. The blend fades to the original binding at both region boundaries.
    """
    region = []
    for vertex in obj.data.vertices:
        old = original['weights'][vertex.index]
        side = 'left' if vertex.co.x < .17 else 'right'
        names = [f'{part}.{side}' for part in ('thigh', 'shin', 'foot')]
        amount = sum(old.get(name, 0) for name in names)
        if amount < .99 or not .24 < vertex.co.z < 1.08:
            continue
        region.append(vertex.index)
        z = vertex.co.z
        blend = smooth(.24, .34, z)*(1-smooth(.88, 1.08, z))
        thigh = smooth(.66, 1.02, z)
        foot = (1-smooth(.27, .69, z))*(1-thigh)
        for name, fraction in zip(names, (thigh, 1-thigh-foot, foot)):
            value = old.get(name, 0)*(1-blend)+amount*fraction*blend
            obj.vertex_groups[name].remove([vertex.index])
            if value > 0:
                obj.vertex_groups[name].add([vertex.index], value, 'REPLACE')
    bpy.context.view_layer.update()
    return region


def pose(obj, rig, geometry, phase):
    reset(rig)
    body = motion.body(phase)
    rotated_bone(rig, 'root', rig.data.bones['root'].head_local + Vector((0, 0, body['bob'])),
                 Quaternion((1, 0, 0), .025))
    rotated_bone(rig, 'spine', inherited_head(rig, 'spine'),
                 Quaternion((0, 0, 1), body['twist']) @ Quaternion((1, 0, 0), body['lean']))
    rotated_bone(rig, 'head', inherited_head(rig, 'head'), Quaternion((1, 0, 0), .015))
    for side, offset, sign in SIDES:
        local = (phase+offset) % 1
        path, geo = motion.paw(local), geometry[side]
        foot_rotation = Quaternion((1, 0, 0), path['foot_pitch'])
        toe_rotation = Quaternion((1, 0, 0), path['toe_pitch'])
        pivot = toe_rotation @ (obj.data.vertices[geo['pivot']].co-geo['base'])
        lowest = min((toe_rotation @ (obj.data.vertices[i].co-geo['base'])).z for i in geo['toes'])
        base = Vector((geo['base'].x, path['y']-pivot.y, path['clearance']-lowest))
        hock = base+foot_rotation @ (geo['hock']-geo['base'])
        hip = inherited_head(rig, f'thigh.{side}')
        try:
            knee = two_bone_joint(hip, hock, rig.data.bones[f'thigh.{side}'].length,
                                  rig.data.bones[f'shin.{side}'].length, Vector((sign*.12, -1, 0)))
        except ValueError as error:
            raise ValueError(f'Phase {phase:.6f}, {side}: {error}') from error
        point_bone(rig, f'thigh.{side}', hip, knee-hip)
        point_bone(rig, f'shin.{side}', knee, hock-knee)
        rotated_bone(rig, f'foot.{side}', hock, foot_rotation)
        rotated_bone(rig, f'toe.{side}', base, toe_rotation)
        arm = motion.arm(local)
        angle = arm['swing']
        upper = Vector((sign*.32, -math.sin(angle), -math.cos(angle))).normalized()
        lower = Vector((sign*.16, -math.sin(angle+arm['bend']), -math.cos(angle+arm['bend']))).normalized()
        hand_angle = angle+arm['bend']+arm['wrist']
        hand = Vector((sign*.13, -math.sin(hand_angle), -math.cos(hand_angle))).normalized()
        point_bone(rig, f'upper_arm.{side}', inherited_head(rig, f'upper_arm.{side}'), upper)
        point_bone(rig, f'forearm.{side}', rig.pose.bones[f'upper_arm.{side}'].tail.copy(), lower)
        point_bone(rig, f'hand.{side}', rig.pose.bones[f'forearm.{side}'].tail.copy(), hand)
    for index in range(4):
        rig.pose.bones[f'tail.{index}'].rotation_quaternion = (
            Quaternion((1, 0, 0), .07*math.sin(2*math.pi*phase-index*.55)) @
            Quaternion((0, 0, 1), .035*math.sin(2*math.pi*phase-index*.45)))
    bpy.context.view_layer.update()


def diagnostics(obj, rig, geometry):
    edges = [(e.vertices[0], e.vertices[1],
              (obj.data.vertices[e.vertices[0]].co-obj.data.vertices[e.vertices[1]].co).length)
             for e in obj.data.edges]
    weights = [{obj.vertex_groups[g.group].name: g.weight for g in v.groups} for v in obj.data.vertices]
    body = {i for i, w in enumerate(weights) if
            sum(value for name, value in w.items() if 'arm.' in name or 'hand.' in name or 'tail.' in name) < .02}
    arms = {side: {i for i,w in enumerate(weights) if w.get(f'forearm.{side}',0)+w.get(f'hand.{side}',0) > .8}
            for side, _, _ in SIDES}
    # The study's hands blend with the forearms (maximum hand influence is
    # .628/.714); select the hand-dominant vertices, not a nonexistent rigid set.
    hands = {side: [i for i,w in enumerate(weights) if w.get(f'hand.{side}',0) > .5] for side,_,_ in SIDES}
    if any(not indices for indices in hands.values()):
        raise ValueError('No hand-dominant surface for clearance measurement')
    return {'edges': edges,
            'body_faces': [list(p.vertices) for p in obj.data.polygons if set(p.vertices) <= body],
            'arm_faces': {side: [list(p.vertices) for p in obj.data.polygons if set(p.vertices) <= indices]
                          for side, indices in arms.items()}, 'hands': hands}


def measure(scene, obj, rig, geometry, phase, diag):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    coords = [evaluated.matrix_world @ v.co for v in mesh.vertices]
    edges = sorted(((coords[a]-coords[b]).length/length, a, b) for a, b, length in diag['edges'])
    bones = {b.name: [list(b.head), list(b.tail)] for b in rig.pose.bones}
    record = {'phase': phase, 'frame': 1+phase*24, 'lowest_z': min(p.z for p in coords),
              'bones': bones, 'rig_pixels': {b.name: [project(scene, b.head), project(scene, b.tail)]
                                           for b in rig.pose.bones}, 'paws': {},
              'edge_ratio': {'min': edges[0][0], 'p01': edges[len(edges)//100][0],
                             'p99': edges[len(edges)*99//100][0], 'max': edges[-1][0]},
              'worst_edges': [{'ratio': ratio, 'indices': [a, b],
                               'rest': [list(obj.data.vertices[i].co) for i in (a, b)]}
                              for ratio, a, b in edges[-8:]],
              'minimum_face_area': min(p.area for p in mesh.polygons)}
    body_tree = BVHTree.FromPolygons(coords, diag['body_faces'], all_triangles=True)
    record['arm_clearance'] = {}
    for side, _, _ in SIDES:
        arm_tree = BVHTree.FromPolygons(coords, diag['arm_faces'][side], all_triangles=True)
        record['arm_clearance'][side] = {
            'forearm_hand_body_triangle_overlaps': len(arm_tree.overlap(body_tree)),
            'min_hand_body_distance': min(body_tree.find_nearest(coords[i])[3] for i in diag['hands'][side])}
    for side, offset, sign in SIDES:
        geo = geometry[side]
        path = motion.paw((phase+offset) % 1)
        markers = {'pad': coords[geo['pivot']], 'toe': coords[geo['tip']],
                   'hock': Vector(bones[f'foot.{side}'][0])}
        record['paws'][side] = dict(path, phase=(phase+offset)%1,
            lowest_z=min(coords[i].z for i in geo['toes']),
            markers={name: list(p) for name, p in markers.items()},
            pixels={name: project(scene, p) for name, p in markers.items()})
        if path['stance'] and path['toe_pitch'] == 0:
            patch = [coords[i] for i in geo['toes'] if obj.data.vertices[i].co.z < .035]
            record['paws'][side]['patch_ground_space'] = [
                [p.x, p.y-motion.STRIDE*phase, p.z] for p in patch]
    evaluated.to_mesh_clear()
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--study', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--no-render', action='store_true')
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--keep-study-weights', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    expected = json.loads(args.manifest.read_text())['foot-study.blend']
    if digest(args.study) != expected:
        raise ValueError('Source differs from the saved foot study')
    args.out.mkdir(parents=True, exist_ok=False)
    bpy.ops.wm.open_mainfile(filepath=str(args.study))
    bpy.context.preferences.filepaths.save_version = 0
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    retained = rig.animation_data.action
    retained.use_fake_user = True
    retained_name = retained.name
    rig.animation_data.action = None
    reset(rig)
    before = state(obj, rig)
    if len(rig.data.bones) != 22:
        raise ValueError('Run 03 requires the foot study toe rig')
    region = [] if args.keep_study_weights else repair_weights(obj, before)
    revised = state(obj, rig)
    for field in ('vertices', 'faces', 'bones', 'uv_sha256', 'images'):
        if before[field] != revised[field]:
            raise ValueError(f'Run skinning repair changed {field}')
    changed = [i for i,(a,b) in enumerate(zip(before['weights'],revised['weights'])) if a != b]
    if not set(changed) <= set(region):
        raise ValueError('Skinning edits escaped the explicit lower-leg region')
    region_set = set(region)
    outside = [hashlib.sha256(json.dumps([w for i,w in enumerate(snapshot['weights']) if i not in region_set],
                                        sort_keys=True).encode()).hexdigest() for snapshot in (before,revised)]
    edits = {'region_vertex_indices': region, 'changed_weight_indices': changed,
             'outside_region_weight_sha256_before_after': outside,
             'geometry_rest_rig_uv_texture_unchanged': True,
             'uv_sha256': revised['uv_sha256'], 'images': revised['images'],
             'region_bounds': [[min(before['vertices'][i][a] for i in region) for a in range(3)],
                               [max(before['vertices'][i][a] for i in region) for a in range(3)]] if region else [],
             'weights': [{'index': i, 'co': before['vertices'][i], 'before': before['weights'][i],
                          'after': revised['weights'][i]} for i in changed]}
    (args.out/'weight-edits.json').write_text(json.dumps(edits, indent=2)+'\n')
    geometry = foot_geometry(obj, rig)
    for geo in geometry.values():
        geo['pivot'] = min(geo['toes'], key=lambda i: (Quaternion((1, 0, 0), .42) @ obj.data.vertices[i].co).z)
        geo['tip'] = min(geo['toes'], key=lambda i: obj.data.vertices[i].co.y)
    rig.animation_data.action = bpy.data.actions.new(motion.ACTION)
    for bone in rig.pose.bones:
        bone.rotation_mode = 'QUATERNION'
    previous = {}
    for index in range(193):
        phase = index/192
        pose(obj, rig, geometry, phase)
        for bone in rig.pose.bones:
            if bone.name in previous and bone.rotation_quaternion.dot(previous[bone.name]) < 0:
                bone.rotation_quaternion.negate()
            previous[bone.name] = bone.rotation_quaternion.copy()
            bone.keyframe_insert('location', frame=1+24*phase, group=bone.name)
            bone.keyframe_insert('rotation_quaternion', frame=1+24*phase, group=bone.name)
    for curve in rig.animation_data.action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
        curve.modifiers.new('CYCLES')
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = 1, 24
    scene.render.fps, scene.render.fps_base = 45, 1
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.film_transparent = True
    scene.eevee.taa_render_samples = 24 if args.preview else 64
    for marker in list(scene.timeline_markers):
        scene.timeline_markers.remove(marker)
    for offset, side in ((0, 'Near'), (12, 'Far')):
        for frame, name in ((1, 'toe landing'), (4, 'compression'), (8, 'toe push-off'), (11, 'folded recovery')):
            scene.timeline_markers.new(f'{side} {name}', frame=frame+offset)
    scene.timeline_markers.new('Loop closure (omitted from exports)', frame=25)
    camera(scene, 'game')
    scene.frame_set(1)
    authored = state(obj, rig)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'run-mouse.blend'))
    bpy.ops.wm.open_mainfile(filepath=str(args.out/'run-mouse.blend'))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    if state(obj, rig) != authored:
        raise ValueError('Saved copy differs from authored mesh or rig')
    diag = diagnostics(obj, rig, geometry)
    checks = []
    count = 48 if args.preview else 384
    for index in range(count+1):
        frame = 1+24*index/count
        scene.frame_set(int(frame), subframe=frame-int(frame))
        checks.append(measure(scene, obj, rig, geometry, index/count, diag))
    side_pixels, side_paw_pixels = {}, {}
    for view in ('game', 'side'):
        camera(scene, view)
        if not args.no_render:
            (args.out/f'frames-{view}').mkdir()
        for index in range(24):
            scene.frame_set(index+1)
            if view == 'side':
                side_pixels[index] = {b.name: [project(scene, b.head), project(scene, b.tail)] for b in rig.pose.bones}
                side_paw_pixels[index] = {side: {name: project(scene, Vector(p)) for name,p in paw['markers'].items()}
                                          for side,paw in checks[index*count//24]['paws'].items()}
            if not args.no_render:
                render(scene, args.out/f'frames-{view}'/f'run-{index:02}.png')
    camera(scene, 'game')
    report = {'status': 'awaiting_user_run_verdict', 'revision': 3, 'action': motion.ACTION,
              'source_sha256': expected, 'source_action_retained': retained_name,
              'saved_copy_verified': True, 'samples': 24, 'fps': 45,
              'cycle_seconds': motion.CYCLE_SECONDS, 'stance_end': motion.STANCE_END,
              'stride': motion.STRIDE, 'virtual_forward_speed': motion.SPEED,
              'ground_z': 0, 'forward_axis': [0, -1, 0], 'contact_marker': 'pad',
              'camera': {'location': list(scene.camera.location), 'target': [.17, -.3, 2.13],
                         'ortho_scale': 5.2, 'ground_row_512': project(scene, Vector((0, 0, 0)))[1],
                         'near_side': 'left', 'resolution': 512},
              'rest_bones': authored['bones'], 'before_equals_after': before == authored,
              'max_weight_sum_error': max(abs(sum(w.values())-1) for w in authored['weights']),
              'side_pixels': side_pixels, 'side_paw_pixels': side_paw_pixels, 'checks': checks}
    (args.out/'motion.json').write_text(json.dumps(report, indent=2)+'\n')
    for name in ('animate_run_v3.py', 'run_motion_v3.py', 'foot_study.py', 'animate_run.py',
                 'run_motion.py', 'inspect_run_master.py'):
        shutil.copyfile(Path(__file__).with_name(name), args.out/name)
    if digest(args.study) != expected:
        raise ValueError('Source study changed during authoring')
    print(json.dumps({'out': str(args.out), 'checks': len(checks),
                      'lowest_z': min(c['lowest_z'] for c in checks),
                      'max_edge_ratio': max(c['edge_ratio']['max'] for c in checks)}), flush=True)


if __name__ == '__main__':
    main()
