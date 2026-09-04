# Character layer deformation

**Status:** 2026-09-04. Immutable-coat A/B proof implemented in
`semantic-arm-immutable-coat-v1`; human review pending. The imported
See-through `topwear` survives with zero RGB changes, zero alpha additions, and
zero alpha removals. The relationship-stretch candidate is rejected because it
modified a coat layer that was already correct.

Two pre-existing hard gates still fail intentionally: 149 static orphan pixels
and four airborne triangle folds. The earlier ARAP-first plan remains withdrawn.

## The problems

Measured on `semantic-arm-v5` with `layered_puppet_diagnostics`.

**1. Old arm pixels stuck to the body — 105 px, 3 clumps.** They did not move, so
they floated beside the character once the arm swung away. The arm outline was
two hand-drawn polygons of 4 and 5 points, which cannot follow a painted sleeve
edge. Step 3 replaced them; the polygons no longer exist in the spec.

The ownership check missed it because it only asks whether each pixel has exactly
one owner. A leftover arm pixel owned by the body has one owner. Step 3 took it
to zero.

**1b. The tail is inside the arm layer.** `parts/front_arm.png` carried the tail
past the wrist and a sliver of belt on the inner edge, so both swung with the
arm. That is why the airborne arm read as a stick.

Same cause as problem 1, opposite direction: the hand-drawn polygon reached x198
while `handwear-l` ends at x186. One polygon left arm pixels behind, the other
took body pixels along. Step 3 fixed both.

**2. We damaged a correct coat layer.** See-through's generated `topwear`
already represents the coat with both arms absent. The pipeline then required
every moving-arm pixel to have static coverage and stretched hundreds of pixels
into that footprint. Coverage passed while the coat became too wide and read as
another arm.

The correct invariant is simpler: imported coat RGB and alpha are immutable.
Arm pixels outside that silhouette reveal background. Shoulder attachment, if
needed, is a separate local deformation effect; cast shadow is a separate
pose-local tonal effect that may not change coat alpha.

**3. The elbow loses area.** Two causes, both fixed in steps 2a and 2b.

Averaging two rotated positions does not give a rotation, it gives something
squashed. Fixed in step 2a: average the angle instead.

The mesh also folds over itself. Folded triangles paint over each other, so their
area is lost.

| pose | elbow bend | folds | on artwork |
|---|---:|---:|---:|
| neutral | 0 | 0 | 0 |
| passing | 6° | 0 | 0 |
| contact | 59° | 43 | 14 |
| airborne | 62° | 44 | 14 |

Bend angle drives it. Passing barely bends and folds nothing.

Only 14 folds were on the arm. The other 31 were in empty space: the mesh gridded
the arm's whole bounding box, but the arm is a thin diagonal inside it, and the
far corners swung wide. Steps 2a and 2b fixed both causes for contact and
passing; airborne still folds 4 after the joint correction.

## Why not ARAP

At 48px the arm is ~60 pixels. The hole is ~26 of them, the debris was 4, and the
gap between a good and a mediocre solver about 2. The two problems ARAP does not
touch are far more visible than the one it does.

The folding was also narrower than it looked. Weight depended only on distance
along the bone, so a pixel inside the elbow and one outside got the same weight
when only the inside needs to compress. That was a weight bug, and step 2b fixed
it without a solver.

## Plan

### Step 1 — measure. Done.

`layered_puppet_diagnostics` reports folds, backfill coverage, orphan islands,
and interior holes. `SolveLayeredPuppetMeshVertices` exposes deformed vertices.
25 tests. Frame digests unchanged, so nothing about the render moved.

Four gates, each turned on with its fix: `require_no_triangle_inversion` after
2b, `require_part_ownership_isolation` after 3, `require_backfill_coverage` after
4. `require_no_interior_holes` stays off — it cannot be turned on as written,
because the source art itself has 174 enclosed gaps. Gate it against the neutral
count instead.

Two things this corrected:

- Do not measure a pose against the neutral silhouette. Airborne then reports
  3,108 lost pixels, nearly all of which is the body translating up 12 px.
- Interior holes do not catch the coat gash, which is a bite out of the
  silhouette edge rather than an enclosed hole. Backfill coverage catches that.

**Correction.** An earlier version of this document said interior holes were
worthless because "all four poses report 174-175, so it is the source art's own
gaps". That was read once on the first diagnostic run and never re-read. Neutral,
passing and airborne do sit at 174, but **contact is 355** — 181 enclosed
transparent holes that are not in the source. Rendered, they are a slash at the
shoulder and a tear between the hand and the tail.

