# Active handoff

Updated 2026-08-31. [`roadmap.md`](roadmap.md) owns sequencing; this file is the
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
  complete. The first production player asset is an authored mouse with
  left/right idle, run, and airborne clips. Six retained-source recipes own its
  Texture, Sprite, timing, playback, and stable Blueprint bindings. Generated
  animation remains deprecated; imported/manual frame sheets are the only
  production source path.

The reusable ordered-reference generation boundary remains supported for
OpenAI, Codex, headless generation, and redraw; it is not an animation roadmap
item. The deprecated evidence is indexed under
[`history/`](history/README.md).

## Pick up next

### Track 5: finish the production-player gate

The mouse asset set and headless review pass
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md) Milestones 5
and the asset portion of 7:

1. Add the Milestone 6 editor import controls over the proven headless import
   and transactional lifecycle.
2. Grant Terminal macOS Screen Recording permission, then record the remaining
   interactive idle, direction, run, jump/fall, and landing acceptance in
   Catacombs. The runtime already loads and presents the production graph.
3. Commit/restart acceptance then unblocks Runtime M4.

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

## Non-blocking debt

- Move `SdlWrapper` from `src/common` to `src/platform/sdl` when editor SDL
  composition is next touched; reuse `SdlSubsystem` for editor ownership.
- Replace `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths fully live-verified.
- Windows Codex process transport remains unsupported.

## Last verification

`animation_frame_set_recipe_test` passes all seven cases and
`sprite_reviewer_test` passes all three. The six-clip mouse import passed every
pipeline and persistence gate; repeated persisted run-right review output was
byte-identical. Focused Catacombs review resolves the mouse at 0.5×, 1×, and 2×,
and `run_game` loaded the production graph continuously before a clean stop.
Interactive screen evidence remains blocked only by the Terminal Screen
Recording permission described above.
