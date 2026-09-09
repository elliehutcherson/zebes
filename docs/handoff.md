# Active handoff

Updated 2026-09-09. **Get the twelve-frame green-coated mouse run working.**
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

1. Review the complete generated loop against the original puppet at 48px.
   Inspect the feet, connections in frames 5/10/11 and identifying-detail
   consistency. The pose set is accepted for experiments, not final art.
2. Complete missing leg artwork and foot attachment using the current poses.
   Show the exact input maps for any subsequent generator trial. Distinguish
   an ordinal layer-order preview from real depth; never pass a diagnostic
   skeleton to an edge/depth model as though it were the expected input.
3. Follow the approved experiments where visible defects justify them.
   Retrying an old technique with corrected binding/maps is allowed after
   input review; old stop decisions are historical evidence.
4. Keep production imports and runtime changes separate until an animation
   works. Existing asset-format and lifetime invariants still apply.

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