The metric works. Compare each pose against the neutral count, not against zero.
Step 4 took contact from 355 to 194, so 20 tears remain above the baseline.

### Step 2a — blend the angle. Done.

`TransformMeshVertex` blends the two bone angles about the shared joint. Every
intermediate transform stays a rotation, so nothing squashes. Both ends still
reproduce their own bone exactly, and neutral is still pixel-exact.

Worth one point. Folding is the rest.

### Step 2b — stop the fold. Done.

**Trimmed the mesh to the arm.** `BuildLayeredPuppetMesh` keeps only cells
carrying opaque pixels plus one cell of collar. Against the pre-step-3 arm this
was 504 triangles to 402 and 286 vertices to 235; the current arm gives 336
triangles and 200 vertices, because step 3 changed what the arm owns.

**Widened the blend band away from the bone.** New
`joint_blend_lateral_scale`, set to 1.0 for the arm. Band width is now
`joint_blend_radius + scale * lateral distance`, still clamped to the shorter
bone so the shoulder and wrist stay rigid.

Rotating about the joint by an angle that changes at rate `g` gives a Jacobian
determinant of `1 - g*d` at lateral distance `d`. Folding is therefore driven by
lateral offset, not by position along the chain — the sleeve's far side crosses
over while the axis is safe. Widening the band out there lowers `g` exactly where
`d` is large and leaves the on-axis band untouched.

The threshold is `scale > bend/2`. The hardest pose bends 62 degrees, giving
0.54; measured, folds hit zero at 0.5. 1.0 was chosen for headroom to 114
degrees, not from the area column.

Result: zero folds in every pose, neutral still pixel-exact, every pose still one
connected component.

**Area got worse, and that is correct.** Measured at the time, contact fell from
88.1% to 81.9% and airborne from 87.8% to 81.0%. The picture improved: at scale 0
the elbow has a notch bitten out of it and the upper sleeve reads as a detached
blob; at 1.0 it is a smooth continuous bend. A bent tube covers less area than a
straight one, so the 95% area target was measuring rigidity, not quality.

Those percentages moved again in steps 3 and 4 as the arm's ownership changed.
Read them from the manifest, not from here.

Retire the area target. The deformation gates are zero folds, one component,
exact neutral, and human review at 48px.

The two arms are near-identical in the 48px composite. That matches the estimate
in "Why not ARAP": deformation is worth about two pixels here.

### Step 3 — ownership from the layer alpha. Done.

`BuildLayeredPuppetOwnershipMask` derives what a skinned part owns: a pixel is
owned when the grown candidate alpha paints it, the source paints it, and it is
within reach of the bone chain. Reach interpolates along the chain:

```
reach(t) = start + t * (end - start)
```

`t` is 0 at the shoulder, 1 at the hand. Wide at the shoulder because a sleeve
really is part of the coat; tight at the hand because the limb is free.

Spec: `source_from_semantic_reach: {start, end, grow}` on the arm, and
`source_exclude_parts: ["front_arm"]` on the body. The body now subtracts exactly
what the arm claimed instead of repeating a second outline, which is what let the
two drift apart in the first place. Set to `{22.0, 10.0, 1}`.
`require_part_ownership_isolation` is on and passing.

Results at the time: orphans 105 to 0, backfill 876 to 598, and the tail no
longer swings with the arm. Neutral still pixel-exact, all 18,974 pixels singly
owned. Correcting the joints afterwards pushed orphans back to 149 — see "Known
failing gates".

The tail confirmed the reach idea rather than the alpha alone. `handwear-l` stops
at x186 and the tail sits at x186-200, so alpha alone already drops it — but the
first reach I tried, `end` 12, cut through where the hand and tail root touch and
severed the tail into an 83 px floating island. The orphan gate caught it. 10 is
the largest value that passes and matches the hand's radius from the wrist.

Two different faults, two different guards: the gate catches a bad candidate
alpha, the reach catches a plausible alpha that includes the wrong body part.

One bug this exposed, worth remembering: subtracting the mask from the ownership
record but not from the rendered artwork made every gate report a clean
decomposition over a composite that still drew the ghost arm. Ownership records
and rendered pixels have to lose the same region.

### Step 3b — correct the shoulder joint. Done, gates now failing.

The rig had one `shoulder` at the body midline (x131, y129) shared by both arms.
Two errors: it pivoted the right arm at the spine rather than its socket, and it
sat 23 px inside a sleeve whose upper-arm bone is only 33 px long. The sleeve cap
therefore swept an arc instead of staying seated — the "arm left behind" look.

Added `shoulder_b` at (156,113) and rebound `upper_arm_b` to it.

