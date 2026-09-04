# Roadmap

Current work and dependency order only. Completed narratives are indexed in
[`history/`](history/README.md); [`handoff.md`](handoff.md) is the resume point.

## Track state

| Track | Scope | State |
|---|---|---|
| 0 | Tooling and slope rename | Done |
| 1 | clang-tidy backlog | Done |
| 2 | Repository hygiene | Done |
| 3 | Terrain carry-overs | Done |
| 4 | Layers and production environment/content | In progress: finite content polish remains |
| 5 | Game runtime | In progress: M1–M3, processing, recipe lifecycle, and curation complete; player art gate reopened |

Tracks 4 and 5 may proceed in parallel when they do not edit the same production
level, player Blueprint, or review evidence.

## Track 4 — environment and content

### Accepted foundation

- Ordered world layers and within-layer entity sort order.
- Standalone parallax themes, compositions, and zone-owned theme references.
- Imported/generated parallax artwork lifecycle, editor workflow, and retained
  provenance.
- Zone fades with platform-neutral resolution and live visual acceptance.
- Production Catacombs route through 0.5× finite parallax coverage.
- Independent Catacombs masonry terrain.
- Distributed player-scaled props, floor/foreground A/B silhouettes, and the
  middle ceiling frieze.
- Headless focused and complete route review with streamed atomic publication.

Current contracts live in
[`environment-artwork-plan.md`](environment-artwork-plan.md),
[`headless-curation.md`](headless-curation.md), and
[`headless-level-review-plan.md`](headless-level-review-plan.md).

### Next content pass

1. Use focused 0.5×/1×/2× evidence to locate actual repetition.
2. Add one floor-scatter silhouette and one foreground-shroud silhouette.
3. Preserve entity density, world layer, sort order, and collider counts.
4. Rebuild the environment byte-stably.
5. Finish with the complete Catacombs route review.

Further visual variants are polish, not a Track 5 prerequisite.

## Track 5 — game runtime

### Milestones 1–3 complete

- **M1:** `run_game`, read-only runtime workspace, frozen render graph,
  fixed-step pacing, free-fly bootstrap, shared scene composition, SDL host.
- **M2:** `RuntimeWorld`, player input/intent, continuous sparse-tile movement,
  slopes, simultaneous and one-way contacts, runtime transforms, camera follow.
- **M3:** stable Blueprint state keys, boot-checked state handles, idle/run/
  airborne selection, remembered facing, loop/hold Sprite playback, and live
  Catacombs acceptance.

Detailed runtime threading and ownership decisions remain in
[`engine-runtime-plan.md`](engine-runtime-plan.md).

### Generated animation closed

Coherent generated sheets failed live motion review. Independently generated
pose-conditioned frames failed identity, proportion, and pose-phase consistency,
even with separated identity views. The experiment is deprecated historical
evidence and is not a dependency or follow-up.

Imported and manually authored sheets are the only production animation source.

### Production pipeline complete; player art gate reopened

Pure frame-set processing, retained-source recipe lifecycle, headless curation,
and the six-state player asset graph are complete. The first authored Blender
mouse proved identity, registration, import, playback, and Catacombs
integration, but human review rejected its flat primitive style.

The C++ layered path restores one accepted See-through arm, enforces exclusive
ownership, removes the static ghost, and reproduces neutral exactly. See-through
also fails or hallucinates the mouse's legs, tail, ears, and hair.

The ARAP-first plan is withdrawn. Measurement showed the visible damage was in
how the layers were cut apart, not in the solver. Follow
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):

1. **Close the backfill.** Use every pixel the See-through coat paints, then
   stretch the surrounding layer to cover what is left. 745 px of the arm still
   has nothing behind it.
2. **Clear the two failing gates.** Correcting the shoulder joint left 149 orphan
   pixels and 4 folded triangles, and neither is tunable with the current knobs.
   The tail needs to become its own part. Reach for MLS driving the existing mesh
   before any other solver.
3. **Finish the layered source gate.** Apply the method to the second arm, split
   footwear, obtain complete legs/tail, and bind legs through hip/knee/foot.
   Keep skeleton-conditioned ML deferred.

Note when reviewing: only 3 of the 10 bones are bound to a part, so the legs and
head do not move in any pose. The four-pose evidence is a standing mouse with one
arm moving, not a gait test.
3. **Render and import the replacement clips.** Preserve the stable Blueprint,
   six state keys, 32×64 collider, timing, and playback contracts.
4. **Editor import flow.** Expose the proven headless import boundary without
   adding remote animation generation.
5. **Human gate.** Record idle, locomotion, direction, jump/fall, landing,
   slopes, walls, and ceilings in Catacombs.

### M4 — thread split

Start only after the production player set passes:

- move `GameEngine` to `EngineRunner`;
- exchange input and immutable frames through latest-wins slots;
- load/transition assets through a bounded I/O executor;
- marshal GPU uploads to the SDL main thread;
- add audio ownership and clean shutdown ordering;
- preserve all M1–M3 headless behavior.

Gate: transition without a main-thread frame hitch and clean TSan shutdown.

### M5 — host tuning

After measurement, expose simulation rate, queue capacities, and thread options
through validated `EngineConfig`. Do not preconfigure unmeasured knobs.

## Active non-blocking debt

- Move `SdlWrapper` under `src/platform/sdl` when editor SDL composition changes.
- Replace the Level Editor linear entity lookup only after profiling justifies a
  spatial index.
- Add shared confirmation UI to tile deletion.
- Quarantine superseded cave assets through reference-checked lifecycle APIs.
- Finish credential-gated OpenAI integration and the real Codex editor
  accept/discard/cancel/shutdown walk.
- Add Windows Codex process transport before advertising Windows support.

## Settled decisions

Do not reopen without new evidence:

- CMake is the only build system.
- SDL/GPU ownership stays on the main thread.
- Runtime definitions are immutable; instance state is separate.
- `RuntimeWorld` uses flat entity-ID registries, not an ECS.
- Parallax layers and world layers remain distinct systems.
- One level resolves one tileset.
- Definition formats are strict and have no optional fields.
- Provider details do not cross provider-neutral image-generation contracts.
- Imported/manual animation does not depend on remote generation success.
- Large review bundles stream through atomic publication rather than retaining
  all decoded frames.
