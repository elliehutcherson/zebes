# Storybook mouse run 01

Created 2026-09-12 from the **accepted saved Blender master**, following the
[plan of record](../../../docs/mouse-3d-plan.md). **Run verdict pending.**

- [Interactive playback and diagnostics](result/review.html)
- [Editable run copy](result/run-mouse.blend)
- [Animated preview](result/run-384.gif), [eight key poses](result/contact-sheet.png)
- [512px sheet](result/sprites-512.png), [128px sheet](result/sprites-128.png),
  [96px sheet](result/sprites-96.png)
- [Frame metadata and hashes](result/review.json), [motion](result/motion.json),
  [saved-file verification](result/saved-scene-check.json)

## Preservation

The master was hash-checked, opened with Blender's `open_mainfile`, and saved
into a separate run directory before changes. `inspection/` retains the initial
loaded copy and measured rest rig/paws; `draft-01/` retains the eight-pose draft.
All six accepted bundle hashes still match. The run retains the same 39,998
vertices, 80,000 triangles, UVs, weights, twenty rest bones and packed texture
bytes. No generation, rebinding, baking or rig/weight repair was needed.
`Neutral_and_two_body_checks` is retained with a fake user; `Storybook_Run` is
active. There are no engine dependencies, CMake targets, provider calls or
production imports.

## Motion and export contract

- Z up, forward -Y; left is near. Level orthographic camera at `(-10, -7, 2.13)`,
  target `(0.17, -0.3, 2.13)`, scale `5.2`. Ground is `z=0`, projected to row
  `465.723` in every 512px cell. Common sprite origin: `(256, 465.723)`.
- A stride takes 2/3 second: 24 frames at 36fps, or every other frame for 12
  samples at 18fps. Both give three steps per second. Virtual speed is 3.15
  scene units/second toward -Y; stride length is 2.1. Each paw stands for 34%
  of the cycle, with opposing phases and two intervening flight phases.
- The measured rigid sole travels backward at virtual speed during stance.
  Lifted recovery trajectories return with continuous position/velocity at
  touchdown. Analytic two-bone solutions preserve measured limb lengths and
  reject unreachable targets. Arms counter-swing; body bob, forward lean and
  restrained tail motion complete the action while the head stays stable.
- Ordinary FK keys are baked every quarter Blender frame (97 keys including
  closure). Frame 25 equals frame 1 and is omitted from every sprite export.
  The action has cycle modifiers and named gait-phase markers.
- Six viewport markers (`Sole`, `Heel`, `Toe`, each side) are parented to foot
  bones at measured mesh vertices. They and the ground-origin helper do not
  render. Their transforms match evaluated geometry after reopening the file.
- Lighting, camera, scale and origin are fixed. The 512px RGBA PNGs are masters;
  96/128px PNGs use whole-frame Lanczos reduction without cell fitting or a new
  palette/outline treatment. Each size has 24 PNGs, a 6×4 sheet and a 6×2
  twelve-sample sheet. APNGs preserve frame content, rounded to 667ms. GIFs are
  convenience previews with coarser format timing and palette.

The local HTML player uses the declared 2/3-second duration and provides
play/pause, scrub, frame step, speed controls, synchronized 24/12 sampling,
ground/contact overlays and rig-only view. It makes no external requests.

## Verification

Nine focused tests pass. Blender measures 193 points, including between keys,
and matches them after saving/reopening. Maximum stance sole error is
0.0002002 units (0.020 pixels at 512px), below the explicit 0.001-unit subpixel
tolerance. Maximum bone-length error is 0.00000134 units; loop joint error is
zero. Weights remain normalized, limb joints remain attached, and decoded PNG
cells/APNGs match both exported samplings. Browser playback, pause, scrubbing,
frame wrap, contacts and rig-only view were checked at actual sprite sizes.

The user's visible run verdict is next; technical checks do not establish
artistic acceptance. Simplified hands and rough tufts remain from the accepted
model. Preserve this run and use a new versioned directory for motion revisions.

## Reproduce

Sources live in `experiments/storybook_mouse/` and `scripts/`; exact snapshots
accompany the results. Use new output directories. Derry's Blender 4.0.2 rendered
with `DISPLAY=:0` in `/tmp/zebes-storybook-run-20260912`. No Hunyuan environment
or NumPy export setup is needed, and ComfyUI was untouched.

```bash
DISPLAY=:0 blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/inspect_run_master.py -- \
  --master experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend \
  --out /path/to/new-inspection
DISPLAY=:0 blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/animate_run.py -- \
  --master experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend \
  --out /path/to/new-result
blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/finalize_run.py -- /path/to/new-result
build/tileset-venv/bin/python scripts/review_storybook_run.py /path/to/new-result
build/tileset-venv/bin/python -m unittest tests.storybook_run_test
git diff --check
```

Open `review.html` directly in a browser or serve the repository locally.