That invalidated the poses, which were absolute coordinates authored against the
old pivot: the upper-arm bone changed length per pose, stretching 32 px to 60 px
in passing. Each elbow was re-solved by two-bone IK keeping the authored hand
target. Neutral solves back to its original values exactly.

**Passing did not fit.** Its hand was 92.7 px from the real shoulder against a
65.4 px arm, so it was clamped to reach. That pose was only possible because the
pivot was at the midline, which lets an arm swing further across the body than a
shoulder can. It needs re-authoring.

### Known failing gates

The render fails two gates and this is deliberate. Do not silence them.

```
body_visible retains 149 orphan pixels inside front_arm
front_arm airborne folds 4 triangles over artwork
```

Neither is tunable with the knobs that exist:

- **Reach.** Below 20 leaves 149 px of sleeve edge behind; above 20 severs the
  tail (82 px). Nothing in between works. The sleeve's outer edge and the tail
  sit at the same distance from the corrected bone, so a symmetric radial reach
  cannot separate them.
- **Lateral blend scale.** 1.0 gives 4 folds, 1.5 gives 3, 2.0 gives 4, 2.5 gives
  6. Not monotonic, never zero.

Both mechanisms were tuned against the old chain, which ran diagonally across the
chest. With the chain where it belongs they need rethinking, not retuning. This
is the strongest evidence so far for the MLS fallback: the hand-rolled distance
heuristics turned out to be brittle to the rig they sit on.

The tail also needs to be its own part. It connects to the body through the
region the hand occupies, so any clean cut of the hand severs it.

### Step 4 — backfill. Done.

**"Stop clipping the underpaint" turned out to do nothing.** Rendering with
`clip_to_source_alpha` off leaves the uncovered count at exactly 745, unchanged.
Ownership is already intersected with the source alpha, so the clip never removed
any of it — `topwear` simply does not paint there. That half of the plan is
withdrawn.

So the whole gap is stretching. `StretchLayeredPuppetBackfill` grows a static
layer outward into the region a moving part will expose, copying each pixel from
the nearest already-painted one by breadth-first distance. Spec:
`stretch_to_cover_parts: {parts, distance, edge_inset}` on the underpaint.

`edge_inset` earns its place. Seeding from the layer's outermost ring propagates
the coat's dark contour into the hole and produces a black slab where coat should
be. Eroding the seed set by 2 first makes the fill carry body colour, and the
belt band continues outward instead of stopping. The real contour is never
overwritten, because only transparent pixels are filled.

Results: backfill uncovered 745 to 0, contact interior holes 355 to 194 against a
174 baseline. Neutral still pixel-exact. `require_backfill_coverage` is on.

`filled_pixels` is 745 of a 1,582 px ownership region, so 47% of what sits behind
the arm is invented rather than painted. That number is reported every run and is
the honest signal: it should fall when real artwork arrives, not be tuned away.

### Step 4b — graded attachment at the shoulder, only if a seam shows

Ownership above stays binary, so the compositor and the exclusive-ownership gate
are unchanged. If the shoulder then shows a crack or a hard cut, the fix is to
let body pixels near the shoulder partially follow the arm, using the same
`reach(t)` falloff as a weight rather than a threshold.

Do not build this pre-emptively. It breaks "every pixel has exactly one owner",
which is the gate that has caught the most real bugs so far.

### Step 4c — relationship-aware stretch. Rejected.

This candidate separated coat silhouette, shoulder relationship, and shadow,
but still stretched 592 pixels into the generated coat. User review showed the
result had more coat than the intended image. The diagnosis that the coat was
cut away was backwards.

Keep the useful infrastructure from this step:

- reachable bent passing pose with invariant 32.249/33.121-pixel bone lengths;
- arm-hidden and moved-part tint evidence;
- separate shadow rendering and shadow tint;
- attachment and backfill masks.

Do not keep the stretched coat as the target.

### Step 4d — immutable generated coat. Candidate ready.

`mouse_immutable_coat_v1.json` removes `stretch_to_cover_parts` and marks
`body_underpaint` as an immutable semantic layer. The proof tool retains the
restored source layer separately and fails if processing changes RGB, adds
alpha, or removes alpha.

Measured result:

- source and final coat digest:
  `0400b5084a83957f727c2292d52f481f1af07e29331c628b07c69c2561351fc4`;
- changed coat pixels: 0;
- alpha additions: 0;
- alpha removals: 0;
- neutral composite difference: 0;
- passing shadow changes 288 displayed pixels but does not mutate stored coat
  RGB or alpha.

