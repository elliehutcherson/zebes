# Neutral storybook mouse implementation

**The user accepted this model and workflow as a success on 2026-09-12.**
This neutral character is the foundation for the next running animation.
The accepted mesh, texture and saved Blender scene must remain unchanged;
animation work starts from a versioned working copy.

[Review and deliverables](../mouse_3d/storybook-neutral-v1/README.md) ·
[Plan of record](../../docs/mouse-3d-plan.md) ·
[Acceptance and exact hashes](../mouse_3d/storybook-neutral-v1/acceptance.json)

## Source and boundaries

- `scripts/run_storybook_shape.py` owns the explicit model download, foreground
  preparation and one bounded Hunyuan3D shape request. Provider calls remain
  outside this self-contained experiment.
- `clean_mesh.py` processes retained local geometry only.
- `inspect_mesh.py` imports local geometry into Blender, normalizes it, reduces
  the review mesh, renders views and exports the study.
- `study_rig.py` defines measured/authored landmarks and a temporary body rig.
- `study_material.py` bakes authored 3D color fields into a 2048px UV texture.
- `scripts/review_storybook_neutral.py` packages the retained PNGs and pose sheets.

There are no engine dependencies, CMake targets, production imports or runtime
format changes. Earlier mouse assets remain intact.

## Shape trial

The user-approved storybook concept supplies the design. Two built-in imagegen
requests made a single neutral character reference: the first returned a baked
checkerboard instead of alpha, and a background-only edit replaced it with
white. Both raw files and exact prompts are retained. Hunyuan's standard rembg
preprocessing then produced an inspected RGBA foreground and its exact 512px
conditioning image/mask, with 15% border. The input files are hash-checked before
inference, and the pipeline's actual preprocessing must equal the inspected image.

The successful trial used:

- Code: `Tencent-Hunyuan/Hunyuan3D-2`, commit
  `f8db63096c8282cb27354314d896feba5ba6ff8a`.
- Model: `tencent/Hunyuan3D-2`, subfolder `hunyuan3d-dit-v2-0`, commit
  `9cd649ba6913f7a852e3286bad86bfa9a2d83dcf`.
- FP16 safetensors, seed 42, 50 steps, guidance 5.5, 384-cell extraction,
  8,000-query chunks, marching cubes, CPU offload.
- Derry's RTX 3090; about 80.44 seconds including loading, sampling,
  extraction and export. Peak PyTorch allocation was about 2.46GiB for this
  process; this excludes the other process already occupying GPU memory.

The initial attempt failed during model initialization because this upstream
pipeline's copied offload helper expects a missing `components` mapping. The
runner explicitly supplies its three model modules and uses the hooks' CUDA
execution device. That failed attempt did not begin sampling. One complete
shape-sampling request ran; no seed search or further shape generation followed.

The external checkout, weights, dependencies and cache live under
`/tmp/zebes-storybook-20260912` on Derry. The working ComfyUI installation was
not changed. `requirements-shape.txt` records the isolated additions, used with
its existing torch/torchvision runtime. Official code/model license information
is available from the [upstream repository](https://github.com/Tencent-Hunyuan/Hunyuan3D-2).

## Geometry and material decisions

Raw output: 257,754 vertices, 515,508 faces and 15 components. The main component
contains the whole character. Inspection located a small thigh-surface fragment
and tiny/zero-area debris; removing the smaller components preserves the full
character bounds. Cleanup removes 1,232 fragment faces and two duplicate or
degenerate faces, then fills two small triangle/quad faces. The resulting
514,276-face main surface is watertight. This is a topology property, not proof
of good anatomy or production-ready edge flow.

Blender reduces the review mesh to 80,000 triangles. One global normalization
sets its size; a post-reduction global translation corrects a 0.000185-unit
ground offset. Individual paws are not repositioned to hide attachment errors.

Single-view color projection was tried and rejected for poor feature alignment.
Authored color fields were then aligned to the measured frontal geometry. Their
vertex-color version had coarse boundaries on the reduced mesh, so the final
appearance is baked into a UV texture. The first bake silently returned black
because the distribution Blender lacks its default denoiser. Denoising is now
disabled for the emission bake, and an explicit pixel-content check rejects an
empty bake. The corrected texture is packed into both Blender and GLB assets.

These are simple study materials, not Hunyuan texture-generation results or a
finished painted treatment. The neutral clay views remain the shape evidence.

## Binding and review scope

Twenty authored bones describe the torso/head, arms, legs/paws and tail. Blender
automatic weights are constrained to a rigid head and paws, then reduced to the
strongest four normalized influences. No vertex is left unweighted. The Blender
file contains three keyed study poses at frames 1/13/25, 12fps: neutral, raised
arms and a bent right leg. These are two deformation checks, not a run cycle.
The head and left support-paw controls remain fixed across the checks.

Blender uses volume-preserving skinning. Standard GLB viewers use their own
skinning implementation and can differ around bends. The retained Blender PNGs
are the reference for this offline sprite study.

The hands have simplified/incomplete digits, the hair/cheek tufts are rough,
and facial topology has not been rebuilt for expressions. These known limits
do not invalidate the accepted baseline or block a run attempt. There are no
facial/finger controls, separate garments or final locomotion clip. Running is
next; do only the rig/weight fixes that its actual defects require, with other
refinement sequenced by the plan of record.

## Reproduce

With the external checkout and isolated dependencies available on Derry, use
its existing Python with the experiment dependency directory on `PYTHONPATH`.
Keep `HF_HOME` and `U2NET_HOME` inside the isolated work directory.

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

Output directories must be new. On Derry, Eevee requires `DISPLAY=:0`.
The recorded complete render is `study-v3/`, copied into the study's `result/`.
The exact successful runner and Blender source snapshots accompany the outputs.

Validation:

```bash
build/tileset-venv/bin/python -m unittest tests.storybook_shape_test tests.storybook_neutral_asset_test
git diff --check
```

The ten focused tests cover input immutability, raw-output hashes, bounded
cleanup, packed/nonempty texture data, finite mesh/UV data, skin weights,
constant bone lengths, connected chains, fixed head/support paw, ground and
transparent sprite margins. The user's explicit verdict establishes artistic
acceptance; the tests protect the implementation and retained artifacts.
