# Reusable boot atlas candidate

[Open the current review](evidence/boot-atlas-v2/review.html), or use
[the local review server](http://127.0.0.1:8766/experiments/sprite_sequence/evidence/boot-atlas-v2/review.html).
The original recovery profile is accepted as a starting point. **User review
now finds the atlas legs inferior to the generated appearance and rejects
`reference_07/far`'s overlong rear heel.** The 96px motion is relatively smooth;
the next target is full-resolution painted animation. See the
[recorded feedback and conclusions](evidence/user-review-full-resolution/README.md).
The atlas remains a control/reuse proof, not accepted final artwork.

## Structural contract

`scripts/sprite_sequence_geometry.cc --atlas` takes the original joint rows
plus explicit view, contact and heel-offset annotations. It emits every ankle,
cuff, heel, toe and contact anchor, rounded surface outlines, texture bases and
whole-character translation. The base profile mode still reproduces the saved
profile-v2 geometry exactly; the original five raw image outputs are unchanged.

All 24 ankle/toe pins and both recovery heel triangles remain unchanged in the
untranslated pose. Shaft centerlines follow the shins. Shaft geometry is cut
at its own sole plane so leather cannot extend beneath the boot. The renderer
unions shaft and foot before outlining; no interior line divides the ankle.

One 512×128 RGB atlas contains four 128px tiles: trousers, shaft leather,
profile foot and sole-visible foot. The same retained donor rectangles supply
cloth and leather color. Surface-local gradients, folds, cuff trim, welt and
sole treads are authored once in the bake recipe. C++ supplies the actual
contours and mapping, so the tiles support changing shaft/foot articulation.
This is a parameterized 2D view atlas, not a 3D camera/depth model or independently
generated per-frame art. Far-leg shading distinguishes limb ownership.

## Contacts and coat

Ten support frames land their named contact exactly on working row 214. Flat
phases set the uncertain heel height to the traced toe height; they preserve
the ankle, toe and limb directions. Pose 1's estimated leading heel is lowered
by an explicit 3px: its original trailing toe was 2.29px below that heel.
This new estimate needs review and is not a source measurement. All annotations
live in [boot-atlas-v1.json](boot-atlas-v1.json).

The whole character receives one translation. No boot moves independently.
Flight interpolates neighboring support translations and caps that shift to
retain at least 6 working pixels of clearance. Both boots and every surface
are checked for ground penetration. Contradictory contacts fail before any
geometry JSON is emitted. The [intermediate atlas v1](evidence/boot-atlas-v1/review.html)
retains the first candidate: it left flight at the old body height, adding an
unnecessary upward jump. V2 corrects that whole-body transition.

Coat response is a bounded compression below the belt, driven by leg recovery
height with a cyclic three-tap lag. C++ emits the deformation; the rasterizer
preserves every torso pixel above the attachment row, including alpha. The
review keeps an identical-geometry fixed-coat comparison. This makes recovery
more visible but does not implement garment collision, separate front/back
panels, or a cape.

## Reuse and review

`--atlas-flex` rotates each free shin another 12 degrees about its knee, keeping
segment lengths and the attached foot triangle. Support geometry and the view
schedule remain unchanged. This second twelve-frame cycle uses the exact same
PNG atlas, with no new artwork or generation requests. It proves reuse under
changed articulation; it is not an accepted alternate motion.

The review includes 48/96/256px synchronized playback, frame selection, leg-only
views, anchors, ground contacts, and fixed-coordinate recovery comparisons.
96px retains more of the boot design and recovery separation than 48px. Neither
resolution is selected for production. The inherited head variation and small
upper-body source fragments remain visible; the candidate is not final polish.

## Reproduction and verification

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic scripts/sprite_sequence_geometry.cc -o /tmp/zebes-sprite-sequence-geometry
build/tileset-venv/bin/python -m scripts.prepare_sprite_boot_atlas --geometry /tmp/zebes-sprite-sequence-geometry --output /tmp/zebes-boot-atlas-review
build/tileset-venv/bin/python -m unittest tests.sprite_sequence_test tests.sprite_boot_atlas_test
```

Choose a fresh output directory; the preparer refuses to overwrite evidence.
The local HTML needs the sibling profile experiment for its historical link;
the generated atlas, images, animations and JSON work independently. No engine
build, provider calls or ignored output directory is required. This remains
outside the production build and serialized asset formats.

Fourteen focused tests cover old-profile reproduction, invariant pins and
recovery heels, shaft alignment, connected alpha, exact C++ surface masks,
contact/flight placement, alternate articulation, cyclic coat invariance,
fixed torso pixels and malformed/contradictory input rejection. The complete
affected test modules pass. C++ builds with warnings as errors and direct
clang-tidy passes; the repository wrapper rejects `scripts/` translation units.
All 216 decoded frames in the eighteen APNGs match their saved individual
frames. All five raw output hashes still match their original result manifest.
Browser checks exercised playback, frame selection, 48/96px, legs and anchors.
