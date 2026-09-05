# Active handoff

Updated 2026-09-04. [`roadmap.md`](roadmap.md) owns sequencing; this file is the
short resume point. Completed narratives live in [`history/`](history/README.md).

## Current state

Two tracks proceed independently:

- **Track 4 — environment/content.** The Catacombs production route has zone
  fades, complete finite parallax coverage through 0.5×, independent masonry,
  distributed player-scaled decor, initial floor/foreground variants, and a
  distinct middle ceiling frieze. The complete route review has no objective
  findings. Remaining silhouette variation is non-blocking content polish.
- **Track 5 — runtime/animation.** Runtime Milestones 1–3, pure frame-set
  processing, recipe/bundle lifecycle, and headless animation curation are
  complete. The stable six-state mouse asset graph remains valid, but human
  review rejected the authored Blender mouse's flat primitive style. The latest
  `semantic-arm-immutable-coat-v1` candidate keeps the accepted generated coat
  byte-for-byte unchanged instead of stretching it into the arm footprint.
  Coat RGB/alpha and digest match exactly, neutral remains exact, passing is a
  reachable bent arm, and its shadow is a separate tonal effect. The existing
  149-orphan and four-airborne-fold gates remain intentionally red; the second
  arm and legs remain deferred.

The reusable ordered-reference generation boundary remains supported for
OpenAI, Codex, headless generation, and redraw; it is not an animation roadmap
item. The deprecated evidence is indexed under
[`history/`](history/README.md).

## Pick up next

### Track 5: finish the layered player-art gate

The technical mouse import and runtime record remains in
[`history/mouse-player-production-2026-09-03.md`](history/mouse-player-production-2026-09-03.md).
The newer art-direction evidence is in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md) and
`experiments/character_binding/FINDINGS.md`:

The ARAP-first plan remains withdrawn. The latest candidate follows
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):

1. **Done.** Preserve useful diagnostics, corrected `shoulder_b`, reachable
   passing pose, moved-part tint, arm-hidden output, and separate shadow.
2. **Rejected.** Relationship-aware stretching still added 592 pixels to a coat
   layer that was already correct.
3. **Done.** Add immutable semantic-layer evidence and a hard gate over decoded
   RGB, alpha additions/removals, and source/final digests.
4. **Done.** Add `mouse_immutable_coat_v1.json` with no coat stretching.
   Source/final coat digests match
   `0400b5084a83957f727c2292d52f481f1af07e29331c628b07c69c2561351fc4`.
5. Review `semantic-arm-immutable-coat-v1` at 48px: immutable coat alone,
   arm-hidden body, moved-arm tint, shadow tint, and current Catacombs bundle.
6. If accepted, keep the coat immutable, separate the tail, then clear the
   149-orphan and four-airborne-fold hard failures.
7. Then second arm, split footwear, legs and tail. Skeleton-conditioned ML stays
   deferred. M4 remains blocked until replacement art passes.

Review frames by tinting the moved part, never by eye — see "How to review this
without getting it wrong" in the experiment doc.

### Track 5: Codex pose conditioning (parallel experiment)

Opened 2026-09-05. Evidence and metrics live in
`experiments/character_binding/out/codex-pose-conditioning-v1/` and the new
section of `experiments/character_binding/FINDINGS.md`. This does not block the
layered-puppet gate above; it is a candidate replacement for its *input*.

**Why it opened.** The layered puppet is a still image at 48px. All three
candidates (`v5`, `v6`, `immutable-coat-v1`) are byte-identical on neutral and
differ by 5-21 pixels of 717 on the other poses. Contact changes the silhouette
by 11 pixels, passing by 13. Only `front_arm` is bound to bones, so eight of ten
bones drive nothing and the striding legs in the pose data render nothing.

**What is settled.**

- Generation supplies pose and limb separation; it will not supply registration.
  Measured twice. Attempt 2 stated an exact canvas, height, ground row, block
  size and palette and the model honoured none of them — Codex satisfied the
  numbers afterwards with ImageMagick.
- Registration aligns frames to a **shared world ground line**, never to each
  figure's bounding box. Normalising bounding boxes deleted the model's real
  flight frame and flattened hip oscillation to zero.
- Skeleton conditioning works through the reference channel when the request
  includes a matched pair: an existing frame plus that frame's skeleton, then
  the target skeleton. Horizontal obedience was complete; vertical travel came
  in at about a third of what was asked.
- The approved source art is a 128x128 sprite stored at 256x256, so rig
  coordinates are in doubled space.

**The tool.** A skeleton animation editor is published as an Artifact:
`https://claude.ai/code/artifact/6ad9861a-8782-4aaa-a7c3-3c1be17af5cb`
("Puppet Rig Bench"). It owns named clips of ordered frames over one shared
27-point / 26-bone skeleton, a draggable floor, add/delete for points and bones,
bone lengths that apply across every frame of a clip, playback, per-frame
tracing underlays, and a cycle check that reports hip oscillation and whether
the lead foot alternates. It exports the clip and a COCO-18 form, and persists
to the artifact's document store at `rig/bench`, so poses can be read back
without pasting. `out/codex-pose-conditioning-v1/rig-bench.json` is a snapshot.

The repository-owned `render_skeleton_rig_review` C++ tool now parses and
validates that snapshot, measures cycle invariants, and emits a standalone
animated HTML review page. It is the reproducible review path when the external
Artifact is unavailable.

