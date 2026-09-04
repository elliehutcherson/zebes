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
  ownership, removes the static ghost, and reproduces neutral exactly. Its
  linear mesh remains rejected: most pixels follow rigid bone sections and the
  narrow elbow blend compresses strong poses by roughly 13%. A bounded ARAP A/B
  gate is next; the second arm and legs remain deferred.

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

1. Implement the fixed-input C++ ARAP comparison in
   [`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md).
   Reuse the accepted arm, ownership, underpaint, skeleton, poses, and renderer
   unchanged.
2. Require exact neutral/ownership, connected output, joint targets, no triangle
   inversion, deterministic digests, improved shape retention, and native 48px
   preference over the linear baseline.
3. If ARAP converges without visible improvement, stop solver tuning and test
   one authored elbow corrective. Do not proceed to `handwear-r` first.
4. Only after deformation passes, apply it to the second arm, split footwear,
   acquire complete legs/tail, and bind legs through hip/knee/foot.
5. Keep skeleton-conditioned ML deferred; deformation quality is the current
   blocker. M4 remains blocked until replacement art passes.

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
  fixed-input ARAP A/B implementation and review contract.

## Non-blocking debt

- Move `SdlWrapper` from `src/common` to `src/platform/sdl` when editor SDL
  composition is next touched; reuse `SdlSubsystem` for editor ownership.
- Replace `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths fully live-verified.
- Windows Codex process transport remains unsupported.

## Last verification

The semantic arm uses a 286-vertex/504-triangle C++ grid with a six-pixel elbow
blend. All 18,974 source pixels are singly owned; none are unowned, multiply
owned, or owned outside the source, and the complete neutral composite changes
zero RGBA pixels. Neutral/contact/passing/airborne arm poses each remain one
connected component and retain 100.0%/87.1%/98.7%/86.9% of neutral opaque area.
The ownership and ghost-arm gates pass, but human review rejects the mesh
deformation as too rigid. This visual verdict supersedes the objective
connectivity result and triggers the ARAP comparison.
`layered_puppet_test` passes four cases and `semantic_layer_import_test` passes
seven; both affected-target gates and clang-tidy pass all four supported edited
translation units.
Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until the replacement art passes.
