# Active handoff

Updated 2026-09-06. [`roadmap.md`](roadmap.md) owns sequencing; this file is the
short resume point. Completed narratives live in [`history/`](history/README.md).

## Current state

Two tracks proceed independently.

**Track 4 — environment and content.** The Catacombs route has zone fades,
finite parallax coverage through 0.5×, independent masonry, distributed
player-scaled decor, floor and foreground variants, and a middle ceiling frieze.
The complete route review reports no objective findings. Remaining silhouette
variation is non-blocking polish.

**Track 5 — runtime and animation.** Runtime Milestones 1–3, frame-set
processing, the recipe lifecycle, and headless curation are complete. The
production player-art gate is still open: the shipped mouse passed every
technical check and failed art direction, and replacement art has not passed
yet. M4 is blocked until it does.

Generated animation is closed twice over. Coherent sheets failed live motion
review, and skeleton conditioning failed because the generator will not draw a
pose that contradicts what it has learned running looks like
([`history/codex-pose-conditioning-2026-09-06.md`](history/codex-pose-conditioning-2026-09-06.md)).
The replacement approach poses the character directly in the interactive editor
and asks generation only to clean up the result.

The animation-experiment cleanup workstream is done
([`history/repository-cleanup-2026-09-06.md`](history/repository-cleanup-2026-09-06.md)).

## Pick up next

### Track 5: the layered player-art gate

The measured problems and the fix order are in
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md).
The ARAP-first plan remains withdrawn.

1. **Review `semantic-arm-immutable-coat-v1` at 48px** — immutable coat alone,
   arm-hidden body, moved-arm tint, shadow tint, and the current Catacombs
   bundle. This is the open decision.
2. If accepted, keep the coat immutable, separate the tail, then clear the
   149-orphan and four-airborne-fold failures.
3. Then the second arm, split footwear, legs and tail.
4. Render and import replacement clips, preserving the stable Blueprint, six
   state keys, 32×64 collider, timing and playback contracts.

Review frames by tinting the moved part, never by eye — see "How to review this
without getting it wrong" in the experiment doc.

Only the torso and one arm consume artwork today. The four-pose evidence is a
one-arm stress test, not a gait test.

### Track 5: interactive pose authoring

`experiments/character_binding/README.md` has the commands. The editor gives a
twelve-frame dropdown, previous/next, 8 FPS playback, three edit modes, 1–6×
zoom, mesh paint and erase, and transparent 256px PNG export.

- `Move pose joints` deforms the composite.
- `Calibrate source joints` moves the bind skeleton over an unchanged source.
- `Define attachment mesh` supplies a full-canvas 4px triangle grid per limb;
  the traced region is only a seed. Green means painted mesh, no overlay means
  none. Pose rendering removes only painted triangles from the static source,
  then deforms those exact source pixels.

The 23-point `rig-bench.json` clip is the only visible authoring skeleton and
follows the selected frame in every mode. Thirteen mapped joints are draggable
circles driving the internal mesh controls; the rest render as colored squares.
Frames with no saved override show `Canonical`; the first pose-joint edit marks
that frame `Authored`, and only authored frames are written under
`frame_overrides`.

Screen-left is the **front** arm and screen-right is the **rear** arm. Draw
order encodes rear arm, both legs, visible body and coat, then front arm.

Durable state is `editor_states/mouse_run_v1.json`. The loopback
`serve_layered_puppet_editor` validates it against source and puppet-contract
digests and writes it atomically. Browser edits are a local crash-recovery draft
until **Save to Repo**; **Revert from Repo** restores the tracked state. A
mismatched source or topology returns 409 without touching the file.

**Set anchor and regenerate** applies each canonical joint's delta from the
chosen anchor to the authored source joints. The anchor frame stays exact and
per-frame edits are stored as offsets. Re-anchoring clears those offsets after
confirmation but preserves source calibration and painted meshes.

### Track 4: finite content polish

