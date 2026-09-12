# Storybook mouse run 02

Created 2026-09-12 from the **accepted saved Blender master** under the
[plan of record](../../../docs/mouse-3d-plan.md). **Visual verdict pending.**

- [Interactive playback, contacts and rig view](result/review.html)
- [Animated preview](result/run-384.gif), [eight poses](result/contact-sheet.png)
- [Editable Blender copy](result/run-mouse.blend)
- [512px sheet](result/sprites-512.png), [128px](result/sprites-128.png),
  [96px](result/sprites-96.png)
- [Frame metadata and hashes](result/review.json), [motion](result/motion.json),
  [saved-file verification](result/saved-scene-check.json)

## Foot mechanics and pace

Each stance uses one actual mesh vertex on the toe pad as its fixed ground
contact. The heel starts raised, compresses above ground, then extends into
toe-led push-off. Recovery begins at release with continuous position and
velocity. The torso axis stays within 6.93–8.65 degrees of vertical and the
head keeps its rigid, stable orientation.

| Contract | Run 01 | Run 02 |
| --- | --- | --- |
| Cycle | 2/3 second | 1/2 second |
| Steps per second | 3 | 4 |
| Virtual speed toward -Y | 3.15 units/s | 4.8 units/s |
| Stride | 2.1 units | 2.4 units |
| Support per paw | 34%, 227ms | 28%, 140ms |
| Recovery per paw | 440ms | 360ms |
| Full / sparse sampling | 36 / 18fps | 48 / 24fps |

The camera, ground, scale and origin match run 01: level orthographic camera
at `(-10, -7, 2.13)`, target `(0.17, -0.3, 2.13)`, scale `5.2`, Z up,
forward -Y, left near. Ground is zero, row `465.723` in each 512px cell;
the common sprite origin is `(256, 465.723)`. All 24 master PNGs retain RGBA.
The 96/128px previews use whole-frame Lanczos reduction, with no per-frame
fitting. Twelve-sample exports omit alternating samples, with the same duration.
APNG timing totals 500ms; GIF is a convenience preview with coarser timing.

## Rig audit and preservation

[The initial audit](inspection/forefoot-audit.json) samples the accepted rigid
paw geometry over pitches of 0.25–1.25 radians. Vertices **8010** (left) and
**35213** (right) remain the lowest points throughout the chosen 0.64–0.92
radian support interval. This provides a planted forefoot pivot without a rig,
weight or geometry repair. Outside stance, the lowest rigid paw point determines
clearance as the paw rotates into recovery.

The existing hip/knee/ankle landmarks and segment lengths are retained. The
same forward knee pole used in run 01 produces a knee ahead of the raised ankle
throughout support; the ankle also remains behind the toe pad. No joint is
reversed and no bone is stretched. Minimum measured heel height is **0.5501
units**, or **54.2 pixels at 512px**. The chosen pivot is a rigid toe-pad support
approximation; there is no new toe-flex articulation.

The master was hash-checked, loaded with `open_mainfile` and saved to a new
working copy. `inspection/master-loaded.blend` retains that loaded checkpoint;
`draft-01/` retains the eight-pose draft. All six accepted-bundle hashes match.
[The preservation manifest](preservation.json) also protects **all 115 files**
in run 01, including its draft, inspection, source snapshots and final results.

Mesh, UVs, normalized weights, twenty rest bones and packed texture remain
identical to the master and run 01. `Neutral_and_two_body_checks` is retained;
`Storybook_Run_02` is the active action. The file has ordinary FK keys at quarter
frames, cycle modifiers, named phases, and eight foot-parented mesh markers
(`Pad`, `Sole`, `Heel`, `Toe`, both sides). Frame 25 closes onto frame 1 and is
omitted from exports. No provider calls or engine/production imports were made.

## Verification

**23 focused tests pass**: nine existing run checks and fourteen run 02 checks.
They validate both preserved bundles, actual saved-action support over whole
stances, raised-heel compression/push-off, knee/ankle relationships, upright
posture, pace, recovery, bone lengths/attachments, loop closure and decoded
PNG/APNG content. Blender checks 193 points including between keys, then reopens
the saved result and checks those same poses and attached markers.

- Maximum planted pad height error: **0.0002845 units / 0.0281 pixels at 512px**.
- Contact tolerance: **0.001 units**, below one tenth of a 512px pixel.
- Maximum bone-length error: **0.00000153 units**; loop joint error: **zero**.
- Saved/reopened joint error: **zero**; source fingerprint unchanged.

Browser review confirmed playback, pause, scrub, frame wrap, contact overlays,
rig-only view and 96/128px previews. These checks establish implementation
behavior; the user's visible verdict on foot mechanics and pace is still next.
Known hand/tuft limitations remain from the accepted model.

## Reproduce

Use new output directories. Sources in `result/` snapshot the successful
authoring, finalization and packaging code; current sources are under
`experiments/storybook_mouse/` and `scripts/`. Derry's Blender 4.0.2 rendered
with `DISPLAY=:0` in `/tmp/zebes-storybook-run02-20260912`. ComfyUI was untouched.

```bash
blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/audit_forefoot.py -- \
  --master experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend \
  --out /path/to/new-inspection
DISPLAY=:0 blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/animate_run.py -- \
  --master experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend \
  --out /path/to/new-result --revision 2
blender --background --threads 4 --python-exit-code 1 \
  --python experiments/storybook_mouse/finalize_run.py -- /path/to/new-result
build/tileset-venv/bin/python scripts/review_storybook_run.py /path/to/new-result
build/tileset-venv/bin/python -m unittest tests.storybook_run_test tests.storybook_run_v2_test
git diff --check
```

Open `result/review.html` directly or serve the repository locally. Preserve
this run for its verdict; subsequent motion revisions use another versioned copy.
