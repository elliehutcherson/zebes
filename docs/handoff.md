# Active handoff

Updated 2026-09-10. **Get the twelve-frame green-coated mouse run working.**
This is the project's current priority. Old track sequencing and art/experiment
gates are suspended; they are not prerequisites for this work.

The accumulated work was pushed to `main` on 2026-09-09 at `7695426`.
The longer-term direction now explicitly includes a defined sprite resolution
and removable clothing/equipment bound to a complete underlying character.
See the future-direction section of the experiment plan; it does not displace
the current run-animation work.

The user approved [the experiment plan](sprite-run-experiment-plan.md), with one
explicit requirement: show the actual source artwork, skeleton alignment,
twelve posed frames and any conditioning maps before sending generation
requests. On 2026-09-08 the user accepted the current tracing and binding as
a starting point for experiments, with foot directions recorded as a known
issue. The approved pilot uses those poses unchanged.

**The first generation experiment is complete:** four pilot requests plus one
fresh twelve-frame batch. The
[comparison](../experiments/character_binding/evidence/pose-cleanup-cycle-v2/review.html)
shows the original puppet, whole redraw and protected-pixel composite in
synchronized playback. The white-matte batch follows the broad poses and
closes more gaps, but frames 5, 10 and 11 still need leg/boot work. Exact
identifying pixels vary in the whole redraw; masking preserves them but
retains more seams. [Findings and provenance](history/posed-mouse-cleanup-2026-09-08.md).

## Current work

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
[`mouse_run_reference_v2.json`](../experiments/character_binding/puppet_documents/mouse_run_reference_v2.json).
It uses the unchanged
[`interactive-run-source-v1.png`](../experiments/character_binding/inputs/interactive-run-source-v1.png),
23 source joints, corrected near/far arm assignments, complete arm meshes,
separate rigid boots, two legs, head, body and an independently bound tail.
There are twelve frames, previewing at 12 fps. The source drawing is the bind
pose, not an instruction to force one of the supplied human poses to match it.
The older `test-puppet.json` and `mouse_interactive_run_v1.json` remain comparison
inputs.

The user rejected v1's inherited motion and supplied the actual twelve-pose
sheet. That unchanged image is now
[`run-pose-reference-12.png`](../experiments/character_binding/inputs/run-pose-reference-12.png).
[`run-pose-trace-v1.json`](../experiments/character_binding/inputs/run-pose-trace-v1.json)
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

The input guide contributed to the problem: its boot shaft rotated with the
sole, then the calf connected to the cuff independently. Pose 10's near calf
and shaft differed by 29.5°, creating an extra bend at the boot opening.
V3 keeps the knee bend but aligns the calf and shaft along one knee-to-proxy-ankle
line; the foot can turn at the ankle. Sole pins, camera and stoutness are held.
All 24 guide legs are now collinear through the cuff. The original puppet has
not been rebound or changed. [Guide comparison and measurements](../experiments/character_binding/evidence/shin-alignment-review-v1/README.md).
These corrected inputs have not been sent for generation. Review the new
independent-leg guide before the next redraw; preserve the successful finish
without treating the rejected part as an anatomical or training target.

The user identified a knee-to-boot assignment error in stout redraw pose 10:
the forward knee of the folded near leg is interpreted as belonging to the
far leg that reaches the planted boot. The visual finish is the best result
so far in the user's assessment, but the pose is not anatomically correct.
[Leg ownership diagnostic](../experiments/character_binding/evidence/leg-ownership-review-v1/reference_10/ownership-review.png)
shows both complete chains and their overlap. Draw order is correct; the
flattened brown guide merges both legs, and its surface-ID view labels both
trouser legs yellow. The previous high-boot/low-boot check missed this error.

That comparison has now run: three pose-10 image requests plus the planned
one-image pose-4 follow-up. [Results](../experiments/character_binding/evidence/leg-ownership-trial-v1/review.html).
The labeled-guide package makes pose 10's folded calf crossing more readable.
Independent legs establish ownership structurally, but the folded boot ends
about 11 working pixels lower than its guide after the fixed crop is inverted;
the standing leg is much closer. No later fitting hides this difference.
The combined route was the more promising immediate visual candidate in that
comparison; the user's subsequent choice is the independent route above.
It needs part-to-rig calibration and boot-view checks. Pose 4 has
only one sample and partly hidden connections. Neither route is a validated
twelve-frame result. [Prompts, registration and findings](../experiments/character_binding/evidence/leg-ownership-trial-v1/README.md).
Keep the current style/stoutness and distinguish correct ownership from exact
pose following. New actual generation setups still need visible input review.