Add one floor-scatter silhouette and one foreground-shroud silhouette only where
focused 0.5×/1×/2× evidence shows repetition. Preserve density, layer, sort
order and collider counts; finish with the complete route gate.

## Runtime invariants

- `RuntimeWorld` borrows one frozen loaded-level graph and owns mutable
  entity-keyed transforms, motion, controller state, presentation and playback.
- Authored entities persist stable Blueprint-local state keys. Boot resolves the
  six player state handles; fixed ticks do no string or catalogue lookup.
- Idle clips loop at 15 ticks per frame; airborne clips hold their final frame.
- Every player state retains the exact 32×64 collider.
- Scene composition presents runtime transforms and frames without mutating the
  authored level.
- M4 uses latest-wins input/frame snapshots and a bounded I/O executor; SDL and
  GPU upload stay on the main thread.

## Where things live

`experiments/character_binding/` splits three ways. `inputs/` is tracked and
holds what the tools consume: the approved mouse, the isolated running source
whose coordinates own the cutout masks, the See-through layers, and the rig.
`evidence/` is tracked and records closed experiments. `out/` is ignored whole
and holds renders any tool can write again. `ls -t out | head` shows the newest.

- [`architecture.md`](architecture.md): architecture index and domain links.
- [`engine-runtime-plan.md`](engine-runtime-plan.md): runtime M4/M5 design.
- [`environment-artwork-plan.md`](environment-artwork-plan.md): Track 4
  contracts and remaining content work.
- [`headless-level-review-plan.md`](headless-level-review-plan.md): focused and
  complete level-review procedure.
- [`prop-artwork.md`](prop-artwork.md): prop lifecycle and provider follow-up.
- [`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):
  the layered-puppet problems, the fix order, and the fallbacks.

## Open decisions

- **The layered-puppet hard gates are still red and may be measuring the wrong
  thing.** 149 orphans, four folds, and 355/177 hole counts are all invisible at
  48px, the size the sprite ships. Decide whether to retire them or re-express
  them as silhouette-change gates before spending more time clearing them.
- **`kReferenceJointMapping` in `layered_puppet_editor.cc`** is a hardcoded
  13-entry array mapping the 23-point authoring rig onto the deformation model.
  It wants one validated data owner before a second rig exists. Preserve the
  current names and image-space arm ownership.

## Non-blocking debt

- Move `SdlWrapper` from `src/common` to `src/platform/sdl` when editor SDL
  composition is next touched; reuse `SdlSubsystem` for editor ownership.
- Replace the `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths are live-verified.
- Windows Codex process transport is unsupported.
- The `Samus` and `Grass1-3` blueprint chains are kept as test fixtures and no
  level places them. Six of their sprites were pinned by recipe rollback records
  until `previous_sprite_id` was removed; nothing pins them now, so they can go
  whenever the fixtures are no longer wanted.
- Catacombs `spawn_point` is (256, 512) while player entity 4 sits at (256, 864),
  so the camera opens 352 px above the mouse until follow corrects it. Content
  fix, not a code fix.
- `build/` holds stale generated output including `codex-run-sheet/`. It is
  ignored and safe to delete entirely.

## Last verification

The immutable-coat proof reports zero changed coat pixels, zero alpha additions
or removals, and identical source/final digests. Neutral composite difference is
zero. No attachment or backfill mask is created; the old full-arm metric reports
745 uncovered pixels and is deliberately not a gate, because pixels outside the
coat silhouette may reveal background.

The corrected passing pose casts a separate 288-pixel shadow without mutating
the stored coat. Focused Catacombs review at 0.5×, 1× and 2× reports no
objective findings.

Hard validation still rejects 149 body-visible orphan pixels and four airborne
folds. Contact has 355 interior holes against 174 neutral; passing has 177.

The full gate passes: 159 test executables and 51 Python tests. The editor page
regenerates byte-identical from the tracked inputs at
`4d27ace1c8b2447fc579c8779b44969348dc7a28fd2e802fa783a680b01bc3e8`.

Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until replacement art passes.