The bind pose was traced by hand from the generated `up` frame and verified
against the art; it owns the character's proportions and every new frame starts
as a copy of it. The earlier procedural seeds were deleted — they produced 13px
shins on a 195px figure.

**Pick up here.** The `run` clip now has twelve complete poses mapped
left-to-right across both rows of the supplied running reference. Every frame
poses the head, spine, arms, legs, feet, and tail while retaining the traced
mouse proportions. The C++ review gate reports 17 px of hip oscillation,
alternating lead feet, and 1.07 px maximum integer-coordinate bone-length
drift. Human review accepted the skeleton set on 2026-09-05. The next gate is
a small Codex obedience pilot before spending quota on all twelve poses;
production frame count remains a later runtime/content decision.

### Track 4: finite content polish

Add one floor-scatter silhouette and one foreground-shroud silhouette only where
focused 0.5×/1×/2× evidence shows repetition. Preserve density, layer, sort
order, and collider counts; finish with the complete route gate.

## Runtime invariants

- `RuntimeWorld` borrows one frozen loaded-level graph and owns mutable
  entity-keyed transforms, motion, controller state, presentation, and playback.
- Authored entities persist stable Blueprint-local state keys. Boot resolves the
  six player state handles; fixed ticks do no string or catalogue lookup.
- Idle clips loop at 15 ticks per frame; airborne clips hold their final frame.
- Every player state retains the exact 32×64 collider.
- Scene composition presents runtime transforms and frames without mutating the
  authored level.
- M4 uses latest-wins input/frame snapshots and a bounded I/O executor; SDL and
  GPU upload remain on the main thread.

## Relevant boundaries

- [`architecture.md`](architecture.md): architecture index and domain links.
- [`engine-runtime-plan.md`](engine-runtime-plan.md): runtime M4/M5 design.
- [`environment-artwork-plan.md`](environment-artwork-plan.md): active Track 4
  contracts and remaining content work.
- [`headless-level-review-plan.md`](headless-level-review-plan.md): focused and
  complete level-review procedure.
- [`prop-artwork.md`](prop-artwork.md): current prop lifecycle and remaining
  provider/recovery follow-up.
- [`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):
  the measured layered-puppet problems, the fix order, and the fallbacks.

## Cleanup owed from the Codex pose-conditioning session

- `build/codex-run-sheet/` duplicates what now lives in
  `experiments/character_binding/out/codex-pose-conditioning-v1/`. `build/` is
  generated output and can be deleted.
- The Rig Bench artifact still carries its diagnostics: an on-page log panel,
  pointer counters, per-move logging, and a `diag/log` document written to the
  store beside `rig/bench`. Strip all of it once the editor has been used for a
  full session without incident, and delete the `diag/log` document.
- `out/codex-pose-conditioning-v1/tools/*.py` are stdlib-only analysis scripts
  with no owner. If the registration pass moves into C++ they should be deleted
  rather than maintained in two languages; `measure_sheet.py` encodes the
  pre-registered metrics, so keep it until the C++ gate replaces it.
- The layered-puppet track's hard gates (149 orphans, four folds, 355/177 hole
  counts) are still red and still measure quantities invisible at 48px. Decide
  whether to retire them or re-express them as silhouette-change gates before
  anyone spends more time clearing them.
- The five tracing underlays are derived from a Codex render whose run cycle is
  wrong — the lead foot never alternates. They are useful for limb shape and
  body height only. Do not treat them as an authoring target.

## Non-blocking debt

- Move `SdlWrapper` from `src/common` to `src/platform/sdl` when editor SDL
  composition is next touched; reuse `SdlSubsystem` for editor ownership.
- Replace `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths fully live-verified.
- Windows Codex process transport remains unsupported.
- Dead sprite/blueprint definitions still load at boot: `Player Airborne
  Left/Right Proof`, `kSamusJumpingLeft`, eight `kGrass*` sprites and the whole
  `Samus` blueprint chain. No level entity or blueprint state uses them; they
  survive only as `previous_sprite_id` history. Deleting definitions touches the
  serialized format, so it wants its own change with a migration.
- Catacombs `spawn_point` is (256, 512) while player entity 4 sits at (256, 864),
  so the camera opens 352 px above the mouse until follow corrects it. Content
  fix, not a code fix.
- `animation_artwork_spike`, `animation_artwork_run_manifest`,
  `pose_conditioned_animation_batch` and `run_pose_conditioned_animation` are
  still built and belong to the closed generated-animation experiment. Unlike
  `stage_animation_live_proof`, which was removed because every asset it named
  was gone, these still have live tests; check before removing.

## Last verification

The immutable-coat proof reports zero changed coat pixels, zero alpha additions,
zero alpha removals, and identical source/final digests. Neutral composite
difference remains zero. No attachment or backfill mask is created; the old
full-arm metric reports 745 uncovered pixels and is deliberately not a gate
because pixels outside the coat silhouette may reveal background.

The corrected passing pose casts a separate 288-pixel shadow without mutating
the stored coat. Focused Catacombs review at 0.5×, 1×, and 2× reports no
objective findings.

Hard validation still rejects 149 body-visible orphan pixels and four airborne
folds. Contact has 355 interior holes against 174 neutral and passing has 177.
`layered_puppet_test` passes 23 cases,
`layered_puppet_diagnostics_test` passes 25, and
`semantic_layer_import_test` passes eight; both affected-target gates and
clang-tidy pass.
Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until the replacement art passes.
