# Active handoff

Updated 2026-09-03. [`roadmap.md`](roadmap.md) owns sequencing; this file is the
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
  review rejected the authored Blender mouse's flat primitive style. The layered
  2D renderer and proof publication now live in C++; obsolete Python binding and
  ComfyUI animation-control code is removed. See-through V3 produced useful
  completed arm, boot, and coat layers from the approved mouse, but failed legs,
  tail, ears, and hair. This passes a targeted candidate-generation gate, not
  the production-player gate.

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

1. Add a C++ adapter for See-through's RGBA/JSON output. Accept only the two arm
   layers, footwear, and coat candidate; preserve original visible pixels and
   reject the human-ear, hair, and headwear hallucinations.
2. Split footwear by connected component. Skin each complete arm to
   shoulder/elbow/wrist and each complete leg to hip/knee/foot with deterministic
   two-bone mesh weights. Add ARAP only if the four-pose evidence shows joint
   collapse.
3. Obtain missing leg and tail layers through targeted completion or authored
   correction, then run the neutral/contact/passing/airborne C++ gate. Do not
   add skeleton-conditioned ML until repeated characters prove semantic
   ownership remains the blocker.
4. Render and import replacement clips without changing the stable Blueprint,
   state keys, timings, playback, or 32×64 collider. Add editor import controls,
   then record live-transition acceptance. M4 remains blocked until art passes.

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

The C++ layered-puppet proof reproduces all four prior Python frame buffers
pixel-for-pixel; `layered_puppet_test` passes three focused cases and supported
translation units pass clang-tidy. See-through revision
`7f139bb25c46a0c8ac720d95ddab185fcda5451c` completed the exact mouse at
1280px in 520.87 seconds after stale ComfyUI weights were unloaded. Visual
review accepts its arm, footwear, and coat candidates and rejects its empty or
human-anatomy layers.
Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until the replacement art passes.
