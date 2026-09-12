> Archived 2026-09-12 after the user accepted the storybook 3D mouse and
> selected its workflow as the plan of record. Statements below about current
> work, next steps or pending acceptance describe the earlier state.
> See the [current plan of record](../mouse-3d-plan.md).

# Sprite sequence milestone: attached feet and shared appearance

Started 2026-09-10 under the user's renewed authorization to inspect, plan and
run a new experiment autonomously. This resumes the paused work. Prior rejected
outputs remain rejected. The existing mouse and supplied twelve drawings remain
the appearance and motion references respectively.

## Direction

Build a reusable sequence of **complete, anchored parts**, then test appearance
completion across several frames at once. Keep an authored profile/view atlas as
the dependable fallback. The first milestone is a reviewable twelve-frame mouse
with correctly attached, readable recovery boots and a retained comparison of
single-part versus shared-sheet generation. It is not a new engine framework.

## Findings from the fresh audit

`prepare_boot_view_guides.py` transfers a heel from the old painted boot,
replaces its heel-to-toe direction with a hand-authored angle and sometimes
translates the boot to ground independently. `render_boot` then derives an ankle
from that heel, its proportions and camera. It never constrains that ankle to
the posed rig ankle. Recomputing v2 with its retained exact pins gives **3.21 to
18.09 working pixels of ankle disagreement** across the 24 boots. The nominal
canvas is 256px; 18px is about 3.4px at 48px. Frame 10's near boot differs by
5.83px and its far boot by 15.74px. These are control errors, not measurements of
generated anatomy.

The current renderer binds `near_boot` to `ankle_l-toe_l` and `far_boot` to
`ankle_r-toe_r`, consistently with the documented labels. Its rigid transform
uses the selected bone's start and end. The new audit compares every thigh,
shin and ankle-to-toe direction with the original trace. This checks data
transfer, not the correctness of joints estimated beneath clothing. There is
no evidence so far that a fresh global left/right swap would help.

A second concrete mismatch is view: v2's fixed 35-degree camera encodes an
oblique boot, while the rejected recovery phase needs side profile. A 2D
ankle-to-toe vector cannot specify camera-facing surfaces. The old same-color
leg composite also loses limb ownership at crossings. Independent complete
legs retain that ownership without asking a model to infer it.

These facts do not establish which mismatch caused every rejected output.
Do not call low silhouette error a correct heel, boot view or successful run.

## Experiment and sequence

1. **Profile control ablation.** A small standalone C++ program takes explicit
   hip/knee/ankle/toe joints and a source ankle/heel/toe triangle. It transfers
   the heel without changing the target ankle or toe, aligns the boot shaft to
   the shin, and emits deterministic polygons. The Python adapter rasterizes
   these and combines them with existing C++-rendered upper-body layers. All
   twelve source overlays, 24 separate legs and 48/96/256px playbacks are
   retained. Heel estimates have a stated 5px source-space uncertainty.
2. **Shared-sheet pilot.** Show exact inputs before generation. Compare one
   recovery leg in a fixed cell with that identical cell among six consecutive
   near-leg poses (7–12). Both use the same 3:2 canvas and source appearance
   reference. Two built-in imagegen requests initially. The empty-cell control
   keeps the recovery leg's pixel scale and position identical in the inputs.
   Built-in randomness is not controlled; this is a screening experiment, not
   a statistically identified effect of joint conditioning.
3. **Judge raw geometry first.** Preserve the raw canvas and split fixed cells;
   never fit individual bounds or clip to the guide. Examine heel above toe,
   side profile, shaft/calf continuation, ankle placement and design consistency
   separately. Compare silhouette and contact sheets, inspect fixed-size
   playback. One result cannot establish repeatability. A miss triggers a
   change in mechanism, not an open-ended prompt/seed sweep.
4. **Reach the complete cycle.** If the pilot is useful, generate the other
   half with shared reference parts and test the opposite folded leg. If it
   fails exact geometry, reuse generated appearance through an authored boot
   view atlas or texture transfer with fixed silhouettes. Keep any constrained
   composite visibly separate from raw model performance. Author contact,
   recovery and sole-visible views; the all-profile diagnostic does not replace
   the previously requested sole-visible leading boot.
