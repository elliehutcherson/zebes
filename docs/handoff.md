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
- **Track 5 — runtime/animation.** Runtime Milestones 1–3 and pure frame-set
  processing are complete. `AnimationFrameSetPipeline` owns source-neutral
  sheet extraction, common registration and scale, shared palette treatment,
  alpha/geometry validation, deterministic packing, ordered timing metadata,
  and loop/hold output. Generated animation research remains deprecated;
  imported/manual frame sheets are the only production source path.

The reusable ordered-reference generation boundary remains supported for
OpenAI, Codex, headless generation, and redraw; it is not an animation roadmap
item. The deprecated evidence is indexed under
[`history/`](history/README.md).

## Pick up next

### Track 5: recipe and bundle lifecycle

Start at Milestone 4 in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md):

1. Add the versioned retained-source frame-set recipe and strict parser.
2. Prepare the complete Texture, Sprite, and stable Blueprint-state binding
   change without mutating stores.
3. Commit create, regenerate, and delete transactionally with stale-snapshot
   refusal and reverse-order compensation.
4. Cover every persistence failure boundary and shipped-definition migration.

Then implement headless curation, editor import controls, and the first complete
left/right idle/run/airborne player set. M4 waits for that set's commit, restart,
and live Catacombs gate.

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

`animation_frame_set_pipeline_test` passes all seven focused cases; scoped
clang-tidy passes the pipeline and test translation units. The last broad
boundary remains commit `18dcba6`: all CMake targets, 147 C++ test executables,
and 95 Python tests passed before the documentation cleanup. Continue using
focused verification per `AGENTS.md`; do not rerun the full suite merely because
a new conversation started.
