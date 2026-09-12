# Storybook mouse run 03

Run 03 continues the positively reviewed foot study under the
[plan of record](../../../docs/mouse-3d-plan.md). **User verdict: not accepted as
a running animation.** Technical checks do not establish artistic acceptance.
The generated review retains its pending-verdict label from authoring time.

- [Interactive review](result/review.html): normal-speed mouse/raptor comparison,
  game/profile cameras, playback, frame stepping, scrubbing and rig/contact overlays.
- [First preview beside revision](result/revision-comparison.gif).
- [Mouse and attributed raptor](result/mouse-and-raptor.gif).
- [Editable packed Blender run](result/run-mouse.blend).
- [512px sheet](result/game-512.png), [128px](result/game-128.png), [96px](result/game-96.png).
- [Motion measurements](result/motion.json), [frame metadata](result/review.json),
  [artifact hashes](result/artifact-sha256.json), [preservation manifest](preservation.json).
- [Hock comparison](result/deformation-comparison.png),
  [exact weight edits](result/weight-edits.json), [fist shape edits](result/fist-edits.json).

## Direction and feedback

The first complete preview is preserved in [draft-02](draft-02/review.html).
The user called it an overall improvement, but said it looked like high-stepping
or pretending to run. They requested a slightly longer stride, some forward
lean, closed fists, and more study of other runs. The revised motion applies
that feedback. The user's subsequent verdict is that it could become a good
walk with slower playback and more relaxed arms, but **is not a running
animation**. The run needs a much larger visible stride, much stronger forward
lean and a back foot much farther behind the body. A walk variant is only a
possible later adaptation, not an accepted clip.

The next workstream is tech debt reduction and compacting the notes/findings.
Further animation revisions are paused; see the [handoff](../../../docs/handoff.md).

The [additional reference record](../../../notes/run-reference-2026-09-12/README.md)
links to the AnimSchool/Angelo Sta Catalina and Animation Mentor/Jason Martinsen
examples inspected, with the actual video segments and the distinction between
observations and mouse authoring choices. The raptor remains a cadence and
digitigrade-articulation reference, not a proportion or horizontal-torso target.

| Motion | First preview | Revised run |
| --- | --- | --- |
| Cycle | 0.5333s | 0.5333s |
| Alternating steps | 3.75/s | 3.75/s |
| Declared stride | 2.60 units | 3.05 units |
| Virtual forward speed | 4.875 units/s | 5.719 units/s |
| Support per paw | 32% / 171ms | 36% / 192ms |
| Maximum recovery clearance | .44 units | .27 units |
| Body bob peak-to-peak | .18 units | .11 units |
| Added spine lean | .07 radians | .19 radians |

The saved spine axis is approximately 13.5 degrees from vertical in the revised
run. The head retains its rigid orientation while following the inclined body.
Upper arms oppose the same-side leg, elbows change bend, wrists follow through,
and the palms face inward. The widened arm paths clear the torso and thighs.

## Source and local edits

Authoring opens `storybook-foot-study-v1/result/foot-study.blend`, SHA-256
`98c1edfc4a00d8922600327a3afc66aad3c7998c80e4d90828f236dd960146a2`, verified
against that study's manifest. The existing 22-bone rig, foot geometry, topology,
UVs and packed 2048px texture remain. No model generation, rebinding, texture
baking or engine/production import occurs.

The accepted neutral bundle (186 files), run 01 (115), run 02 (116), and foot
study (97) are preserved byte for byte. The source actions remain in the working
copy; `Storybook_Run_03` is active. Ordinary FK location/quaternion keys are
baked every eighth frame. All arm and leg channels close at frame 25; exports
omit that duplicate. The editable action plays frames 1–24 at 45fps.

3,063 weight records change in the explicit lower-leg region .24 < Z < .84.
Only the shin/foot split changes; toe, thigh and other influences remain fixed.
The first measured rear-hock weight cliff changed foot influence by .119 over
an edge only .0093 units long. A broad smooth transition reduces that cliff.
`weight-edits.json` contains exact indices, before/after weights, region bounds
and matching outside-region hashes. `weight-baseline/` renders the final leg
motion with the original study weights for direct comparison.