5. **Reuse milestone.** Once the full mouse is visually accepted at a declared
   logical resolution, freeze the part/anchor/view manifest, render a second
   motion or equipment variation without new per-frame structural prompting,
   and only then consider a production C++ adapter. Add coat response after
   the legs work. Avoid changing production serialized assets now.

The current diagnostic deliberately retains the old global poses and ground
calibration so the attachment comparison is interpretable. New heel/contact
annotations are explicit but not used to move the body in this ablation.
Contact grounding and projected boot proportions remain visible limitations.

## Research and decision

Primary sources checked 2026-09-10:

- [RAVE, CVPR 2024](https://rave-video.github.io/) edits grids of frames with
  shared processing and noise shuffling. Its
  [ablation](https://rave-video.github.io/supp/supp.html) shows that independently
  processing multiple grids still has consistency problems. This supports
  trying shared appearance context; the proposed sheet pilot is **not** an
  implementation of RAVE and does not inherit its results.
- [Sprite Sheet Diffusion, revised March 2025](https://arxiv.org/html/2412.03685v2)
  adapts Animate Anyone with reference, pose and temporal components. The
  revised study reports improved motion matching but remaining fine-detail,
  subject-consistency and second-stage overfitting problems. The older plan's
  619-pair summary describes the earlier version, not this revised evaluation.
  Training is a later option after retaining reviewed targets and held-out
  character/action sequences.
- [Sprite Sheet Generation using a Diffusion Model, CVMP 2025](https://marcovolino.github.io/docs/papers/2025-wong-cvmp.pdf)
  is a small poster study using a motion module and reference network, with
  845 human-motion frames and synthetic pixelation. It is useful architectural
  evidence, not validation for our nonhuman character or exact foot semantics.
- [ToonCrafter](https://github.com/Doubiiu/ToonCrafter) supports endpoint cartoon
  interpolation and sparse sketch guidance; its official README describes up
  to 16 frames and warns of imperfect success. It is a sensible later temporal
  candidate once valid colored endpoints exist. Interpolation alone does not
  guarantee our twelve authored poses or a closed loop.
- [ToonComposer](https://github.com/TencentARC/ToonComposer) combines sketches
  and color reference for post-keyframing. The documented 480p/61-frame workload
  needs about 57GB VRAM, above the previously recorded 24GB 3090. This is not
  evidence that all implementations need 57GB, but it makes installing that
  default stack a poor first move for this bounded test.
- [ControlNet](https://github.com/lllyasviel/ControlNet) trains separate control
  representations. Skeleton labels, material IDs and ordinal layer order are
  diagnostic maps, not interchangeable with image edges or geometric depth.

Engineering judgment: repair the contradictory spatial contract first; use
shared sheets as a low-cost appearance test; use a temporal model when there
are valid keyframes to propagate. Neither more samples nor a newer model can
make a wrong camera or detached guide ankle correct by construction.

## Boundaries and review

All experiment code lives in `scripts/`, data/evidence in
`experiments/sprite_sequence/`, tests in `tests/`. There is no CMake target,
`src/` dependency, provider call in the preparer, production import, or format
migration. The C++ executable deliberately uses only the standard library to
keep this experiment independent of engine dependencies; errors are reported
at its process boundary before any output is emitted.

Guidance needed from the user is visual: whether the new recovery-foot profile
matches the intended drawing, and ultimately which logical sprite resolution
to ship. For now 48px and 96px are review sizes, not a settled production size.
Autonomous implementation and the bounded pilot do not imply art acceptance.

## Current result and next milestone

**Current verdict: visual milestone unmet.** The user reviewed the latest
proportion attempt and said, **“we are still pretty far off the mark here.”**
It is not accepted as a satisfactory painted run. [Assessment](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-v3/assessment.json).
Passing technical checks and measured width changes do not establish visual
success. Earlier wording that the correction was complete overstated the result.

The retained [512px painted-run proportion attempt](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-v3/review.html)
now covers all twelve original phases with complete generated paintings,
C++ anatomy/contact and explicit semantic registration. Four new raw requests,
their exact inputs/prompts, three mapping iterations and the final decoded-frame
checks are preserved. [Method and remaining visual questions](../../experiments/sprite_sequence/evidence/painted-master-v1/README.md).
The user's 2026-09-12 narrow-leg/oversized-arm review led to complete-sleeve
reuse and C++ transverse leg expansion. [Correction and before/after evidence](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-README.md).
The remaining milestone is a satisfactory master-size painted cycle, not
merely obtaining a first review. Reassess silhouette, proportions and motion
against the source and preferred complete leg artwork before choosing the
next mechanism. The latest verdict does not specify all causes; previously
noted folds, sole visibility, arm deformation, head/coat interpretation and
paw/muzzle overlap remain assistant observations. Shipping resolution and final
art remain unresolved. More poses or another width adjustment are not yet
justified as solutions to this gap.

The prior experiments below explain the retained comparisons and boundaries.

The user reviewed the enlarged frame-10 control and said, **“Yes, use this
profile as the starting point.”** The two initial requests were followed by
three six-leg sheets using the first generated sheet as an appearance
reference. All 24 leg poses now have raw artwork and fixed-coordinate playback.
The full comparison, measurements and reproduction commands are in the
[experiment README](../../experiments/sprite_sequence/README.md).

Expansion **did not validate reliable sheet registration**. Initial near-leg
recovery centroid drift was at most 2.94px; the full set reaches 19.78px and
foreground areas are 1.17–1.60 times their controls. The images look consistent
in materials but cannot be trusted as pose-authoritative frame assets. No
further strength/seed sweep was run.

A second, explicitly constrained route now reuses two material samples from
one generated leg on all C++ control polygons. It makes no per-pose generation
request and preserves every control silhouette. This establishes a reusable
technical route, **not final painted artwork**: the boots still look angular,
lighting/folds are simplistic, support calibration is inherited, and the
current coat occludes recovery. The numerical comparison never scores this
constrained result as a generative success.

The reusable rounded atlas is now implemented in
[boot-atlas-v2](../../experiments/sprite_sequence/evidence/boot-atlas-v2/review.html).
It preserves both recovery triangles and all 24 ankle/toe pins, adds explicit
flat/heel/toe support and a sole-visible view, and renders two twelve-frame
motions through one four-tile atlas. The second motion flexes free shins by
12 degrees while retaining segment lengths and support geometry. This is a
bounded reuse proof, not validation of a new animation or production adapter.

C++ owns rounded surface outlines, texture bases, landmarks, view validation,
whole-character grounding and lower-coat deformation. Python bakes the shared
paint recipe, rasterizes and composes retained body layers. Flat phases replace
the uncertain heel height with the toe height; pose 1's leading heel is lowered
3px to clear the trailing toe. Flight blends neighboring support translations
while retaining at least 6px clearance. The coat uses a closed three-frame lag
and preserves torso pixels exactly. Its fixed-coat comparison remains visible.

The user's subsequent review finds the 96px motion relatively smooth but the
atlas artwork inferior to the generated isolated legs. `reference_07/far` has
an overlong rear heel and an inverted-T silhouette: the ankle projects 49.57%
along its sole. Preserve the control/reuse findings, but atlas v2 is not the
painted-art solution. [Review evidence and next-workstream brief](../../experiments/sprite_sequence/evidence/user-review-full-resolution/README.md).

The full-resolution workstream above follows this brief: correct the
explicit boot anatomy and use complete generated part artwork, with semantic
anchors and limited, visible registration/deformation corrections. Do not
reduce generated legs to small material patches or treat a downsampled preview
as acceptance. A 512px full-character master is proposed for the experiment;
shipping resolution is undecided. The existing 256px body source also needed
attention if the larger master is to contain coherent actual detail.

First keep the original twelve-pose timing and verify anatomy, attachment,
volume and painted-detail stability at the master size. If cadence still needs
more samples, evaluate a separate 24-frame/24fps cycle with authored intermediate
poses and stable support phases; do not silently replace the original reference.
New generation inputs must be shown before submission. No runtime integration
or production asset migration has been started.

The five built-in generation tool calls took approximately 100.5 seconds in
total (18.2–21.1 seconds each). The 3090 did not run these requests. The larger
elapsed task time was code audit, implementation and review, not inference.
