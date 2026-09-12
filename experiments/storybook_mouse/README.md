# Storybook mouse tools

The [3D mouse plan](../../docs/mouse-3d-plan.md) owns current decisions and
remaining work. The [asset index](../mouse_3d/README.md) links the accepted
master, foot study, and all three runs. Animation is paused after run 03's
rejection; reproduce or revise only in a new output directory.

## Implementation

| Files | Responsibility |
|---|---|
| `scripts/run_storybook_shape.py` | Explicit download, foreground preparation, and bounded Hunyuan shape request |
| `clean_mesh.py`, `inspect_mesh.py` | Local mesh cleanup, normalization, Blender inspection/export |
| `study_rig.py`, `study_material.py` | Neutral rig and baked authored material |
| `inspect_run_master.py`, `audit_forefoot.py` | Accepted-master validation and rigid toe-pivot measurements |
| `run_motion{,_v2,_v3}.py` | Separate retained motion definitions for runs 01–03 |
| `animate_run.py`, `finalize_run.py` | Run 01/02 authoring and saved-file verification |
| `foot_study.py`, `animate_run_v3.py` | Local foot articulation, run 03, and deformation diagnostics |
| `scripts/review_storybook_*.py` | Fixed-camera review packaging |

Blender tools operate on local geometry and saved scenes. They have no engine
dependencies, CMake targets, or production imports. Provider work stays under
`scripts/`. Source snapshots inside asset bundles reproduce their exact result;
live implementations above are the source for future changes.

## Findings

- Hunyuan produced one reusable mesh. Cleanup retained the main component,
  removed debris/degeneracy, and filled small holes; reports record exact counts.
- Initial CPU-offload setup failed before sampling because the upstream helper
  expected a missing `components` map. The runner supplies the three modules
  and uses the hooks' CUDA execution device; the successful trial ran once.
- Single-view color projection misaligned features. Authored color fields baked
  into a UV texture supplied the accepted appearance.
- A first bake was empty because the distribution Blender lacked its denoiser.
  Emission baking disables denoising and explicitly checks nonempty pixel data.
- Rigid toe contact did not establish convincing articulation. The foot study
  adds local geometry/weights and independent toe bones; run 03 retains that work.
- Blender volume-preserving deformation may differ from GLB viewer skinning.
  Blender's retained renders are the offline sprite reference.

Exact input/model revisions, raw geometry, failed attempts, settings, and source
copies remain in the [neutral bundle](../mouse_3d/storybook-neutral-v1/README.md).
Animation measurements and commands remain in each run/study README.

## Runtime and reproduction

Derry has Blender 4.0.2 and the RTX 3090. Eevee needs `DISPLAY=:0`; the
distribution Blender also needs NumPy on its Python path for GLB export.
Use the recorded runtime invocation in the relevant bundle README. Keep the
working ComfyUI installation intact.

Opening or animating the packed master requires neither Hunyuan nor its
temporary workspace. To reproduce the original shape trial only, use the
isolated dependencies in `requirements-shape.txt`, upstream checkout and pinned
weights recorded in the neutral bundle. The original external workspace was
`/tmp/zebes-storybook-20260912` on Derry. Keep `HF_HOME` and `U2NET_HOME`
inside the isolated work directory.

```bash
python scripts/run_storybook_shape.py --repo /path/to/Hunyuan3D-2 prepare \
  --input input-clean.png --out prepared
python scripts/run_storybook_shape.py --repo /path/to/Hunyuan3D-2 download \
  --revision 9cd649ba6913f7a852e3286bad86bfa9a2d83dcf --cache hf-cache --out weights
python scripts/run_storybook_shape.py --repo /path/to/Hunyuan3D-2 generate \
  --input prepared --weights weights --out raw-shape
python experiments/storybook_mouse/clean_mesh.py --input raw-shape/raw.ply --out cleanup
blender --background --python-use-system-env --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/inspect_mesh.py -- \
  --mesh cleanup/clean.glb --input prepared --out result --with-rig
python scripts/review_storybook_neutral.py /path/to/study-root
```

## Verification

```bash
build/tileset-venv/bin/python -m unittest tests.storybook_shape_test \
  tests.storybook_neutral_asset_test tests.storybook_foot_study_test \
  tests.storybook_run_test tests.storybook_run_v2_test tests.storybook_run_v3_test \
  tests.storybook_review_test
git diff --check
```

These checks cover inputs, preservation, local edits, saved-scene measurements,
contacts, bones, loop closure, and decoded exports. They do not establish
artistic acceptance.
