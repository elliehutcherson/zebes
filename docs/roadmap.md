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
| 5 | Game runtime | In progress: M1–M3 complete; production player frame-set pipeline precedes M4 |

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

### Next: production frame-set pipeline

Follow [`animation-artwork-pipeline.md`](animation-artwork-pipeline.md):

1. **Pure frame-set processing.** Promote source-neutral extraction, shared
   registration, palette treatment, alpha/geometry validation, deterministic
   packing, and Sprite frame metadata.
2. **Recipe and bundle lifecycle.** Versioned retained-source recipe;
   transactional Texture/Sprite/Blueprint-state create, regenerate, and delete;
   stale-snapshot refusal and compensation.
3. **Headless curation.** Native frames, alignment/contact overlays, loop or
   hold evidence, focused Catacombs context, byte-stable re-review.
4. **Editor import flow.** Sheet layout, timing, origin/contact line, playback
   mode, and stable Blueprint state bindings. No remote animation generation.
5. **Production player set.** Import or manually author left/right idle, run,
   and airborne clips while preserving the exact 32×64 collider.
6. **Human gate.** Commit, restart, and exercise idle, locomotion, direction,
   jump/fall, landing, slopes, walls, and ceilings in Catacombs.

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