The `Running_fists` shape key compacts 3,251 hand-region vertices and is held at
1 throughout the run. `Basis` retains the open hands; setting the shape to 0
restores them. The generated character has partly fused digits: this produces
a compact closed-paw silhouette without claiming an anatomical finger rig.
The saved shape data is hashed and checked after reopening. The inward wrist
orientation presents the knuckles during the run.

## Verification and remaining limits

Blender reopens the saved run before measuring 385 points, including between
keys, and rendering both cameras. Checks cover constant bone lengths and joint
attachments, fixed toe-patch support in represented ground space, toe roll,
ground penetration, loop closure, local edit scope and fist shape preservation.
Body-mesh BVH checks cover forearm/hand triangle overlap and hand clearance;
they are not a claim of whole-character self-collision freedom.

The final package's focused tests verify both full and sparse sheets against
the rendered RGBA frames, decode APNG frames and duration, check transparent
margins/common origins, and hash-check all four preserved input bundles.
Final measurements are recorded in `review.json` and `deformation-comparison.json`.

All **51 focused tests pass** across run 03, the foot study, both earlier runs
and the neutral checkpoint. Maximum toe support height error and whole-mesh
ground penetration are .00772px at 512px. Maximum bone length error is below
.000001 units; loop joint error is zero. Forearm/hand-to-body triangle overlaps
are zero across all 385 checks; minimum hand surface clearance is .1119 units.
The hock region's maximum edge-length ratio drops from 4.927 to 3.820 in the
matched leg poses. Browser inspection covered playback, pause, scrub, frame
wrap, profile/rig-only controls, speed changes and actual 96/128px playback.

The hock comparison shows reduced stretching, not perfect skinning at every
fold. Whole-mesh diagnostics also retain a larger stretch near the existing
pelvis/tail-root transition outside the repair region. Hands remain a stylized
closed-paw approximation. The user's visual assessment should drive further
targeted work rather than another broad model rebuild.

Every frame uses the fixed, level orthographic camera contract: -Y forward,
Z up, ground zero, game camera `(-10,-7,2.13)` looking at `(.17,-.3,2.13)`,
scale 5.2, left near. The profile camera is also fixed. The origin is
`(256,465.723)` at 512px. All small frames are whole-frame Lanczos reductions;
there is no per-frame fitting. The 24/12-sample APNGs total 533ms. GIF previews
use coarser 10ms timing and total 530ms; HTML uses the exact nominal cadence.

## Reproduction

Sources are in `experiments/storybook_mouse/` and `scripts/`, with successful
snapshots beside the generated outputs. Derry runs Blender 4.0.2/Eevee with
`DISPLAY=:0`; ComfyUI remains untouched. Use new output directories. The
final remote render is `/tmp/zebes-storybook-run03-20260912/result-02`.

```bash
DISPLAY=:0 blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/animate_run_v3.py -- \
  --study experiments/mouse_3d/storybook-foot-study-v1/result/foot-study.blend \
  --manifest experiments/mouse_3d/storybook-foot-study-v1/artifact-sha256.json \
  --out /path/to/new-result
build/tileset-venv/bin/python scripts/review_storybook_run_v3.py /path/to/new-result
build/tileset-venv/bin/python -m unittest tests.storybook_run_v3_test \
  tests.storybook_foot_study_test tests.storybook_run_test tests.storybook_run_v2_test \
  tests.storybook_shape_test tests.storybook_neutral_asset_test
git diff --check
```

Packaging resolves `draft-02/` and `weight-baseline/` beside the result for
comparisons and uses the retained raptor frames under `notes/`. Reproduce that
layout when packaging elsewhere. `--preview` authors all 24 frames in each
camera with fewer render samples and 49 measurements; `--keep-study-weights`
creates a deformation baseline. Later draft folders retain development checks;
only `result/` is the current review deliverable.
