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
  review rejected the authored Blender mouse's flat primitive style. The C++
  layered path now restores one accepted See-through arm, enforces exclusive
  ownership, removes the static ghost, and reproduces neutral exactly. Three
  problems were measured and two are fixed. The elbow no longer folds, and
  ownership now comes from the layer's own alpha plus a reach limit off the bone,
  so no arm pixels stay stuck to the body and the tail no longer swings with the
  arm. Backfill is closed by stretching the coat outward. Correcting the shoulder
  joint then exposed two gates that the old rig had masked. The second arm and
  legs remain deferred.

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

The ARAP-first plan is withdrawn; ARAP addressed the smallest of three problems.
Steps 1 through 4 are done. Follow
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):

1. **Done.** Diagnostics and four opt-in gates. Frame digests unchanged.
2a. **Done.** Skinning blends the bone angle instead of averaging two rotated
   positions.
2b. **Done.** Mesh trimmed to the arm, blend band widened away from the bone via
   `joint_blend_lateral_scale`. Zero folds, exact neutral, smooth elbow.
   Retained area is retired as a gate: removing the folds lowered it to
   81.9%/81.0% while the picture improved, because a bent tube covers less area
   than a straight one.
3. **Done.** Ownership derived from the layer's own alpha plus a reach limit off
   the bone chain, wide at the shoulder and tight at the hand. Orphans 105 to 0,
   backfill 876 to 598, and the tail no longer swings with the arm.
4. **Done.** Backfill by stretching. "Stop clipping" turned out to change
   nothing — the clip never removed those pixels, `topwear` just does not paint
   there. `StretchLayeredPuppetBackfill` grows the underpaint outward from its
   interior, not its contour, so the fill carries coat colour rather than the
   dark outline. Uncovered 745 to 0, contact interior holes 355 to 194.
   `require_backfill_coverage` is on.
5. Review at 48px. If it still fails, drive the existing mesh with MLS rather
   than replacing it, then bounded biharmonic weights, then ARAP.
6. Only after that passes: second arm, split footwear, legs and tail.
7. Skeleton-conditioned ML stays deferred. M4 blocked until the art passes.

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

The semantic arm uses a mesh trimmed to its artwork with the blend band widening
away from the bone. All 18,974 source pixels are singly owned and the neutral
composite changes zero RGBA pixels. Read vertex, triangle and area figures from
`out/semantic-arm-v5/manifest.json`; they move whenever ownership changes.

Retained area is no longer a gate. Removing every fold lowered it while the elbow
visibly improved, so it was rewarding rigidity.

Backfill now covers every pixel the arm exposes, 745 of them by stretching rather
than painted artwork — that count is reported each run and should fall when real
artwork arrives. Two gates still fail on purpose after the shoulder correction:
149 orphan pixels and 4 folded triangles on airborne, neither tunable with the
current knobs. `require_no_interior_holes` stays off; the source art has 174
enclosed gaps of its own.

`layered_puppet_diagnostics_test` passes 25 cases and `layered_puppet_test` 13;
the affected-target run and clang-tidy pass on both edited library sources.
Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until the replacement art passes.
