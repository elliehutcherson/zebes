# Sprite sequence: profile control and shared appearance

**Superseded on 2026-09-12 by the [accepted 3D mouse](../../docs/mouse-3d-plan.md).**
The results and reproduction below remain evidence; their pending gates are
not current work. [Historical plan and research](../../docs/sprite-sequence-milestone.md).
The user accepted the new frame-10 recovery profile **as a starting point**.
Final artwork and the full animation are not accepted.

**Latest user verdict (2026-09-12): “we are still pretty far off the mark here.”**
The full-resolution painted run remains unsatisfactory; the visual milestone
has not been met. [Recorded assessment](evidence/painted-master-v1/proportions-v3/assessment.json).

**Latest unsuccessful result:** [painted-run proportion attempt](evidence/painted-master-v1/proportions-v3/review.html),
512×512, twelve frames at 12fps, using complete native painted parts and C++
semantic registration. [Method, raw requests, measurements and reproduction](evidence/painted-master-v1/README.md).
[Measured changes, limitations and retained before/after](evidence/painted-master-v1/proportions-README.md).

The [reusable boot atlas, v2](evidence/boot-atlas-v2/review.html) remains a
technical comparison. [Atlas method and reproduction](BOOT_ATLAS.md).
The [earlier full-resolution direction review](evidence/user-review-full-resolution/README.md) prefers
the generated isolated-leg appearance, rejects the atlas's frame-7 far heel,
and established the direction toward full-resolution painted quality.
The profile/shared-sheet experiment below is retained unchanged as evidence.

