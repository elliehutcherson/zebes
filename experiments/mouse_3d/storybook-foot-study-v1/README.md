# Storybook mouse: foot shape and articulation study 01

Authored 2026-09-12 under the [plan of record](../../../docs/mouse-3d-plan.md).
**Positively reviewed 2026-09-12:** "okay, this is much better." The user next
wants a running animation at a similar speed to the raptor reference, with
more animated arms. This study supplies the source for run 03; the feedback
does not establish finished skinning or run acceptance. The generated review
retains its status from the time of authoring. See the
[run 03 direction](../../../docs/mouse-3d-plan.md#immediate-next-step-run-03-with-active-arms).

- [Interactive review](result/review.html): playback, frame stepping, five pose
  buttons, side/game cameras, joint overlays and actual 96/128px previews.
- [Foot comparison](result/foot-comparison.png) and
  [original/revised front, side and game views](result/comparison.png).
- [Five blocked poses](result/poses.png) and [motion preview](result/game-study.gif).
- [Editable packed Blender copy](result/foot-study.blend).
- [Saved-action measurements and exact edit region](result/study.json).
- [Export metadata](result/review.json) and [preservation hashes](preservation.json).

## What changed

The source was opened from the accepted `neutral-mouse.blend` and verified
against SHA-256 `32f9bb3c460bc09086cac0afcfb29e26941e2bea5bb8e726459e6caa223672c9`.
The accepted bundle's 186 files, run 01's 115 files and run 02's 116 files are
preserved byte for byte. This study does not replace the master.

4,089 vertices are reshaped and 3,722 vertex weight records change. The edit
region is selected from the original foot/shin/thigh influences below Z=0.72;
its original bounds and exact indices are retained. A smooth rear-foot lift
of at most 0.20 units, small backward shift and 22% maximum lateral taper
preserve the broad front toe pad. No topology is added. The 39,998 vertices,
80,000 triangles, UVs and packed color texture remain. Geometry and weights
outside the region have matching before/after hashes.

Each leg now has hip → knee → hock → toe-base → toe-tip articulation. The knee
and hip rest positions stay fixed. Each hock moves into the raised rear foot;
the shin ends there, the foot ends at a new toe base, and a new toe bone carries
the pad. Six bones are added or changed: shin, foot and toe on each side.
The other rest bones are unchanged. Local weights blend the new bend into the
existing shin weights. There is no automatic rebinding or texture rebake.

## Pose test and limits

`Foot_support_articulation_01` contains five blocked poses at frames
1/9/17/25/33: neutral support, landing, compression, push-off and folded recovery.
Ordinary editable FK keys use analytic leg solutions at quarter-frame intervals.
The pelvis lowers during compression and advances over the left toes into
push-off. The left toe patch stays stationary through landing/compression,
then rolls onto its front at push-off. The right paw supports the recovery.
The torso stays upright. Playback deliberately holds the endpoints and resets
after recovery; this is not a closed run cycle.

The foot shape and chain are fitted to the mouse. The inspected raptor informs
separate toe motion and leg folding; no raptor action, joint coordinates,
proportions or horizontal torso are imported.

The saved file is reopened before 257 measurements and final rendering. All
39 focused tests pass across this study, both runs and the neutral source.
Checks cover preservation, local edit scope, bone lengths, connected joints,
normalized weights, a broad planted toe patch, sole penetration, noncollapsed
local triangles, and decoded frame cells. Maximum toe support height error is
0.0091px at 512px; maximum whole-mesh ground penetration is 0.0234px. The maximum
bone length error is below 0.000001 units.

The entire mesh is checked against the ground, including blended toe edges.
The rigid contact patch uses vertices with exactly 1.0 toe weight; a first audit
incorrectly included the blend boundary and detected its small intended motion.
The correction changes the measured patch, not the skinning or test tolerance.

Edge-length ratios are retained as deformation diagnostics, not artistic pass
criteria. The strongest recovery bend stretches some local edges to about four
times their rest length; inspect the knee/hock crease in the close-up. The
study demonstrates articulation but does not establish finished skinning or
run acceptance. Further local weight correction should target that visible
fold. Topology changes are not yet justified by this study.

All frames use the fixed level game camera from runs 01–02, orthographic scale
5.2, ground Z=0 and the same origin; an additional fixed profile camera exposes
the chain. The PNGs are 512px RGBA. Whole-frame reductions produce 96/128px
previews without fitting or repositioning individual silhouettes. HTML/GIF
playback holds interior frames for 70ms to make the study easy to inspect; the
editable Blender timeline is 24fps. These are study playback settings, not
proposed run cadence. Browser playback, scrubbing, pose buttons, frame stepping,
side/game selection and joint-only review were inspected.

## Reproduction

Sources live in `experiments/storybook_mouse/` and `scripts/`; successful
source snapshots accompany the outputs. Derry's Blender 4.0.2 rendered with
`DISPLAY=:0` in `/tmp/zebes-storybook-foot-study-20260912/result-03`. ComfyUI
was untouched. Always use a new output directory.

```bash
DISPLAY=:0 blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/foot_study.py -- \
  --master experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend \
  --out /path/to/new-result
build/tileset-venv/bin/python scripts/review_storybook_foot.py /path/to/new-result
build/tileset-venv/bin/python -m unittest tests.storybook_foot_study_test \
  tests.storybook_run_test tests.storybook_run_v2_test \
  tests.storybook_shape_test tests.storybook_neutral_asset_test
git diff --check
```

Use these poses to author run 03 with cadence, opposed legs/arms, loop closure
and virtual travel under the existing export contract. Keep this study and
both earlier runs available for comparison.
