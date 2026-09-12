"""Author a run by loading the accepted Blender master into a new output copy.

No reconstruction, rebinding, material bake or mesh edits. Analytic two-bone
solutions use measured rest lengths; all keys are ordinary editable FK keys.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_run_master import MASTER_SHA256
from rig_support import (fingerprint, inherited_head, paw_geometry, point_bone,
                         project, reset, rotated_bone, two_bone_joint, update)
import run_motion

SAMPLES = 24


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pose(rig, geometry, phase, motion_source=run_motion):
    reset(rig)
    motion = motion_source.body(phase)
    root = rig.data.bones['root']
    rotated_bone(rig, 'root', root.head_local + Vector((0, 0, motion['bob'])), Quaternion((1, 0, 0), .055))
    rotated_bone(rig, 'spine', inherited_head(rig, 'spine'), Quaternion((1, 0, 0), motion['lean']))
    # Preserve the head as one rigid shape, with a restrained constant forward tilt.
    rotated_bone(rig, 'head', inherited_head(rig, 'head'), Quaternion((1, 0, 0), .025))
    contacts = {}
    for side, offset, sign in (('left', 0., -1), ('right', .5, 1)):
        local_phase = (phase+offset) % 1
        path = motion_source.paw(local_phase)
        geo = geometry[side]
        rotation = Quaternion((1, 0, 0), path['pitch'])
        lowest = min((rotation @ (v.co-geo['ankle'])).z for v in geo['vertices'])
        ankle = Vector((geo['ankle'].x, path['y'], path['clearance']-lowest))
        if 'pad' in geo:
            pad_offset = rotation @ (geo['pad']-geo['ankle'])
            ankle.y -= pad_offset.y
        hip = inherited_head(rig, f'thigh.{side}')
        knee = two_bone_joint(hip, ankle, rig.data.bones[f'thigh.{side}'].length,
                              rig.data.bones[f'shin.{side}'].length, Vector((sign*.12, -1, 0)))
        point_bone(rig, f'thigh.{side}', hip, knee-hip)
        point_bone(rig, f'shin.{side}', knee, ankle-knee)
        rotated_bone(rig, f'foot.{side}', ankle, rotation)
        # Arm swing opposes the same-side leg. Fixed elbow bend keeps recovery compact.
        swing = motion_source.body(local_phase)['arm_swing']
        shoulder = inherited_head(rig, f'upper_arm.{side}')
        upper = Vector((sign*.18, -math.sin(swing), -math.cos(swing))).normalized()
        point_bone(rig, f'upper_arm.{side}', shoulder, upper)
        elbow = rig.pose.bones[f'upper_arm.{side}'].tail.copy()
        lower = Vector((sign*.08, -math.sin(swing+1.50), -math.cos(swing+1.50))).normalized()
        point_bone(rig, f'forearm.{side}', elbow, lower)
        point_bone(rig, f'hand.{side}', rig.pose.bones[f'forearm.{side}'].tail.copy(), lower)
        contacts[side] = dict(path, phase=local_phase, target_ankle=list(ankle))
    for index in range(4):
        bone = rig.pose.bones[f'tail.{index}']
        bone.rotation_quaternion = (Quaternion((1, 0, 0), .055*math.sin(2*math.pi*phase-index*.55))
                                    @ Quaternion((0, 0, 1), .035*math.sin(2*math.pi*phase-index*.45)))
    update()
    return contacts


def audit(scene, obj, rig, geometry, phase, contacts):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    coordinates = [evaluated.matrix_world @ v.co for v in mesh.vertices]
    record = {'phase': phase, 'frame': 1+phase*SAMPLES,
              'lowest_z': min(v.z for v in coordinates),
              'bones': {b.name: [list(b.head), list(b.tail)] for b in rig.pose.bones},
              'rig_pixels': {b.name: [project(scene, b.head), project(scene, b.tail)] for b in rig.pose.bones},
              'paws': {}}
    for side, geo in geometry.items():
        markers = {name: coordinates[index] for name, index in geo['markers'].items()}
        record['paws'][side] = dict(contacts[side],
            lowest_z=min(coordinates[v.index].z for v in geo['vertices']),
            markers={name: list(p) for name, p in markers.items()},
            pixels={name: project(scene, p) for name, p in markers.items()})
    evaluated.to_mesh_clear()
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--master', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--revision', type=int, choices=(1, 2), default=1)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    motion_source = run_motion
    if args.revision == 2:
        import run_motion_v2
        motion_source = run_motion_v2
    action_name = getattr(motion_source, 'ACTION', 'Storybook_Run')
    fps = round(SAMPLES/motion_source.CYCLE_SECONDS)
    if digest(args.master) != MASTER_SHA256:
        raise ValueError('Source hash differs from accepted master')
    args.out.mkdir(parents=True, exist_ok=False)
    bpy.ops.wm.open_mainfile(filepath=str(args.master))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'run-mouse.blend'))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    before = fingerprint(obj, rig)
    original_action = rig.animation_data.action
    original_action.use_fake_user = True
    rig.animation_data.action = bpy.data.actions.new(action_name)
    for bone in rig.pose.bones:
        bone.rotation_mode = 'QUATERNION'
    geometry = paw_geometry(obj, rig, getattr(motion_source, 'SUPPORT_PITCH', None))
    scene.camera.location = (-10, -7, 2.13)
    target = Vector((.17, -.3, 2.13))
    scene.camera.rotation_euler = (target-scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 5.2
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.fps = fps
    scene.render.fps_base = 1
    scene.frame_start, scene.frame_end = 1, SAMPLES
    for frame, name in ((1, 'Near contact'), (4, 'Near compression'), (9, 'Near release'),
                        (11, 'Flight'), (13, 'Far contact'), (16, 'Far compression'),
                        (21, 'Far release'), (23, 'Flight'), (25, 'Loop closure (not exported)')):
        scene.timeline_markers.new(name, frame=frame)
    if args.revision == 2:
        for marker in list(scene.timeline_markers):
            scene.timeline_markers.remove(marker)
        for offset, side in ((0, 'Near'), (12, 'Far')):
            for frame, name in ((1, 'toe-pad contact'), (3, 'heel compression'),
                                (7, 'toe push-off'), (8, 'release'), (11, 'flight')):
                scene.timeline_markers.new(f'{side} {name}', frame=frame+offset)
        scene.timeline_markers.new('Loop closure (not exported)', frame=25)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.film_transparent = True
    scene.eevee.taa_render_samples = 64
    previous = {}
    for index in range(SAMPLES*4+1):
        phase = index/(SAMPLES*4)
        pose(rig, geometry, phase, motion_source)
        for bone in rig.pose.bones:
            quat = bone.rotation_quaternion.copy()
            if bone.name in previous and quat.dot(previous[bone.name]) < 0:
                quat.negate()
                bone.rotation_quaternion = quat
            previous[bone.name] = quat
            bone.keyframe_insert('location', frame=1+phase*SAMPLES, group=bone.name)
            bone.keyframe_insert('rotation_quaternion', frame=1+phase*SAMPLES, group=bone.name)
    for curve in rig.animation_data.action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
        curve.modifiers.new('CYCLES')
    after = fingerprint(obj, rig)
    if after != before:
        raise ValueError('Run authoring changed accepted mesh, UV, weights, rest rig or packed texture')
    records = []
    # Evaluate the saved action, not the authoring function, including half-key samples.
    for index in range(SAMPLES*8+1):
        phase = index/(SAMPLES*8)
        frame = 1+phase*SAMPLES
        scene.frame_set(int(frame), subframe=frame-int(frame))
        contacts = {side: dict(motion_source.paw((phase+offset)%1), phase=(phase+offset)%1)
                    for side, offset in (('left', 0), ('right', .5))}
        records.append(audit(scene, obj, rig, geometry, phase, contacts))
    report = {'master_sha256': MASTER_SHA256, 'before': before, 'after': after,
              'action': action_name, 'revision': args.revision, 'original_action_retained': original_action.name,
              'samples': SAMPLES, 'fps': fps, 'cycle_seconds': motion_source.CYCLE_SECONDS,
              'stance_end': motion_source.STANCE_END, 'stride': motion_source.STRIDE,
              'virtual_forward_speed': motion_source.SPEED,
              'contact_marker': 'pad' if args.revision == 2 else 'sole',
              'forward_axis': [0, -1, 0], 'ground_z': 0,
              'contact_tolerance_units': .001,
              'camera': {'location': list(scene.camera.location), 'target': list(target),
                         'ortho_scale': 5.2, 'ground_row_512': project(scene, Vector((0, 0, 0)))[1],
                         'near_side': 'left', 'resolution': 512},
              'paw_markers': {side: geo['markers'] for side, geo in geometry.items()},
              'rest_bones': {b.name: {'length': b.length, 'head': list(b.head_local),
                                     'tail': list(b.tail_local), 'parent': b.parent.name if b.parent else None}
                             for b in rig.data.bones}, 'checks': records}
    (args.out/'motion.json').write_text(json.dumps(report, indent=2)+'\n')
    scene.frame_set(1)
    scene['run_contract'] = json.dumps({k: report[k] for k in ('cycle_seconds', 'virtual_forward_speed', 'ground_z', 'camera')})
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'run-mouse.blend'))
    frames = [1, 4, 7, 10, 13, 16, 19, 22] if args.preview else range(1, SAMPLES+1)
    (args.out/'frames-512').mkdir()
    for frame in frames:
        scene.frame_set(frame)
        scene.render.filepath = str(args.out/'frames-512'/f'run-{frame-1:02}.png')
        bpy.ops.render.render(write_still=True)
    for source in (Path(__file__), Path(__file__).with_name('run_motion.py'),
                   Path(__file__).with_name('rig_support.py'),
                   Path(motion_source.__file__), Path(__file__).with_name('inspect_run_master.py')):
        shutil.copyfile(source, args.out/source.name)
    if digest(args.master) != MASTER_SHA256:
        raise ValueError('Accepted master was modified')
    print(json.dumps({'status': 'complete', 'out': str(args.out), 'source_preserved': True}), flush=True)


if __name__ == '__main__':
    main()
