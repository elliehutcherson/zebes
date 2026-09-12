"""Measure rigid toe-pad support and joint placement on the accepted master."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
from mathutils import Quaternion

sys.path.insert(0, str(Path(__file__).resolve().parent))
from animate_run import fingerprint, paw_geometry
from inspect_run_master import MASTER_SHA256


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--master', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if hashlib.sha256(args.master.read_bytes()).hexdigest() != MASTER_SHA256:
        raise ValueError('Source is not the accepted master')
    args.out.mkdir(parents=True, exist_ok=False)
    bpy.ops.wm.open_mainfile(filepath=str(args.master))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'master-loaded.blend'))
    obj, rig = bpy.data.objects['Mouse_Neutral'], bpy.data.objects['Mouse_Study_Rig']
    report = {'master_sha256': MASTER_SHA256, 'fingerprint': fingerprint(obj, rig), 'paws': {}}
    for side, geo in paw_geometry(obj, rig).items():
        envelope = []
        for index in range(101):
            pitch = .25+index*.01
            rotation = Quaternion((1, 0, 0), pitch)
            support = min(geo['vertices'], key=lambda v: (rotation @ v.co).z)
            envelope.append({'pitch': pitch, 'vertex': support.index, 'position': list(support.co)})
        report['paws'][side] = {
            'rigid_vertices': len(geo['vertices']), 'support_envelope': envelope,
            'joints': {name: list(rig.data.bones[f'{name}.{side}'].head_local)
                       for name in ('thigh', 'shin', 'foot')},
            'heel': list(obj.data.vertices[geo['markers']['heel']].co),
            'toe': list(obj.data.vertices[geo['markers']['toe']].co)}
    (args.out/'forefoot-audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
