"""Attach inspectable contact markers, then reopen and verify the saved run."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rig_support import fingerprint
from inspect_run_master import MASTER_SHA256


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    out = args.result
    blend = out/'run-mouse.blend'
    if (out/'saved-scene-check.json').exists():
        raise ValueError('The saved scene is already finalized')
    if hashlib.sha256(blend.read_bytes()).hexdigest() == MASTER_SHA256:
        raise ValueError('Finalize only an authored run, never the accepted master')
    report = json.loads((out/'motion.json').read_text())
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    if rig.animation_data.action.name != report['action'] or fingerprint(obj, rig) != report['after']:
        raise ValueError('The saved file does not match the authored run')
    for marker in list(scene.timeline_markers):
        if marker.name in ('Neutral', 'Reach', 'Step'):
            scene.timeline_markers.remove(marker)
    collection = bpy.data.collections.new('Run contact diagnostics')
    scene.collection.children.link(collection)
    for side, markers in report['paw_markers'].items():
        rest = rig.data.bones[f'foot.{side}']
        for name, vertex in markers.items():
            marker = bpy.data.objects.new(f'{name.title()}.{side}', None)
            collection.objects.link(marker)
            marker.empty_display_type = 'SPHERE'
            marker.empty_display_size = .025
            marker.show_in_front = True
            marker.parent = rig
            marker.parent_type = 'BONE'
            marker.parent_bone = rest.name
            marker.matrix_parent_inverse = Matrix.Identity(4)
            # Blender bone-object parenting is relative to the bone tail.
            marker.location = rest.matrix_local.inverted() @ obj.data.vertices[vertex].co - Vector((0, rest.length, 0))
            marker['mesh_vertex'] = vertex
            marker.hide_render = True
    origin = bpy.data.objects.new('Common_ground_origin', None)
    collection.objects.link(origin)
    origin.location = (.17, -.3, 0)
    origin.empty_display_type = 'CIRCLE'
    origin.empty_display_size = .3
    origin.hide_render = True
    scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    scene = bpy.context.scene
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    if fingerprint(obj, rig) != report['before']:
        raise ValueError('Saved/reloaded run changed the accepted source data')
    max_weight_error = 0
    for vertex in obj.data.vertices:
        weights = [g.weight for g in vertex.groups if g.weight > 0]
        if not weights or len(weights) > 4 or not all(math.isfinite(w) and w >= 0 for w in weights):
            raise ValueError(f'Invalid weights on vertex {vertex.index}')
        max_weight_error = max(max_weight_error, abs(sum(weights)-1))
    max_joint_error = max_marker_error = 0
    max_scale_error = max_head_shape_error = 0
    head_rotation = None
    for check in report['checks']:
        frame = check['frame']
        scene.frame_set(int(frame), subframe=frame-int(frame))
        evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.to_mesh()
        for name, (head, tail) in check['bones'].items():
            bone = rig.pose.bones[name]
            max_joint_error = max(max_joint_error, math.dist(bone.head, head), math.dist(bone.tail, tail))
            max_scale_error = max(max_scale_error, max(abs(v-1) for v in bone.matrix.to_scale()))
        for side, markers in report['paw_markers'].items():
            for name, vertex in markers.items():
                actual = bpy.data.objects[f'{name.title()}.{side}'].matrix_world.translation
                expected = evaluated.matrix_world @ mesh.vertices[vertex].co
                max_marker_error = max(max_marker_error, math.dist(actual, expected))
        rotation = rig.pose.bones['head'].matrix.to_quaternion()
        if head_rotation is None:
            head_rotation = rotation
        max_head_shape_error = max(max_head_shape_error, min((rotation-head_rotation).magnitude,
                                                            (rotation+head_rotation).magnitude))
        evaluated.to_mesh_clear()
    result = {'reopened_saved_blend': True, 'source_data_unchanged': True,
              'samples_checked': len(report['checks']), 'max_joint_reload_error': max_joint_error,
              'max_contact_marker_error': max_marker_error, 'max_weight_sum_error': max_weight_error,
              'max_bone_scale_error': max_scale_error, 'max_head_rotation_difference': max_head_shape_error,
              'contact_markers_parented_to_foot_bones': True,
              'blend_sha256': hashlib.sha256(blend.read_bytes()).hexdigest()}
    if max(max_joint_error, max_marker_error, max_weight_error, max_scale_error, max_head_shape_error) > 2e-5:
        raise ValueError(f'Saved-scene invariant failed: {result}')
    (out/'saved-scene-check.json').write_text(json.dumps(result, indent=2)+'\n')
    for name in ('finalize_run.py', 'rig_support.py'):
        shutil.copyfile(Path(__file__).with_name(name), out/name)
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
