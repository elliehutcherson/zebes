> Archived 2026-09-12 after the user accepted the storybook 3D mouse and
> selected its workflow as the plan of record. Statements below about current
> work, next steps or pending acceptance describe the earlier state.
> See the [current plan of record](../mouse-3d-plan.md).

# Active handoff

Updated 2026-09-12. **Current checkpoint: review the new neutral storybook 3D
mouse.** The user explicitly requested implementation of the approved plan.
The first neutral head/body and two body-deformation checks are now available.
Earlier independent-2D-layer sequencing and art gates do not block this work.

**New neutral study:** [review](../../experiments/mouse_3d/storybook-neutral-v1/result/review.html),
[Blender model](../../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend),
[textured GLB](../../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.glb),
[comparison](../../experiments/mouse_3d/storybook-neutral-v1/result/comparison.png),
and [implementation/provenance](../../experiments/storybook_mouse/README.md).
This is a new Hunyuan3D-2 shape, cleaned to one watertight main surface and
reduced to 80,000 triangles for the study. A temporary 20-bone rig supplies
neutral, raised-arm and bent-leg checks. All vertices have normalized weights;
head and paws are rigid for these body checks. The study has a packed 2048px
authored color texture and 96/128/512px pose sheets. It is not a full run cycle.

**Limits and next work:** user art review is pending. Hands/digits, rough hair
tufts and facial topology need deliberate refinement; there are no facial or
finger controls or separate garments yet. The neutral form must be judged
before further outfit/locomotion work. The clay views show the actual geometry;
the study colors are placeholders, not final painted materials.

All ten focused input/artifact tests pass: inspected-input hashes, raw mesh
preservation, bounded cleanup, nonempty packed texture, mesh/UV/skin validity,
constant bone lengths, connected chains, fixed head/support paw, ground and
transparent sprite margins. Raw input, failed projection, empty-bake failure,
working source snapshots and inference receipts remain retained. The single
completed shape trial took about 80 seconds after setup. See the implementation
README for isolated dependencies and reproduction. No production imports or
runtime changes ran.

**Latest direction: storybook adventurer, reference image 2.** The user supplied
three higher-fidelity character references, asked how to reach that look, and
explicitly selected image 2. The new [design/modeling direction](../mouse-storybook-direction.md)
records the selected silhouette, face and anatomy, reusable pipeline, and a
bounded route toward a new neutral character mesh. The
[first concept sheet](../../experiments/mouse_3d/storybook-direction-v1/concept-sheet.png)
is a 2D visual proposal from one built-in imagegen request using only reference
2. It is not a new 3D asset or a geometrically validated turnaround.
All three references, the exact visible prompt, raw output and hashes are
retained. That planning discussion preceded the implementation checkpoint
above. Front, profile and three-quarter geometry views and two body-pose checks
are now available at sprite size.
This new design direction supersedes continuing automatic v3 proportion edits.

