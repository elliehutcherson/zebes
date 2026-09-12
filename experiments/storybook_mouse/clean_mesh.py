"""Clean this inspected shape while retaining raw geometry separately."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import trimesh


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        raise ValueError('Choose a new output directory to preserve prior geometry')
    original = trimesh.load(args.input, force='mesh')
    components = sorted(original.split(only_watertight=False), key=lambda part: -len(part.faces))
    result = components[0]
    removed = sum(len(part.faces) for part in components[1:])
    if removed/len(original.faces) > 0.005 or not np.allclose(result.bounds, original.bounds, atol=1e-6):
        raise ValueError('Discarded components exceed the inspected small-fragment case')
    before = len(result.faces)
    result.update_faces(result.nondegenerate_faces(height=1e-8))
    result.update_faces(result.unique_faces())
    result.remove_unreferenced_vertices()
    trimesh.repair.fix_normals(result)
    before_fill = len(result.faces)
    trimesh.repair.fill_holes(result)
    args.out.mkdir(parents=True)
    result.export(args.out/'clean.glb')
    result.export(args.out/'clean.ply')
    report = {'raw_sha256': hashlib.sha256(args.input.read_bytes()).hexdigest(),
              'raw_faces': len(original.faces), 'raw_components': len(components),
              'removed_fragment_faces': removed, 'removed_degenerate_or_duplicate_faces': before-before_fill,
              'filled_triangle_quad_faces': len(result.faces)-before_fill,
              'faces': len(result.faces), 'vertices': len(result.vertices),
              'watertight': bool(result.is_watertight), 'bounds': result.bounds.tolist(),
              'reason': 'Inspected main component contains the whole character. Small components include a thigh surface fragment and zero-area debris.'}
    (args.out/'cleanup.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