The user approved the boot view guides for generation and requested stouter
legs and boots. That amendment is implemented in
[`boot-view-guide-v2.json`](../experiments/character_binding/inputs/boot-view-guide-v2.json).
Four built-in image-generation redraws (poses 1/7/10/12) are complete:
[review and retained evidence](../experiments/character_binding/evidence/stout-boot-redraw-v1/review.html).
They produce rounded leather boots and shaded stout trousers, preserve the
broad contact/tucked/airborne states, and expose local registration changes.
The airborne result's lowest boot is about 11 working pixels higher than its
guide. All raw outputs are 1254×1254 and uniformly mapped to the fixed canvas.
The original mask clips some newly drawn boot shapes; the review includes a
separately labeled broader compositing comparison that recovers the tucked boot.
[Method, prompts and limitations](../experiments/character_binding/evidence/stout-boot-redraw-v1/README.md).

The brown geometry, original character and editing-region guide were the stout
pilot's model inputs. Boot-face surface labels were only for human inspection;
the newer A/B leg-identity diagrams were supplied in the ownership experiment.
The user's
approval covers the view directions with the requested stout adjustment;
do not ask them to approve that same change again. This is still four-pose
evidence, not a twelve-frame result. Correct leg ownership before full-cycle
coverage and registration; keep strict/raw/broader evidence distinct.

The [connection experiment](../experiments/character_binding/evidence/run-gap-control-v1/review.html)
is complete: twelve local ComfyUI requests, comparing blank/blur/shaped inputs
for poses 5 and 10 at denoise 0.35/0.60. The prefill supplies most of the useful
connection. Generated repairs change only 3–11 pixels beyond their input at
48px and retain rough seams. The protected-boot control cannot fix perspective,
so no complete-cycle batch was run. All outputs are 1024×1024, with original
pixels restored exactly outside the mask. Requests totalled about 148 seconds
including loading and polling. [Results and provenance](../experiments/character_binding/evidence/run-gap-control-v1/README.md).

The same review's **New boot guides** view now shows twelve unsubmitted
lower-body inputs. A small 3D boot proxy uses a fixed camera and changing sole
directions, with a connected 2D calf placeholder. Pose 1 exposes the near sole
and hides the far sole; support poses have flatter soles. Full-leg/boot masks
and knee/cuff/sole anchors can be overlaid. These are boxy geometry guides,
not final art or a validated replacement rig.
[Configuration and reproduction](../experiments/character_binding/evidence/boot-view-guides-v1/README.md).
That earlier view review is now complete with the stout amendment described
above. The original mouse puppet document is unchanged.

The [earlier input review](../experiments/character_binding/evidence/run-gap-inputs-v1/review.html)
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

Future garment motion, requested by the user and explicitly deferred for this
leg trial: the coat/cloak hem should lift when contacted by a raised knee or
extended thigh, then settle toward a resting/running-neutral shape with lag.
Running motion can keep cloth flared even without direct knee contact. Keep
upper-coat attachment, front/back hems and any removable cape separately
controllable. A cape attaches at the shoulders/back and needs its own trailing
and settling response, not direct copying of knee motion. Author or bake the
cloth into a closed twelve-frame cycle once leg ownership is stable. Current
coat artwork was retained as the occluder in this experiment; cloth motion has
not been implemented.

## Tools and retained evidence

- `build/dev/bin/serve_puppet_editor` opens the existing editor.
- `puppet_edit` applies named document commands; `render_layered_puppet`
  supplies actual C++ frames and part masks.
- [Puppet README](../experiments/character_binding/README.md) documents commands.
- Generated command lists live with their disposable renders in `out/`.
  `retarget_run_reference.py` regenerates the current transfer and ground
  commands from the retained trace and source binding.
- `experiments/character_binding/out/043-run-binding-before/` is the prior
  render; `out/044-run-calibration-review/` contains the superseded calibration
  review. `out/045-run-reference-v2/review.html` compares the actual supplied
  drawing, its trace and the current C++ mouse render, starting on pose 10.
- [Historical findings](../experiments/character_binding/FINDINGS.md) retain
  previous failures and corrected interpretations.
- [Current input-review measurements](../experiments/character_binding/evidence/run-reference-v2-review.json)
  retain the supplied image/trace/document hashes, transferred directions and
  sole rows. They are diagnostics, not art approval.
- [Previous handoff](history/handoff-before-run-calibration-2026-09-08.md) retains
  editor debt, runtime invariants and earlier verification details.
- [Roadmap](roadmap.md) keeps unrelated work parked.

## Verification of this change

The four affected puppet-document test executables, the renderer's 26 focused
tests, the two embedded-page tests and three reference-transfer tests pass.
Scoped C++ lint and the proof tool's direct clang-tidy check pass; the repository
lint wrapper does not accept `scripts/` translation units. `git diff --check`
passes. Browser review exercised playback, frame selection and the layer-order
view. The later cleanup experiment added sixteen image requests, three focused
Python checks and exact decoded-frame verification of all three APNG previews.
No production import was performed.
