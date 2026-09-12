"""Sample and render the downloaded raptor's existing run without editing it."""

import json
from pathlib import Path
import sys

import bpy
from mathutils import Vector

source, out = map(Path, sys.argv[sys.argv.index('--')+1:])
out.mkdir(parents=True, exist_ok=False)
bpy.ops.wm.open_mainfile(filepath=str(source), use_scripts=False)
scene = bpy.context.scene
rig = bpy.data.objects['raptor_skeleton']
action = bpy.data.actions['run']
rig.animation_data.action = action
for track in rig.animation_data.nla_tracks:
    track.mute = True
obj = bpy.data.objects['raptor_scaly']
for item in scene.objects:
    if item.type == 'MESH':
        item.hide_render = item != obj
obj.hide_set(False)
records = []
for frame in range(19):
    scene.frame_set(frame)
    records.append({'frame': frame,
                    'bones': {b.name: [list(rig.matrix_world @ b.head), list(rig.matrix_world @ b.tail)]
                              for b in rig.pose.bones},
                    'rotations': {b.name: list(b.rotation_quaternion) for b in rig.pose.bones}})
report = {'action': action.name, 'fps': scene.render.fps/scene.render.fps_base,
          'samples': records,
          'keys_after_16': {f.data_path: [list(k.co) for k in f.keyframe_points if k.co.x > 16]
                            for f in action.fcurves if any(k.co.x > 16 for k in f.keyframe_points)}}
(out/'sampled-run.json').write_text(json.dumps(report, indent=2)+'\n')
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'SINGLE'
scene.display.shading.single_color = (.44, .66, .54)
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.render.film_transparent = True
scene.render.resolution_x = 800
scene.render.resolution_y = 400
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
camera = scene.camera
camera.location = (5, .15, .43)
target = Vector((0, .15, .43))
camera.rotation_euler = (target-camera.location).to_track_quat('-Z', 'Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 3.2
for frame in range(16):
    scene.frame_set(frame)
    scene.render.filepath = str(out/f'run-{frame:02}.png')
    bpy.ops.render.render(write_still=True)
print(json.dumps({'status': 'sampled', 'out': str(out)}), flush=True)