Open [the interactive comparison](evidence/profile-v2/results/review.html).
It has synchronized frame scrubbing, 48/96/256px rendering and **Legs only**.
The coat hides much of the folded recovery leg, so inspect the leg-only view.
The local review can also be opened at
[localhost](http://127.0.0.1:8766/experiments/sprite_sequence/evidence/profile-v2/results/review.html)
while the review server is running.

## What changed

The old boot proxy had its own derived ankle, 3.21–18.09 pixels from the posed
rig ankle on a 256px canvas, and a 35-degree oblique camera. Current limb
directions agree with the retained source trace to floating-point precision;
the audit found no evidence for another global leg-side swap.

The new standalone C++ geometry preserves ankle and toe, transfers an explicit
heel triangle and aligns the shaft to the shin. Python rasterizes the polygons
and combines them with retained C++ upper-body layers. All 24 legs are connected
and preserve the control pins. New heel coordinates are authored estimates
with ±5 source-pixel uncertainty. All-profile is an explicit ablation: it does
not provide the desired sole-visible contact view.

## Results

| Route | Result | Use |
|---|---|---|
| Single frame-10 leg on its original sheet cell | 15.94px upward centroid drift; IoU 0.211 | Negative registration control |
| Six consecutive near-leg poses | Frame 10 drift `(0.72,-2.85)`px; coherent cloth/leather; areas 1.17–1.25× controls | Promising appearance screening, insufficient geometry |
| Expanded four sheets, 24 leg poses | IoU 0.105–0.835; centroid drift up to 19.78px; areas 1.17–1.60× controls | Complete raw draft; rejected as reliable pose-following assets |
| Two reusable material samples on C++ polygons | All 24 silhouettes identical to controls; no per-pose generation | Deterministic feasibility proof; angular draft art |

Five built-in imagegen calls completed. Tool wall times were 20.6, 18.2, 20.3,
20.3 and 21.1 seconds (about 100.5 seconds total). No ComfyUI/3090 work ran.
The API did not expose a controlled seed or exact model revision. The single
versus sheet comparison is an exploratory screen, not an estimate of causal
improvement from shared attention.

Raw outputs are unchanged 1536×1024 images. Review applies one uniform whole
canvas scale to 1056×704, splits six fixed 352px cells and inverts the fixed
176px world crop `[48,80,224,256]`. There is no per-leg fitting, warping or
silhouette clipping in the raw route. The minimum RGB channel below 220
defines foreground; thresholds 180/200/240 are also recorded. These are
silhouette/registration measurements, not anatomical landmark scores.

The material route deliberately imposes the C++ control alpha. Its exact shape
is **not** evidence that the image generator followed a pose. One cloth region
and one leather region are sampled from the generated standing-leg sheet and
mapped in limb-local coordinates. This is a lower-detail material proof, not
a complete semantic boot-view atlas or approved style. It demonstrates that
the same appearance samples can animate without further model calls.

## Evidence

- [Source/heel overlays](evidence/profile-v2/source-landmarks.png),
  [all mouse overlays](evidence/profile-v2/pose-overlays.png),
  [ankle comparison](evidence/profile-v2/binding-comparison.png),
  [numeric binding audit](evidence/profile-v2/binding-audit.json).
- [Frame-10 comparison](evidence/profile-v2/results/recovery-detail.png),
  [first six leg comparisons](evidence/profile-v2/results/sheet-comparison.png),
  [remaining 18](evidence/profile-v2/results/completion-comparison.png).
- [Raw twelve-frame sheet](evidence/profile-v2/results/hybrid-cycle.png),
  [96px raw animation](evidence/profile-v2/results/hybrid-96.png),
  [material-reuse sheet](evidence/profile-v2/results/material-cycle.png),
  [96px material animation](evidence/profile-v2/results/material-96.png).
- [Inputs, hashes and cell mapping](evidence/profile-v2/manifest.json),
  [result measurements](evidence/profile-v2/results/summary.json),
  [manual assessment](evidence/profile-v2/assessment.json),
  [request log](evidence/profile-v2/request-log.json).
- Exact prompts: [six-pose pilot](sheet-prompt.txt),
  [single-leg control](single-prompt.txt), [completion](completion-prompt.txt).
  They were submitted through the built-in imagegen tool. All five raw outputs
  are retained in `evidence/profile-v2/` and referenced in the request log.

## Reproduce without a model or engine build

From the repository root:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic scripts/sprite_sequence_geometry.cc -o /tmp/zebes-sprite-sequence-geometry
build/tileset-venv/bin/python -m scripts.prepare_sprite_sequence --geometry /tmp/zebes-sprite-sequence-geometry --output /tmp/zebes-sprite-sequence-review
build/tileset-venv/bin/python -m unittest tests.sprite_sequence_test
```

Use a fresh output directory: the preparer refuses to replace evidence.
Its only Python dependency is Pillow. The named virtualenv is the project's
existing image-tool environment; an ordinary Python with Pillow also works.

`inputs/baseline-v1/` retains decoded C++ part frames and original whole frames
from the accepted mouse document. No ignored `out/` directory or engine build
is needed. The existing source drawing, trace, puppet document and earlier v2
boot manifest remain referenced as immutable sibling experiment inputs; their
hashes are retained. No CMake target or `src/` dependency was added.

The original two pilot inputs were prepared before copying the baseline into
this experiment. `pilot-generation-manifest.json` retains that initial
provenance. After the path-only baseline relocation and additional sheet
preparation, byte comparison confirmed the pilot sheet and single-leg inputs
were unchanged. The current manifest records the portable baseline paths.

To rerun the raw-output reviewer, copy the five retained `*-raw.png` files,
the three prompt files and the input manifest into the same experiment layout,
then run:

```sh
build/tileset-venv/bin/python -m scripts.review_sprite_sequence --inputs experiments/sprite_sequence/evidence/profile-v2 --output /tmp/zebes-sprite-output-review --complete
```

The output review HTML uses sibling input paths, so use an adjacent fresh output
directory for browser playback or the retained `results/review.html`. PNGs,
APNGs and measurements work in any output directory. The material recipe binds
its two sample rectangles to the donor image's exact SHA-256 and fails if it
changes. The experiment makes no provider calls from code.

## Verification and remaining work

Seven focused tests exercise every pose's invariant ankle/toe and shaft axis,
connected alpha, exact material-route silhouettes, recovery feet on both legs,
known triangle rotation, malformed/duplicate input rejection without partial
output, and fixed-registration scoring. C++ compiles with warnings as errors;
direct clang-tidy passes. The repository lint wrapper refuses `scripts/`
translation units, so it was followed by direct analysis with the same config.

The [rounded boot atlas](BOOT_ATLAS.md), support contacts, sole-visible leading
view and a first coat response are now implemented. Review 48px and 96px before
settling the logical asset resolution; visual acceptance remains open.
The complete raw sequence demonstrates appearance, not reliable pose control.
The material proof demonstrates control, not final artwork. Keep both truths
visible while improving the atlas.
