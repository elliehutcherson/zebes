# Run and gap-input comparison

Prepared 2026-09-09. **Connection inputs reviewed with reservations.** Twelve
subsequent requests are retained separately in `../run-gap-control-v1/`.
This directory remains the unchanged preparation snapshot.

Review response: the user considers testing the leg connection worthwhile,
but has not accepted the cuff positions as correct. They identified that the
boot must show different surfaces through the stride. This frozen setup
protects most boot pixels, so it is only a connection-control experiment.
See `docs/sprite-run-experiment-plan.md` for the revised whole-leg/boot approach;
the next generation input review must include the intended boot views.

Open `review.html` for source binding, all twelve blank/blur/shaped input
variants, editable masks, and an independent motion benchmark. The inline
review reconstructs every 256px input exactly from its original plus a compact
pixel delta. The zoom is a viewing operation; every submitted candidate would
use the entire 1024×1024 canvas. `prefills/` retains those exact PNG inputs.

The accepted `mouse_run_reference_v2.json` and its frame order remain unchanged.
Source-space cuff endpoints in `../../inputs/run-gap-trial-v1.json` are authored
estimates for review. They follow each boot's existing rigid transform and are
separate from ankle joints. The shaped variant draws rough dark trousers into
missing regions, behind the existing artwork. The blur variant extends nearby
character colors without mixing in transparent white. Both use the same mask.
The mask admits a small cuff transition while protecting the remainder of the
boots, head, torso/coat, arms and tail. This trial addresses the lower-body gaps;
existing shoulder/neck defects and boot directions are still visible.

## Motion benchmark

`spineboy-pro.source.json` is the unchanged published Spine example, retained
for evaluation with its license notice. Provenance and SHA-256 are recorded in
`spine-run-samples.json`. The source is
[Esoteric Software's Spineboy](https://en.esotericsoftware.com/spine-examples-spineboy).
The vendor describes using exported examples to evaluate its runtimes on the
[trial download page](https://en.esotericsoftware.com/spine-download).

`scripts/sample_spine_run.mjs` evaluates the example's original run with the
official `@esotericsoftware/spine-core@4.2.120` package. It retains the run's
bone, IK, transform and slot timelines, omitting image attachments and unrelated
clips with mesh-deform timelines. No runtime is embedded in Zebes. Twelve
distinct phases are sampled over approximately 0.6667 seconds; a separately evaluated,
non-wrapped closing endpoint matches the first sampled pose (maximum joint
distance 0 in original rig units). The endpoint is not exported as an extra
frame. This verifies sampled skeletal closure, not artwork or foot-contact quality.

The review uses one uniform display transform per entire clip. Each clip keeps
its own initial phase; matching column positions are not matched semantic poses.
The sample has a weapon-holding arm and its original body proportions. It is
a motion/rig benchmark, not a fitted or accepted mouse animation.

## Prepared ComfyUI trial

`workflows/` contains twelve initial API graphs, not submitted requests:
frames 5 and 10 × blank/blur/shaped × denoise 0.35/0.60, seed 17290401.
Four checks remain reserved as described in `workflows/plan.json`. The full
cycle is a later twelve-image batch after useful pilot results. This is the
approved bounded E4 allocation, not a new parameter sweep.

The installed node schemas and model filenames were read from derry's live
ComfyUI `/object_info`. Actual inference and resource usage remain untested.
The graph uses explicit SDXL, pixel-art-xl, IP-Adapter Plus and CLIP Vision
loaders. Regular VAE encoding retains prefill pixels. A grayscale-to-mask node
reads the PNG's red channel and supplies `SetLatentNoiseMask`; the PNG is opaque,
so the loader's alpha-derived mask would be empty. Canny and depth are disabled.
Raw decoded output and a composite restored outside the mask are saved
separately. Matte removal and final 48px registration remain later evaluation
steps. `workflows/plan.json` records identity hashes and fixed settings.

After input approval, upload the retained `prefills/` tree under ComfyUI's
`input/zebes-run-gap-v1/` and submit graphs individually, beginning with the
first measured inference. Do not queue all requests before checking the first
output, actual mask effect and memory use. No uploader or provider invocation
is part of this evidence directory.

## Reproduction

The Python scripts use the existing `build/tileset-venv/bin/python` environment.
Use a fresh output revision; preparation refuses to overwrite existing inputs.
Run commands from the repository root.

```bash
npm install --prefix build/run-reference-tools --ignore-scripts --no-audit --no-fund --save-exact @esotericsoftware/spine-core@4.2.120
node scripts/sample_spine_run.mjs \
  build/run-reference-tools/node_modules/@esotericsoftware/spine-core \
  experiments/character_binding/evidence/run-gap-inputs-v1/spineboy-pro.source.json \
  experiments/character_binding/out/run-gap-reproduction/spine-run-samples.json

build/dev/bin/render_layered_puppet \
  --source=experiments/character_binding/inputs/interactive-run-source-v1.png \
  --document=experiments/character_binding/puppet_documents/mouse_run_reference_v2.json \
  --output=experiments/character_binding/out/run-gap-reproduction/baseline \
  --frame_size=48 --zoom=8

build/tileset-venv/bin/python scripts/prepare_run_gap_trial.py \
  --document experiments/character_binding/puppet_documents/mouse_run_reference_v2.json \
  --config experiments/character_binding/inputs/run-gap-trial-v1.json \
  --render experiments/character_binding/out/run-gap-reproduction/baseline \
  --output experiments/character_binding/out/run-gap-reproduction/prefills

build/tileset-venv/bin/python scripts/prepare_gap_workflows.py \
  --prefills experiments/character_binding/out/run-gap-reproduction/prefills \
  --identity experiments/character_binding/inputs/interactive-run-source-v1.png \
  --output experiments/character_binding/out/run-gap-reproduction/workflows
```

`scripts/render_gap_trial_review.py --help` lists the explicit inputs for
regenerating the review fragment. The cuff configuration freezes the accepted
document hash so changing the binding requires reviewing new annotations.

Verification: five focused Python tests pass. Every prefill preserves pixels
outside its declared mask; the review builder checks exact pixel reconstruction.
Browser review exercised source alignment, masks, frame selection, motion
playback and narrow layout. These are input/tool checks, not animation approval.
