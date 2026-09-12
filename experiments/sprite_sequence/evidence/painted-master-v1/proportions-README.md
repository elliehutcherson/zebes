# Painted-run proportion correction: unsatisfactory result

**User review, 2026-09-12:** after seeing `proportions-v3`, the user said,
**“we are still pretty far off the mark here.”** The assembled animation is
not accepted as a satisfactory painted run. This is an unmet visual milestone,
not a result that only needs minor polish or is awaiting its first review.
[Recorded assessment and hashes of the reviewed artifacts](proportions-v3/assessment.json).

The prior assistant's statements that the proportions were “corrected” and the
correction was “complete” overstated the result. They described implementation
and measured changes, not achievement of the user's intended animation quality.
The technical findings below remain valid within their stated limits.

The user reviewed `mapped-v3` on 2026-09-12 and found the legs too narrow and
the back arm far too large. These are proportion defects, not minor polish.
The retained [before/after review](proportions-v3/review.html) shows the attempted
correction through the same twelve 512px frames at 12fps. Its original labels
predate the latest user verdict; the assessment above supersedes them.

## Current result

- [Reviewed, unsatisfactory transparent APNG](proportions-v3/frames-512.png)
- [Synchronized before/after APNG](proportions-v3/before-after-512.png)
- [All twelve comparisons](proportions-v3/proportions-comparison.png)
- [Proportion measurements](proportions-v3/proportions.json)
- [Contact, registration and decoded-frame checks](proportions-v3/summary.json)

Thigh midpoint widths average 30% wider and calf midpoint widths average 40%
wider across both legs and twelve phases. These are connected alpha>=128
cross-section measurements; tightly folded parts can intersect adjacent
segments, so the largest ratios should not be read as isolated calf diameter.
The back arm's painted area is 41–46% smaller. Its C++/document identity is
`far_arm`; its on-screen side changes through the run.

The head, body, near arm and tail part PNGs are pixel-identical to `mapped-v3`
for all twelve frames. Original joint targets, heel/toe pins, draw order and
whole-character grounding are unchanged. No leg pixels fall below row 428
at alpha thresholds 32, 128 or 224, and no mesh cells whose centers sample
opaque artwork are inverted. All five 512px APNGs decode to their retained
individual PNG frames. Exact-solve semantic residual is below 6e-12 raw pixels.
Local shape distortion still exists; the maximum sampled axis ratio is 6.75.
These properties establish a functioning render/control pipeline. They do not
establish natural anatomy, appropriate volume, preserved painted quality under
deformation or convincing motion. The user still finds the result far off target.

## Why the previous mapping failed

Joint pins constrained attachment but left transverse width unconstrained.
The old back-arm layer occupied roughly twice the other arm's area. Its
compact raw upper-arm drawing was stretched strongly to span the target joints.
Trying to squeeze that warped painting further produced visible swirls.

The corrected back arm instead reuses the complete `near_arm` raw painting,
registered to the original **far-arm** shoulder/elbow/wrist/paw targets. This
changes the appearance donor, not limb identity or pose. The original far-arm
raw cell remains intact as evidence. No new artwork request ran.

`sprite_painted_registration --proportions` applies a C++ transverse expansion
field along hip–knee, knee–cuff and cuff–ankle. Midsection scale parameters are
1.45, 1.55 and 1.2, tapering toward segment endpoints and away from the limb.
The field is stationary at all original semantic pins. Sixteen midpoint
integration steps evaluate its inverse before the original registration maps
directly into native raw pixels. No already-warped bitmap is enlarged or
painted over. Each frame retains the resulting sampling mesh and width controls.

## Retained attempts

- `proportions-v1`: displaced spline rails widened the legs but folded some
  recovery textures; squeezing the old back-arm drawing produced swirls.
  The audit found 88 inverted cells over sampled artwork. Rejected.
- `proportions-v2`: complete sleeve reuse corrected the arm proportions, but
  the spline widening still had the same folded-knee mechanism. Rejected.
- `proportions-v3`: sleeve reuse plus smooth inverse width flow. The full-cycle
  audit finds zero inverted sampled-art cells. Subsequently judged by the user
  to remain far off the mark; not accepted as a satisfactory animation.

All prior raw images and mapping iterations remain unchanged. This correction
made zero provider calls and added no production import, CMake target or runtime
change. It remains an isolated experiment.

## Reproduce

Use a fresh sibling output directory so the comparison can find `mapped-v3`:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic scripts/sprite_painted_registration.cc -o /tmp/zebes-sprite-painted-proportions
build/tileset-venv/bin/python -m scripts.render_sprite_painted --registration /tmp/zebes-sprite-painted-proportions --inputs experiments/sprite_sequence/evidence/painted-master-v1 --output experiments/sprite_sequence/evidence/painted-master-v1/proportions-reproduction --landmarks experiments/sprite_sequence/painted-landmarks-v2.json --geometry experiments/sprite_sequence/evidence/painted-master-v1/geometry-registration-v2.json --proportions
build/tileset-venv/bin/python -m scripts.review_sprite_painted --inputs experiments/sprite_sequence/evidence/painted-master-v1 --output experiments/sprite_sequence/evidence/painted-master-v1/proportions-reproduction --geometry experiments/sprite_sequence/evidence/painted-master-v1/geometry-registration-v2.json
build/tileset-venv/bin/python -m unittest tests.sprite_painted_test tests.sprite_boot_atlas_test tests.sprite_sequence_test
```

All 25 focused tests pass. The edited C++ translation unit passes clang-tidy
using the repository configuration; `scripts/lint.sh` excludes `scripts/`
translation units, so the same documented direct check was used. Whitespace
checks pass. Browser review exercised master-size before/after, frame selection
and independent limb views.

## What the result does and does not establish

A 512px twelve-frame artifact exists; complete paintings can be reused with
C++ pose/contact control; width changes and donor replacement are reproducible;
the raw evidence survives. A satisfactory full-resolution painted run has not
been achieved. Neither the width percentages nor the 25 passing tests are an
art-quality score or an accepted target for the next iteration.

The latest user feedback does not enumerate every remaining defect. Fold
continuity, subtle sole visibility, arm deformation, coat response and
paw/muzzle spacing are previously recorded assistant observations, not a
complete diagnosis from the user. Do not invent more specific acceptance or
rejection of individual raw parts from the overall verdict.

Any next attempt should begin with a full-size visual comparison against the
source character and preferred complete leg paintings, reassessing overall
silhouette, proportions and motion before choosing a mechanism. Another width
multiplier or more frames is not an established solution. Preserve C++ geometry
authority and all raw evidence; final art, shipping resolution and production
integration remain unresolved. This documentation turn ran no new generation,
rendering, mapping correction or production import.