The former full-arm backfill report now shows 745 uncovered pixels and is not a
gate. Those pixels lie outside the approved coat silhouette and may correctly
reveal background. Contact has 355 interior holes against 174 neutral; passing
has 177. The unchanged 149-orphan and four-airborne-fold failures remain.

### Step 5 — review at 48px. Immutable candidate pending.

Review `semantic-arm-immutable-coat-v1` in this order:

1. `immutable-sources/body_underpaint.png`: the accepted generated coat.
2. `parts/body_underpaint.png`: must be byte-identical.
3. `diagnostics/working/passing-front_arm-hidden.png`: coat plus static body,
   with no invented sleeve.
4. `diagnostics/working/passing-front_arm-tint.png`: one bent moving arm.
5. `diagnostics/working/passing-shadow-tint.png`: shadow only.
6. `frames/passing.png` and `proof.png`: native 48px result.

Do not reintroduce coat stretching to make the old backfill number green.


## If step 5 fails

**Do not delete the mesh.** It is the rasterizer, it transports texture exactly,
and neutral reproduces pixel-for-pixel. Replace what decides where its vertices
go, not the mesh itself.

1. **MLS rigid deformation** (Schaefer et al. 2006) driving the mesh vertices.
   Smooth from a few handles, so it cannot fold, and the existing rasterizer and
   neutral proof both survive. ~60 lines. Caveat: smooth everywhere means it
   cannot hold a hard edge such as a cuff, and it ignores the arm's outline.
2. **Bounded biharmonic weights** (Jacobson et al. 2011). Solves the weights once
   per part from the part's own shape. Where step 2b's weight change leads.
3. **ARAP**, if both fail. Use Igarashi et al. 2005, not Sorkine-Alexa: two
   prefactored least-squares solves, no iteration, so it stays deterministic.
4. **Hand-drawn elbow patches.** Four of them is cheaper than any of the above
   and certainly works.

## How to review this without getting it wrong

Do not identify parts by eye. At 48 px, upscaled, the moved arm and the static
one are not reliably distinguishable, and four separate wrong calls in this
document's history came from stating a visual conclusion before measuring it.

Tint the layer instead. Composite the pose, then paint every pixel that
`part-poses/<part>/<pose>.png` marks opaque in a flat colour. That answers "which
of these is the thing that moved" with no judgement involved. Bounding boxes from
the part poses do the same job in numbers.

The four wrong calls, for the pattern rather than the blame: retained area was
predicted to rise and fell; interior holes were dismissed on a count read once
and never re-read; "stop clipping the underpaint" was planned and does nothing;
and the arm draped across the chest was called the static one when it is the
moved one. Every one was a conclusion formed before the measurement existed.

## Caveats

- Retained area is not a quality measure. Removing every fold lowered it by six
  points while the elbow visibly improved. Rigid sections score well on it.
- Widening `joint_blend_radius` uniformly reduces folding but smears the sleeve
  into a rubber tube. `joint_blend_lateral_scale` widens only away from the bone,
  which is why the on-axis silhouette is unchanged.
- `joint_blend_lateral_scale` must exceed half the largest bend in radians. 1.0
  covers 114 degrees. A harder pose or a thicker part needs it raised, and the
  fold gate will say so.
- Stretching repeats or distorts nearby texture. Fine for the coat, which is flat
  and vertical. It will not invent a belt buckle or a boot.
- See-through returned nothing for `bottomwear`, `legwear`, and `tail`, and
  audit found it also covers neither the mouse's ears nor the brown appendage
  beside the right paw. Its nine kept layers miss 21% of the source. Legs, tail
  and ears stay blocked on hand-drawn artwork no matter how this ends.
  `raw/head.png` does hold the real face and was dropped by mistake; recover it
  if a head part is ever wanted.
- Wings are a rig question with no artwork behind it. A wing needs its own chain
  anchored at a body joint, which the spec can already express by adding joints
  and bones — nothing in the renderer prevents it. The blocker is that a profile
  mouse has no wing pixels to bind, same as legs and tail. Worth testing once
  some part exists that is not on the arm or leg chain; not worth rig work now.
- The interior-hole gate cannot be turned on as written: the source art has 174
  enclosed gaps. Compare against the neutral count or drop it.

## Non-goals

No new generation or training. No second arm, leg, tail, or run cycle until this
resolves. No changes to the Blueprint, six state keys, timings, playback, or the
32x64 collider. No Python or numerical dependency in the engine.

## After a pass

Apply to `handwear-r`. Split footwear into two boots. Get leg and tail artwork.
Bind each leg to hip/knee/foot under the same gates. Full-character acceptance,
then the six replacement clips, then unblock Runtime M4.
