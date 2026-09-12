# Full-resolution painted run

**Latest user verdict (2026-09-12): “we are still pretty far off the mark here.”**
The [proportion attempt](proportions-v3/review.html) changed leg width and arm
size but did not achieve a satisfactory painted run. [Results and limitations](proportions-README.md),
[recorded assessment](proportions-v3/assessment.json). The technical results
below do not establish visual success.

The original full-resolution baseline is [mapped-v3/review.html](mapped-v3/review.html): a
**512×512, twelve-frame, 12fps** run using complete painted parts. The 96px
view is secondary. This is an experiment master, not a selected shipping size
or accepted final animation.

## Result

- [Full-resolution transparent APNG](mapped-v3/frames-512.png)
- [Legs-only APNG](mapped-v3/legs-512.png)
- [Twelve full-size frames](mapped-v3/frames-sheet.png)
- [Contact and recovery anatomy comparison](mapped-v3/anatomy-comparison.png)
- [Measurements](mapped-v3/summary.json) and [semantic registration](mapped-v3/registration.json)

The new paintings preserve full cloth folds, boot volume, cuffs and leather
detail. No material snippets, procedural gradients or C++ silhouette alpha
replace these drawings. Twelve independent paintings supply both sides using
the corresponding opposite phase; each of the 24 legs binds to its own original
C++ pose. Five separate upper-body paintings supply actual new fur and cloth
detail, rather than merely enlarging the old 256px source. Their design changes
are visible and remain unaccepted.

C++ retains the traced hip/knee/ankle/toe pins, derives the authored heel from
the previous sole direction and places the ankle at 22% of the sole's length.
The rejected frame-7 far boot was at 49.57%. This repairs the inverted-T control
and also corrects two airborne heel estimates with negative ankle projection.
It is an authored anatomy rule, not a newly discovered source landmark. The
previous recovery profile was a starting point; its heel changes are visible.
Frame 1 uses a 4px leading-heel drop before this construction, compared with
the previous 3px. All subsequent grounding moves the entire character.

The corrected contact soles meet the C++ row-428 pins. There are no leg pixels
below row 428 at alpha thresholds 32, 128 or 224. At threshold 128, flight
frames 6 and 12 have 12px and 14px clearance. Raster coverage sometimes ends
on row 427; that one-pixel edge difference is retained, not cropped away.
There are no inverted mesh cells whose centers sample opaque artwork. The
maximum exact-solve landmark residual is 4.78e-12 raw pixels. Every decoded
frame of the five 512px APNGs matches its corresponding retained PNG.

These checks measure registration and contact, not final art quality. Maximum
leg landmark correction is **19.31 master pixels**; it is not negligible.
Some local deformations are substantial: the largest sampled axis ratio is
4.15 in an arm. Complete-painting reuse preserves drawn detail but does not
guarantee constant volume under deformation.

## Exact inputs and raw evidence

Before all four requests, the conversation displayed the original source,
retained complete-leg appearance references, [all new C++ pins](all-anchors.png),
[all twelve controls](all-poses.png), three exact leg sheets and the separated
upper-body guide. No subsequent generation was needed for registration fixes.

| Request | Exact guide | Raw output | Exact prompt |
|---|---|---|---|
| Phases 1–4 | [guide](legs-1-guide.png) | [1448×1086 raw](legs-1-raw.png) | [prompt](legs-1-prompt.txt) |
| Phases 5–8 | [guide](legs-2-guide.png) | [1448×1086 raw](legs-2-raw.png) | [prompt](legs-2-prompt.txt) |
| Phases 9–12 | [guide](legs-3-guide.png) | [1448×1086 raw](legs-3-raw.png) | [prompt](legs-3-prompt.txt) |
| Five upper parts | [guide](upper-guide.png) | [1536×1024 raw](upper-raw.png) | [prompt](upper-prompt.txt) |

The built-in imagegen tool handled all four requests, about **97.73 seconds**
of tool wall time in total. Exact model revision and seed were not exposed.
[Request log](request-log.json) records source receipts, inputs and timings.
[Input manifest](manifest.json) hashes the inputs and all five previous raw
images. [Final manifest](mapped-v3/manifest.json) hashes the new raw outputs,
landmarks and implementation. Raw files are byte-for-byte copies of the tool
outputs; the original tool files also remain in place.

