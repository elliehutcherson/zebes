"""Read third-party rig/action data without enabling embedded Blender scripts."""

import json
from pathlib import Path
import sys

import bpy

source, out = map(Path, sys.argv[sys.argv.index('--')+1:])
bpy.ops.wm.open_mainfile(filepath=str(source), use_scripts=False)
scene = bpy.context.scene
report = {
    'source': str(source), 'fps': scene.render.fps / scene.render.fps_base,
    'scene_frames': [scene.frame_start, scene.frame_end],
    'embedded_texts': [text.name for text in bpy.data.texts],
    'objects': [{'name': o.name, 'type': o.type, 'location': list(o.location),
                 'scale': list(o.scale), 'hidden': o.hide_render,
                 'action': o.animation_data.action.name if o.animation_data and o.animation_data.action else None}
                for o in scene.objects],
    'actions': {a.name: {'range': list(a.frame_range),
                         'curves': len(a.fcurves),
                         'keyed_channels': sorted({f.data_path for f in a.fcurves}),
                         'key_times': sorted({p.co.x for f in a.fcurves for p in f.keyframe_points})}
                for a in bpy.data.actions},
    'rigs': {},
}
for obj in scene.objects:
    if obj.type != 'ARMATURE':
        continue
    report['rigs'][obj.name] = {b.name: {
        'head': list(b.head_local), 'tail': list(b.tail_local), 'length': b.length,
        'parent': b.parent.name if b.parent else None, 'deform': b.use_deform,
        'constraints': [{'name': c.name, 'type': c.type,
                          'target': c.target.name if hasattr(c, 'target') and c.target else None,
                          'subtarget': getattr(c, 'subtarget', None),
                          'chain_count': getattr(c, 'chain_count', None)}
                         for c in obj.pose.bones[b.name].constraints]}
        for b in obj.data.bones}
out.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report), flush=True)