**The user has now explicitly approved the storybook plan.** They supplied
the [original Reddit post](https://www.reddit.com/r/blender/comments/1s6s4dg/first_model_and_look_for_some_tips/)
and asked what further input is needed. None is currently required to begin the
neutral-model stage. No download link was found in the post/visible comments or
focused searches; creator-profile retrieval was unavailable. The source author
confirms generated reference drawings with mismatched views. Use them for
appearance and resolve the geometry in 3D. Derry is reachable with Blender
4.0.2 and the 24GB 3090. No creator contact ran. The subsequent isolated
Hunyuan installation and neutral-study implementation are documented above.
See the plan's source-model lookup for the bounded search findings.

**Earlier feedback:** hood-down v2 is “much much better” and a “huge huge
improvement.” Keep that direction. The user requested a little more width,
a shorter nose, cuter eyes and character, and folds in the smooth trousers.

**Latest retained 3D asset:** [synchronized v2/v3 comparison and playback](../../experiments/mouse_3d/v3/review.html),
[before/after](../../experiments/mouse_3d/v3/before-after.png),
[face detail](../../experiments/mouse_3d/v3/face-detail.png),
[trouser detail](../../experiments/mouse_3d/v3/pants-detail.png),
[Blender asset](../../experiments/mouse_3d/v3/mouse.blend),
[animated GLB](../../experiments/mouse_3d/v3/mouse.glb), and
[method, reproduction and limitations](../../experiments/mouse_3d/README.md).
Relative to v2, the coat/chest are another 9–11% broader, with wider shoulders
and stance. Smooth deformation shortens the muzzle without scaling the skull;
the cheeks are fuller and the eyes are rounder with warm irises, large pupils,
softer lids and paired highlights. Continuous trousers now contain localized
geometric folds around the knee and above the boot. These authored folds deform
with the existing rig; they are not a cloth simulation. The hood stays down,
and the single head skin and cupped ears are retained. Both earlier versions
remain intact.
One authored model and armature produce a 24-frame run at 24fps, a
16-view turntable and transparent 512/128/96px sheets. Near/far limb chains
have fixed lengths and explicit foot contacts. No per-frame image generation,
production imports or runtime changes ran. This is a new gait, not a transfer
of the old twelve drawings.

All 22 focused motion/artifact tests pass across the three versions, including decoded sheet/APNG
equality, GLB skin/animation, retained face vertex colors and source hashes.
Blender checks actual rest matrices, posed joints, support-sole orientation,
ground clearance and single closed head/trouser skins. The initial bind-matrix
failure and a rejected contact-shadow render remain retained.

**V2 is the user-preferred basis; v3 awaits review.** The latest refinement
addresses width, muzzle length, eyes and fabric detail. It does not establish
final proportions, painted finish or convincing run timing.
The historical 2D verdicts and their evidence below remain valid for those
earlier outputs.

**Latest user verdict: still far off the mark.** After reviewing the proportion
attempt, the user said, **“we are still pretty far off the mark here.”**
`proportions-v3` is not accepted as a satisfactory painted run. The intended
visual milestone remains unmet; do not describe the gap as minor polish or
merely pending a first review. Earlier assistant claims that the correction
was complete overstated the result.
[Assessment and reviewed-artifact hashes](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-v3/assessment.json).

**Latest unsuccessful result:** [proportion attempt, before/after](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-v3/review.html).
The user found `mapped-v3`'s legs too narrow and its back arm far too large.
These are proportion defects, not minor polish. The attempted correction widens
thigh and calf midpoint cross sections by about 30% and 40% on average, and
reduces the back arm's painted area by 41–46%. It reuses a complete compatible
sleeve on the original far-arm joints, while C++ applies smooth transverse
leg expansion with fixed semantic pins. Head/body/near-arm/tail part pixels
and contact geometry are unchanged. No new generation requests ran.
[Correction method, rejected attempts and reproduction](../../experiments/sprite_sequence/evidence/painted-master-v1/proportions-README.md).
All 25 focused tests pass; sampled-art mesh inversions and ground penetration
are zero. Raw evidence and the rejected spline-widening attempts remain intact.
Those checks establish implementation behavior, not appropriate proportions,
natural anatomy, preserved appearance under deformation or convincing motion.
The latest user judgment above supersedes prior acceptance/status wording.

**Previous full-resolution baseline:** [painted 512px run](../../experiments/sprite_sequence/evidence/painted-master-v1/mapped-v3/review.html).
The user's autonomous full-resolution request now has a complete **512×512,
twelve-frame, 12fps** result with full-character and legs-only playback, raw
placement and prior-atlas comparisons, pins and secondary 96px viewing.
[Method, exact requests, reproduction and limitations](../../experiments/sprite_sequence/evidence/painted-master-v1/README.md).
Final animation acceptance and shipping resolution remain open.

Four built-in imagegen requests produced twelve complete painted leg drawings
and five independent upper-body parts with actual new detail. Raw 1448×1086
leg sheets and the 1536×1024 upper sheet remain byte-for-byte intact, as do all
five earlier raw outputs. Exact inputs were displayed before submission.
No additional requests, production imports or runtime changes followed.

C++'s new `--painted` mode preserves traced hip/knee/ankle/toe pins and places
the ankle at 22% of the sole, correcting frame 7 far's 49.57% placement and
the two negative-projection airborne heel estimates. These are authored heel
corrections, not new trace measurements. C++ semantic registration maps the
complete native paintings using explicit observed landmarks; measured outsole
points constrain support soles without silhouette clipping. One whole-body
translation grounds each frame. Original profile and atlas modes still
reproduce their retained geometry.

**Verification:** 22 focused tests; no inverted mapping cells over sampled
opaque artwork; all five 512px APNGs decode to their twelve retained PNGs;
no leg pixels below ground row 428 at alpha thresholds 32/128/224. The largest
leg pin correction is 19.31 master pixels, and arm distortion remains locally
substantial. The first mapping with stretched paws and ground penetration,
and the second mapping whose final manifest failed on a relative path, remain
retained separately. `mapped-v3` is the complete baseline with rejected proportions.

**Next work must start from the unmet visual target:** compare the full-size
assembled run with the source character and preferred complete leg paintings,
then reassess overall silhouette, proportions and motion before selecting a
new mechanism. Another width multiplier or more frames is not an established
solution. The latest feedback does not enumerate all remaining defects.
Changing folds, subtle sole visibility, arm deformation, coat response and
paw/muzzle spacing are assistant observations, not a complete user diagnosis.
Keep C++ geometry authoritative and preserve raw evidence. The preference for
complete painted parts remains; atlas v2 is not an accepted appearance fallback.
This turn documents the result only; no further generation or rendering ran.

**Review that established the full-resolution direction.**
The user likes the generated isolated legs' appearance and considers the 96px
atlas animation relatively smooth, but finds the procedural atlas legs inferior.
`reference_07/far` is specifically rejected: its heel projects too far behind
the ankle, producing an inverted T instead of an L. The ankle projects 49.57%
of the way from heel to toe along that sole. This is a control-geometry defect;
passing attachment tests did not validate anatomy. The user's review images
and conclusions are retained in
[full-resolution review](../../experiments/sprite_sequence/evidence/user-review-full-resolution/README.md).

**Standing decision:** retain the deterministic ownership, attachment, contact
and reuse mechanisms, but do not accept atlas v2 as the painted-art solution.
Use complete generated legs/boots as the appearance target and preserve their
native detail; two tiny material samples cannot carry their shape, folds and
volume. Rework the explicit heel/ankle/toe contract before further generation.
The accepted recovery profile remains a starting point, not blanket approval
of the transferred heels or atlas silhouettes. The new painted candidate above
follows these findings; no further approval of atlas v2 is needed.

**Resumed by the user on 2026-09-10 for a fresh audit and autonomous experiment.**
The active direction and research are in the
[sprite sequence milestone](../sprite-sequence-milestone.md). The new
[experiment and review](../../experiments/sprite_sequence/README.md) retain a C++
profile-control prototype, five built-in imagegen requests, a complete raw
twelve-frame draft and a separate deterministic material-reuse proof.

The audit found that the old boot guide's derived ankle disagrees with the
posed ankle by 3.21–18.09 working pixels. The original leg directions transfer
correctly; there is no evidence for another global left/right swap. The new
profile keeps all 24 ankles/toes fixed and aligns shafts with shins. **The user
accepted the frame-10 profile as a starting point.** This is not approval of
all heel estimates, generated artwork or the final animation.

Shared-sheet generation improved the initial recovery comparison but failed
reliable registration on expansion: full-set centroid drift reaches 19.78px
and area grows 17–60%. Preserve that failure. **The reusable atlas branch is
now implemented:** [current twelve-frame review](../../experiments/sprite_sequence/evidence/boot-atlas-v2/review.html).
Four shared tiles shade rounded C++ trouser, shaft, profile and sole-visible
surfaces. Both recovery heel triangles and all 24 original ankle/toe pins are
preserved before one whole-character grounding translation. Ten explicit
support contacts meet row 214; flight interpolates neighboring translations
with at least 6px clearance. Flat support replaces uncertain heel heights;
frame 1 has an explicit 3px leading-heel correction. These new estimates and
the sole view still need visual judgment.

The review compares restrained C++ coat response against the fixed coat and
a second run that flexes free shins another 12 degrees using the **same atlas**.
It includes **Legs only**, anchors, 48/96/256px playback and fixed-crop recovery
comparisons. Atlas v1 is retained as the intermediate with an excessive jump
into flight; v2 smooths only whole-character translation. Fourteen focused
tests pass, including old-profile reproduction, geometry, contact, reuse and
cyclic coat/torso invariants. Raw model output hashes remain unchanged. No new
generation requests, production imports or runtime changes ran.

**Full-resolution brief, now implemented above:** correct contact-boot anatomy,
preserve complete painted artwork and build a twelve-frame comparison. A 512×512 full-character
master is a proposed experiment target, not a user-selected shipping resolution.
The current source character is only 256×256, so enlarging it alone will not
restore detail. Keep the 96px view secondary. First evaluate the same twelve
poses for stable anatomy, registration and painted finish at the master size;
only then add intermediate poses if temporal cadence still needs work. Preserve
raw images separately from any mapped/corrected assets. Show exact new inputs
before any generation requests, as already required. Head/upper-body source
artifacts persist and final animation is not accepted.

The [earlier corrections](../history/sprite-boot-control-2026-09-10.md) still apply:
the six old candidates and depth-0.45 recommendation remain rejected. No new
ComfyUI requests, machine tuning or production imports ran in the fresh audit.

The accumulated work was pushed to `main` on 2026-09-09 at `7695426`.
The longer-term direction now explicitly includes a defined sprite resolution
and removable clothing/equipment bound to a complete underlying character.
See the future-direction section of the experiment plan; it does not displace
the current run-animation work.

The user approved [the experiment plan](../sprite-run-experiment-plan.md), with one
explicit requirement: show the actual source artwork, skeleton alignment,
twelve posed frames and any conditioning maps before sending generation
requests. On 2026-09-08 the user accepted the current tracing and binding as
a starting point for experiments, with foot directions recorded as a known
issue. The approved pilot uses those poses unchanged.

**The first generation experiment is complete:** four pilot requests plus one
fresh twelve-frame batch. The
[comparison](../../experiments/character_binding/evidence/pose-cleanup-cycle-v2/review.html)
shows the original puppet, whole redraw and protected-pixel composite in
synchronized playback. The white-matte batch follows the broad poses and
closes more gaps, but frames 5, 10 and 11 still need leg/boot work. Exact
identifying pixels vary in the whole redraw; masking preserves them but
retains more seams. [Findings and provenance](../history/posed-mouse-cleanup-2026-09-08.md).

## Existing source calibration

The existing puppet's source skeleton and motion tracks disagreed. Arm sides
were mapped inconsistently with the leg phase. The frame nearest the source's
arms had the wrong leg phase. Centering the clip could not fix this:
`rebase_frames` only translates joint centers, while `anchor_frame` only chooses
the opening frame.

A new `retarget_frames` command makes the selected frame exactly the source
skeleton and transfers bone-angle changes through the remaining frames.
Rigid bones retain source lengths. Explicitly stretchable bones retain relative
length changes. Invalid/collapsed or ambiguous skeletons fail without partial
changes. The editor distinguishes this operation from merely centering a clip
and no longer calls a 1.00 bone-length ratio a matching pose.

The current candidate is
[`mouse_run_reference_v2.json`](../../experiments/character_binding/puppet_documents/mouse_run_reference_v2.json).
It uses the unchanged
[`interactive-run-source-v1.png`](../../experiments/character_binding/inputs/interactive-run-source-v1.png),
23 source joints, corrected near/far arm assignments, complete arm meshes,
separate rigid boots, two legs, head, body and an independently bound tail.
There are twelve frames, previewing at 12 fps. The source drawing is the bind
pose, not an instruction to force one of the supplied human poses to match it.
The older `test-puppet.json` and `mouse_interactive_run_v1.json` remain comparison
inputs.

The user rejected v1's inherited motion and supplied the actual twelve-pose
sheet. That unchanged image is now
[`run-pose-reference-12.png`](../../experiments/character_binding/inputs/run-pose-reference-12.png).
[`run-pose-trace-v1.json`](../../experiments/character_binding/inputs/run-pose-trace-v1.json)
records manual joint-center traces in the twelve original cells, in order.
The old `rig-bench.json` and v1 motion are no longer pose authorities. In
particular, the old far thigh jumped 93 degrees from frame 7 to 8, and pose 10
did not retain the supplied folded leg.

`scripts/retarget_run_reference.py` now takes limb directions directly from
that trace, keeps the torso/head artwork rigid, and transfers them to the
mouse. Both legs use one fixed reference-to-mouse scale, preserving the traced
knee/ankle positions relative to the hips and the drawing's foreshortening. The
four thigh/shin bones explicitly scale their bind artwork to these projections.
The mean of the two bind leg lengths sets the shared scale once; it is never
recomputed per frame. Equal fixed segment lengths were tried and discarded
because they changed the foot heights. A second pass moves
the whole body to the annotated support boot at a fixed ground row; poses 6
and 12 are authored flight phases. It never seats every frame on its bounds.

Source-space labels in this candidate: `_l` is the near arm (reaching toward
screen-left in the source) and near leg (reaching toward screen-right);
`_r` is the far pair. Review part labels and colors rather than guessing from
where a limb happens to point.

The candidate's poses are accepted for experimentation, not as final animation.
Flat dark fills under the coat are rough completion, not approved final artwork.
Joint placement hidden by clothing is an authored estimate. Foot directions
need later attention; alternative pose sets remain available if useful.
Do not reinterpret a clean render or passing code tests as accepted animation.

## Next

**Current direction: independently rendered layers**, selected by the user on
2026-09-10. They rejected the isolated folded leg's shin angle while liking the
fabric folds and boot finish. That generated part is an appearance reference,
not accepted geometry. Do not revert to whole-character rendering on the basis
of the earlier one-frame comparison.

**Latest correction from the user:** the original guide already shows an
almost-horizontal calf and downward-pointing toe. The generated leg instead
curves downward into a boot whose toe turns right. V3's small cuff-axis change
does not fix this output distortion. Its 29.5° measurement describes guide
axes; it does not establish the cause of the generated anatomy. Keep v3 as a
separate unsubmitted diagnostic, not the next test's pose authority.

**Latest experiment result:** contour control is not view control. The completed
[isolated-leg trial](../../experiments/character_binding/evidence/leg-pose-preservation-v1/README.md)
held the outer pose with local Canny but all candidates still painted a
viewer-facing boot. The completed
[boot surface-control trial](../../experiments/character_binding/evidence/boot-surface-control-v1/README.md)
then isolated the boot and used real z-buffer depth from the fixed-camera proxy.
Canny-only repeated the wrong view; stronger depth followed the proxy more
closely. User review then correctly rejected the proxy itself: frame 10 is a
side-profile recovery leg with its heel lifted and toe hanging down, not an
oblique boot whose overall axis points down-left. All candidates are rejected.
Retained `review.png`, `summary.json`, exact graphs, receipts and manual
`assessment.json` distinguish
silhouette scores from view/art judgment.

The reusable pipeline boundary is now explicit: C++/authored data should own
pose, anchors, view selection, depth, draw order and deterministic composition;
generation may own isolated-part appearance. Do not prepare a second-seed
repeat. First audit the twelve source phases and complete each recovery-foot
chain with explicit heel and sole-contact semantics. Replace the current 35°
oblique proxy with a reviewed side-profile raised-heel control, show the exact
new maps, and only then run a bounded generation comparison. If that fails,
use an authored boot-view atlas with generated work only as texture/reference.

The [twelve-frame phase audit](../../experiments/character_binding/evidence/run-phase-audit-v1/README.md)
is retained: unchanged source drawings sit beside orange near-leg and blue
far-leg traces. It displays the existing support annotations and highlights
frame 10's near-leg recovery; it does not independently validate the skeleton.
The boot guide already has authored heel anchors, but the source trace has no
heel landmark. The missing trace data and proxy assumptions need review;
neither has been isolated as the cause. No provider requests ran for the audit.

The user identified a knee-to-boot assignment error in stout redraw pose 10:
the forward knee of the folded near leg is interpreted as belonging to the
far leg that reaches the planted boot. The visual finish is the best result
so far in the user's assessment, but the pose is not anatomically correct.
[Leg ownership diagnostic](../../experiments/character_binding/evidence/leg-ownership-review-v1/reference_10/ownership-review.png)
shows both complete chains and their overlap. Draw order is correct; the
flattened brown guide merges both legs, and its surface-ID view labels both
trouser legs yellow. The previous high-boot/low-boot check missed this error.

That comparison has now run: three pose-10 image requests plus the planned
one-image pose-4 follow-up. [Results](../../experiments/character_binding/evidence/leg-ownership-trial-v1/review.html).
The labeled-guide package makes pose 10's folded calf crossing more readable.
Independent legs establish ownership structurally, but the folded boot ends
about 11 working pixels lower than its guide after the fixed crop is inverted;
the standing leg is much closer. No later fitting hides this difference.
The combined route was the more promising immediate visual candidate in that
comparison; the user's subsequent choice is the independent route above.
It needs part-to-rig calibration and boot-view checks. Pose 4 has
only one sample and partly hidden connections. Neither route is a validated
twelve-frame result. [Prompts, registration and findings](../../experiments/character_binding/evidence/leg-ownership-trial-v1/README.md).
Keep the current style/stoutness and distinguish correct ownership from exact
pose following. New actual generation setups still need visible input review.

The user approved the boot view guides for generation and requested stouter
legs and boots. That amendment is implemented in
[`boot-view-guide-v2.json`](../../experiments/character_binding/inputs/boot-view-guide-v2.json).
Four built-in image-generation redraws (poses 1/7/10/12) are complete:
[review and retained evidence](../../experiments/character_binding/evidence/stout-boot-redraw-v1/review.html).
They produce rounded leather boots and shaded stout trousers, preserve the
broad contact/tucked/airborne states, and expose local registration changes.
The airborne result's lowest boot is about 11 working pixels higher than its
guide. All raw outputs are 1254×1254 and uniformly mapped to the fixed canvas.
The original mask clips some newly drawn boot shapes; the review includes a
separately labeled broader compositing comparison that recovers the tucked boot.
[Method, prompts and limitations](../../experiments/character_binding/evidence/stout-boot-redraw-v1/README.md).

The brown geometry, original character and editing-region guide were the stout
pilot's model inputs. Boot-face surface labels were only for human inspection;
the newer A/B leg-identity diagrams were supplied in the ownership experiment.
The user's
approval covers the view directions with the requested stout adjustment;
do not ask them to approve that same change again. This is still four-pose
evidence, not a twelve-frame result. Correct leg ownership before full-cycle
coverage and registration; keep strict/raw/broader evidence distinct.

The [connection experiment](../../experiments/character_binding/evidence/run-gap-control-v1/review.html)
is complete: twelve local ComfyUI requests, comparing blank/blur/shaped inputs
for poses 5 and 10 at denoise 0.35/0.60. The prefill supplies most of the useful
connection. Generated repairs change only 3–11 pixels beyond their input at
48px and retain rough seams. The protected-boot control cannot fix perspective,
so no complete-cycle batch was run. All outputs are 1024×1024, with original
pixels restored exactly outside the mask. Requests totalled about 148 seconds
including loading and polling. [Results and provenance](../../experiments/character_binding/evidence/run-gap-control-v1/README.md).

The same review's **New boot guides** view now shows twelve unsubmitted
lower-body inputs. A small 3D boot proxy uses a fixed camera and changing sole
directions, with a connected 2D calf placeholder. Pose 1 exposes the near sole
and hides the far sole; support poses have flatter soles. Full-leg/boot masks
and knee/cuff/sole anchors can be overlaid. These are boxy geometry guides,
not final art or a validated replacement rig.
[Configuration and reproduction](../../experiments/character_binding/evidence/boot-view-guides-v1/README.md).
That earlier view review is now complete with the stout amendment described
above. The original mouse puppet document is unchanged.

The [earlier input review](../../experiments/character_binding/evidence/run-gap-inputs-v1/review.html)
retains the official Spineboy run benchmark and exact connection-test inputs.
The user considers trying the leg connection worthwhile, but identified a more
fundamental boot-view problem: the leading boot in pose 1 exposes its underside
while the trailing boot does not. The same two painted boot views cannot be
reused in every pose. Cuff estimates remain uncertain. Keep this masked trial
as a connection control; the next meaningful art candidate needs pose-dependent
boot/lower-leg drawings and an editable region covering the entire boot.
Review the new silhouettes/surface guides before that generation setup. See
the experiment plan's boot-view amendment and artist reference sheets.
The Spine sample is a motion benchmark, not yet a replacement mouse binding.
Head flicker remains deferred.

1. The user's 2026-09-09 priority is motion and missing leg/boot connections;
   head flicker is deferred. Compare an established biped rig and motion source
   with the supplied tracing before expanding custom authoring tools. The
   current pose set remains the accepted experimental control.
2. Use the completed connection comparison as evidence that input geometry
   matters. Move to whole-leg/boot redraws after review of the new guides;
   do not continue polishing the boot-protected control or head flicker.
   Prefer reusable completed views where possible.
   Show the exact input maps for any subsequent generator trial. Distinguish
   an ordinal layer-order preview from real depth; never pass a diagnostic
   skeleton to an edge/depth model as though it were the expected input.
3. Follow the approved experiments where visible defects justify them.
   Retrying an old technique with corrected binding/maps is allowed after
   input review; old stop decisions are historical evidence.
4. Keep production imports and runtime changes separate until an animation
   works. Existing asset-format and lifetime invariants still apply.

Post-training is a possible later branch, as the user reiterated on 2026-09-09.
Keep exact inputs, model settings and accepted target artwork now. These
procedural prefills and unreviewed generated frames are not training targets.
Define whether training should improve reusable-part completion, pose obedience
or temporal consistency before choosing a model and dataset. Reserve held-out
poses/sequences to evaluate improvement rather than memorization.

Garment direction, requested by the user: the coat/cloak hem should lift when contacted by a raised knee or
extended thigh, then settle toward a resting/running-neutral shape with lag.
Running motion can keep cloth flared even without direct knee contact. Keep
upper-coat attachment, front/back hems and any removable cape separately
controllable. A cape attaches at the shoulders/back and needs its own trailing
and settling response, not direct copying of knee motion. Author or bake the
cloth into a closed twelve-frame cycle once leg ownership is stable. The new
atlas experiment includes only a bounded lower-coat compression with cyclic
three-frame lag and fixed belt/torso pixels. It is a first response candidate,
not a full garment collision model or cape implementation.

## Tools and retained evidence

- `build/dev/bin/serve_puppet_editor` opens the existing editor.
- `puppet_edit` applies named document commands; `render_layered_puppet`
  supplies actual C++ frames and part masks.
- [Puppet README](../../experiments/character_binding/README.md) documents commands.
- Generated command lists live with their disposable renders in `out/`.
  `retarget_run_reference.py` regenerates the current transfer and ground
  commands from the retained trace and source binding.
- `experiments/character_binding/out/043-run-binding-before/` is the prior
  render; `out/044-run-calibration-review/` contains the superseded calibration
  review. `out/045-run-reference-v2/review.html` compares the actual supplied
  drawing, its trace and the current C++ mouse render, starting on pose 10.
- [Historical findings](../../experiments/character_binding/FINDINGS.md) retain
  previous failures and corrected interpretations.
- [Current input-review measurements](../../experiments/character_binding/evidence/run-reference-v2-review.json)
  retain the supplied image/trace/document hashes, transferred directions and
  sole rows. They are diagnostics, not art approval.
- [Previous handoff](../history/handoff-before-run-calibration-2026-09-08.md) retains
  editor debt, runtime invariants and earlier verification details.
- [Roadmap](../roadmap.md) keeps unrelated work parked.

## Verification of this change

The four affected puppet-document test executables, the renderer's 26 focused
tests, the two embedded-page tests and three reference-transfer tests pass.
Scoped C++ lint and the proof tool's direct clang-tidy check pass; the repository
lint wrapper does not accept `scripts/` translation units. `git diff --check`
passes. Browser review exercised playback, frame selection and the layer-order
view. The later cleanup experiment added sixteen image requests, three focused
Python checks and exact decoded-frame verification of all three APNG previews.
No production import was performed.
