# Pose analogy: grid-and-skeleton transfer from a reference sprite sheet

**Superseded by the [accepted 3D mouse](../../docs/mouse-3d-plan.md).**
The open questions below describe an unsubmitted proposal, not remaining work.

Status at closure on 2026-09-12: **inputs built, nothing submitted.** No image generator has
seen any of this. `manifest.json` records `"submitted": false` and the sha256 of
every input and output.

The idea under test, proposed by the user: put a grid and a skeleton over a set
of reference sprites, put the same grid and a matching skeleton over our own
character, and ask a generator to draw our character in the reference's poses.
Each card states one analogy — A is to A' as B is to B'.

Build with `scripts/prepare_pose_analogy.py`; check with `tests/pose_analogy_test.py`.

## What is retained

- `inputs/reference-run-10.png` — ten unmodified 80x80 cells lifted from row
  `09: Moving right - not aiming` of the supplied sheet, at x = 18 + 96i, y = 1202.
- `inputs/reference-neutral.png` — one unmodified standing cell at x 18, y 1074.
  Retained as evidence only. It is no longer used to build anything: the user
  judged it a poor choice of frame, and it stands in a wide idle stance unlike
  any pose in the run. The grid anchors now come from the running cycle itself.
- `inputs/reference-run-trace-v1.json` — manual joint traces, schema matching
  `character_binding/inputs/run-pose-trace-v1.json`.
- `evidence/analogy-inputs-v1/request/` — the submission set: five reference
  images and five mouse images, one mouse image carrying the character artwork.
  Frames 1, 3, 5, 7 and 9, sampled every other frame so the set covers near
  support, far support and one flight phase.
- `evidence/analogy-inputs-v1/request/character-reference.png` — an optional
  eleventh image. On the artwork card the drawing is in its bind pose while the
  skeleton holds the requested pose, so the two visibly disagree and the artwork
  can be misread as the pose. Here they agree, which is what teaches the
  skeleton-to-artwork correspondence. Include it or drop it.
- `evidence/analogy-inputs-v1/trace-check.png` — the trace over the sprites, for
  checking the tracing itself.
- `evidence/analogy-inputs-v1/target-skeletons.png` — the ten mouse targets.

## Decisions, and why

**The grid is normalised per character, not shared in absolute pixels.** Each
character's cell is a twelfth of its own standing height, with the origin on its
hip column and ground row, and both panels show the same window measured in
cells. A shared absolute lattice would tell the model to reach the reference's
rows with the mouse's limbs, which is how `proportions-v3` ended with legs too
narrow and a back arm far too large. The proportion difference is not hidden by
this: it moves onto the pink anatomy lines, and the reference's hip line sits
about two cells higher in the body than the mouse's.

Leg-length normalisation was tried first and rejected for the card. It makes a
three-cell stride mean the same distance on both bodies, but the mouse's short
legs and large head then make it twice as many cells tall as the reference and
the two rows stop looking comparable.

**The skeleton states depth rather than leaving it to be inferred.** Near limbs
are orange, far limbs blue, matching `run-phase-audit-v1`, and the translucent
torso and head are composited between the two limb sets so a far limb visibly
passes behind the body. The isolated-leg trial established that outline control
does not settle which way a limb faces; a bare stick figure is outline control.

**Leg scale is fitted, not taken from the first frame.** `transfer()` derives its
scale from whichever pose it is handed, and `reference_01` is the most folded
pose in this cycle (21.57 against 28.50 for `reference_08`). Handing it the first
frame stretched every other frame by up to 1.32x and floated the grounded hip
about 30px above the mouse's own hip row. One factor, 2.4597, fitted once on the
most extended supported frame, now stands that frame exactly on the hip row; the
hip then bobs 149.0 to 161.5 around a standing row of 153.

**Elbows and wrists are derived, not traced.** At 42px of body height an arm
segment is two or three pixels, so a traced elbow carries about 20 degrees of
quantisation noise, and transferring it folded the mouse's near arm against its
hip. The shoulder and the cannon muzzle are the readable ends; the two interior
joints are placed along that line with a small downward bow. The raw readings
stay in the trace file, superseded by `derive_arms`.