The raw leg comparison uses only the fixed full-sheet coordinate transform.
It combines those legs with registered upper parts. The reused far painting
is displayed at its original near-pose coordinates, exposing the difference
that registration imposes. It is not a claim that the model generated that
far pose. The raw upper sheet remains separately available in full.

## Ownership and mapping

`scripts/sprite_sequence_geometry.cc --painted` owns anatomical construction,
sole samples, contact, original pose, view schedule and cyclic coat response.
The original profile and atlas modes still reproduce their retained geometry.
The extra outsole sample fields exist only in the new painted mode.

`painted-landmarks-v2.json` records visual estimates on native raw sheets with
4px uncertainty. It distinguishes observed image positions from rig targets.
The Python adapter extracts fixed cells, removes the white matte into separate
RGBA assets and measures painted outsole edges. It never fits per-part bounds.

`scripts/sprite_painted_registration.cc` solves inverse thin-plate registration
from explicit source/target pairs and emits a 4px grid of sampling quads.
Two-pin head/body/tail registrations use similarities to keep their shape
stable. The adapter samples native paintings at 512px, applies the C++ lower
coat response, follows the document's draw order and applies one C++ global
translation. Seven extra observed sole points constrain support boots to C++
sole samples. No guide-mask clipping or independent foot translation is used.

Each final frame retains its exact mapping input and compressed C++ sampling
mesh in `mapped-v3/maps/`. Complete native extracted parts, observed landmark
overlays, ungrounded part PNGs and final composites are separate. The final
render also retains source snapshots. Later C++ lint/failure-path cleanup does
not change this valid sequence's output.

## Retained iterations

- `mapped-v1/`: first full-detail assembly. Wrist observations were inside the
  paws and stretched them; support soles penetrated by up to four master pixels.
- `mapped-v2/`: corrected wrist and outsole observations. All frames and APNGs
  completed, but the final manifest writer failed when given a relative
  landmark path. This directory is retained as incomplete evidence.
- `mapped-v3/`: same corrected artwork with the path handling fixed, complete
  manifests, source snapshots, decoded-frame checks and interactive review.

## Reproduction without a provider or engine build

From the repository root, use a new output directory:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic scripts/sprite_sequence_geometry.cc -o /tmp/zebes-sprite-sequence-geometry
c++ -std=c++20 -Wall -Wextra -Werror -pedantic scripts/sprite_painted_registration.cc -o /tmp/zebes-sprite-painted-registration
build/tileset-venv/bin/python -m scripts.render_sprite_painted --registration /tmp/zebes-sprite-painted-registration --inputs experiments/sprite_sequence/evidence/painted-master-v1 --output /tmp/zebes-painted-reproduction --landmarks experiments/sprite_sequence/painted-landmarks-v2.json --geometry experiments/sprite_sequence/evidence/painted-master-v1/geometry-registration-v2.json
build/tileset-venv/bin/python -m scripts.review_sprite_painted --inputs experiments/sprite_sequence/evidence/painted-master-v1 --output /tmp/zebes-painted-reproduction --geometry experiments/sprite_sequence/evidence/painted-master-v1/geometry-registration-v2.json
build/tileset-venv/bin/python -m unittest tests.sprite_painted_test tests.sprite_boot_atlas_test tests.sprite_sequence_test
```

Dependencies are the existing Pillow/NumPy environment and a C++20 compiler.
The mapped outputs can reproduce outside the repository; HTML comparison
links assume the retained evidence layout. No CMake target, `src/` dependency,
provider call in the scripts, production import or format migration was added.
The lint wrapper excludes `scripts/` translation units, so both standalone
programs were checked directly with the repository's clang-tidy configuration.

## Remaining visual work

Judge the 512px cycle before adding more poses. Trouser folds change between
paintings, the sole-facing view is subtle, arms can squash/stretch locally,
and the forward paw approaches the muzzle. The new head and coat are an
interpretation of the source, not identical source pixels. The lower-coat
response is still a simple cyclic compression rather than cloth collision.
The run is now reviewable at master resolution; final animation acceptance,
shipping resolution and production integration remain open.
