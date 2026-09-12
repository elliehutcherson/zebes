"""Inspect the accepted saved scene; preserve it before authoring any motion."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector


MASTER_SHA256 = '32f9bb3c460bc09086cac0afcfb29e26941e2bea5bb8e726459e6caa223672c9'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--master', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if hashlib.sha256(args.master.read_bytes()).hexdigest() != MASTER_SHA256:
        raise ValueError('The source is not the accepted Blender master')
    args.out.mkdir(parents=True, exist_ok=False)
    bpy.ops.wm.open_mainfile(filepath=str(args.master))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out / 'master-loaded.blend'))
    scene = bpy.context.scene
    obj = bpy.data.objects['Mouse_Neutral']
    rig = bpy.data.objects['Mouse_Study_Rig']
    report = {
        'master_sha256': MASTER_SHA256,
        'loaded_action': rig.animation_data.action.name,
        'objects': [(o.name, o.type) for o in scene.objects],
        'mesh_matrix': [list(row) for row in obj.matrix_world],
        'rig_matrix': [list(row) for row in rig.matrix_world],
        'bones': {b.name: {'head': list(b.head_local), 'tail': list(b.tail_local),
                           'length': b.length, 'parent': b.parent.name if b.parent else None,
                           'connected': b.use_connect} for b in rig.data.bones},
        'paws': {},
    }
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    for side in ('left', 'right'):
        group = obj.vertex_groups[f'foot.{side}'].index
        vertices = [v for v in obj.data.vertices if any(g.group == group and g.weight > .999 for g in v.groups)]
        sole = min(vertices, key=lambda v: v.co.z)
        # Retain actual sole geometry for rotation-dependent support queries.
        report['paws'][side] = {
            'rigid_vertices': len(vertices),
            'bounds': [[min(v.co[i] for v in vertices) for i in range(3)],
                       [max(v.co[i] for v in vertices) for i in range(3)]],
            'lowest_vertex': sole.index, 'sole': list(sole.co),
            'heel': list(min((v for v in vertices if v.co.z < .10), key=lambda v: -v.co.y).co),
            'toe': list(min((v for v in vertices if v.co.z < .10), key=lambda v: v.co.y).co),
        }
    scene.camera.location = (-10, -7, 2.13)
    target = Vector((.17, -.3, 2.13))
    scene.camera.rotation_euler = (target - scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 5.2
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.filepath = str(args.out / 'neutral-game-camera.png')
    bpy.ops.render.render(write_still=True)
    (args.out / 'inspection.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