## The first trace was mostly wrong

Reviewed against the artwork after the user rejected it, and it does not hold up:

- the far arm runs straight down the middle of the chest, where no arm is painted
- a far toe lands in empty background, off the sprite
- knees sit at mid-thigh rather than where the leg outline bends
- the hip sits at the waist rather than the pelvis

Only `head_top`, `paw_l` (the cannon muzzle) and the two toes follow readable
silhouette edges. Everything else was inference drawn as a solid line, which is
how it passed for measurement. Those four joints are now the only ones marked
`observed` in the trace; the rest are `estimated` and draw dashed with hollow
dots, in the cards and in the editor alike.

Sprite choice is not the fix. Bigger sprites — 3rd Strike, Garou, KOF XIII and
Skullgirls all run 110px and up, with the far limbs shaded darker — would make
the guessing less bad. They would not make it right, because a sprite is a flat
projection and near/far is a fact about the original 3D pose that the artist
either encoded or did not. If limb ownership is the blocker, a 2D sheet is
structurally the wrong source for it.

## Validate the retained trace

The browser trace editor and authoring server are retired. Their implementation
is available at Git checkpoint `ede7b18`. Retained trace data and all reference
artwork remain unchanged.

```bash
scripts/trace_sprite_sheet.py \
  --sheet experiments/pose_analogy/inputs/reference-run-10.png \
  --trace experiments/pose_analogy/inputs/reference-run-trace-v1.json
```

This command checks joint placement, observed/estimated confidence, source-cell
bounds, and support-foot consistency. It prints a result and exits without
modifying files. The re-trace findings below record the earlier editor session.

## The user's re-trace, 2026-09-12

The first pass was replaced by hand in the editor. 135 of 170 joints are now
marked observed, against 40 before, and the skeletons follow the sprites. The
support pattern came out as a proper run: near for four frames, flight, far for
four frames, flight.

Rebuilding on it exposed three defects, all now fixed:

- **`reference_01` named the raised foot as its support.** Its near foot sits ten
  source pixels lower, and both neighbouring frames say near. Grounding drove the
  mouse's near toe fourteen pixels below the floor. The label is now `near`, and
  `support_conflict` in the tracer refuses to save this class of error.
- **Grounding seated the toe, burying heel-down feet.** One support pose put its
  ankle five pixels under the floor. `foot_contact` now grounds on whichever of
  ankle or toe reaches lower.
- **`derive_arms` overrode hand-traced elbows.** It was written to clean up the
  first pass's noisy guesses and was still replacing observed joints with a
  synthetic bow. It now skips any joint marked observed.

## Open: the hip bobs half again too much

The mouse's hip rises and falls 0.415 of its standing hip height where the
reference manages 0.316, about 31 percent too much. This is not a tuning error.
Transferring scaled joint distances preserves the reference's angles, not its hip
height, and the mouse's bind legs start far more bent than the reference's, so
straightening them lifts its hip proportionally further. A sweep of the single leg
scale from 0.8 to 3.0 bottoms out at 0.467 and rises from there, so no value of
that knob closes the gap.

Closing it needs two-bone leg IK: take the hip height from the reference, plant
the foot, solve the knee. The figure is recorded as `hip_bob` in the manifest and
asserted as a bounded gap in the tests, so the work has a number to beat.

## Remaining limits

**Tracing precision is about one to two source pixels**, roughly 3 to 5 percent
of stature. A 42px source cannot specify a pose on a 129px character more finely
than that, however carefully the joints are placed.

**The far arm is not drawn** in most frames. No amount of care recovers it from
this sheet; it can only be marked guessed.

**The reference pixels are Nintendo's.** Traced coordinates are geometry and
carry no such claim. Decide whether the reference pixels go to a generator at all
before submitting; the traced skeletons alone carry the pose information.

## Open, and what settles it

Whether a grid plus a skeleton is enough conditioning at all. The cards are a
prompt-shaped input, which suits a built-in imagegen request; a ComfyUI run would
want the target skeleton as an actual control map rather than a picture of one.
That choice is unmade. Run a bounded comparison on two or three poses before the
full ten, and judge limb ownership and boot view separately from silhouette.
