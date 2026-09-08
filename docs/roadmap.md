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
even with separated identity views.

Skeleton conditioning closed the same way on 2026-09-06: the generator draws
legs far apart because that is what reads as running, and ignores a skeleton
asking for legs together. Measured across two runs
([`history/codex-pose-conditioning-2026-09-06.md`](history/codex-pose-conditioning-2026-09-06.md)).

Generation is no longer asked to infer a pose. The interactive editor poses the
character directly and generation is asked only to clean up the result.
Imported and manually authored sheets remain the only production animation
source.

### Animation experiment cleanup complete

Done 2026-09-06, about 7,000 lines removed. The closed generated-animation and
skeleton-conditioning tooling is gone, the strict JSON readers have one
implementation, the editor page is a real HTML file, and tracked inputs are
separated from generated output. Narrative in
[`history/repository-cleanup-2026-09-06.md`](history/repository-cleanup-2026-09-06.md).

One decision was deferred rather than made and is listed under "Open decisions"
in [`handoff.md`](handoff.md): whether the layered-puppet hard gates measure
anything visible at 48px. The second, who should own `kReferenceJointMapping`,
was settled by deleting the file it lived in.

### Puppet document editor

Started 2026-09-07, uncommitted on `animation-recipe-lifecycle`. A puppet is now
one document holding its artwork, skeleton, parts and frames, edited only
through named commands. Three ways in share that one path: `puppet_edit` for an
agent, `serve_puppet_editor` for a browser, and a five-step page. Two of the
five legacy specs were migrated to documents and deleted, proven byte-identical
first; the three See-through specs remain and render through `--spec`.

The second editor was retired on 2026-09-07, so there is one now. Its tracked
state held nothing the documents did not already have. The same day gave the
server defaults for every flag, made a root joint drag slide the whole rig,
added `scale_to_size` so a puppet can be stretched onto artwork at another
resolution, and added `set_part_bones` so a part built on the wrong bone is
re-pointed instead of retraced.

Remaining work is listed under "what the puppet editor still needs" in
[`handoff.md`](handoff.md). None of it blocks the art gate.

### Production pipeline complete; player art gate reopened

Pure frame-set processing, retained-source recipe lifecycle, headless curation,
and the six-state player asset graph are complete. The first authored Blender
mouse proved identity, registration, import, playback, and Catacombs
integration, but human review rejected its flat primitive style.

The C++ layered path restores one accepted See-through arm and separates moving
arm, static coat, and cast shadow. User review showed that See-through had
already generated the desired coat-without-arms layer; stretching it to satisfy
full-arm backfill only made the coat too wide. The latest immutable-coat
candidate changes zero coat pixels and no alpha while retaining exact neutral
and a separate reachable passing shadow.

The ARAP-first plan remains withdrawn. Follow
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):

1. **Review the immutable coat candidate.** Compare imported/final coat,
   arm-hidden body, moved-arm tint, shadow tint, native passing frame, and
   current Catacombs evidence. Do not re-enable stretching for pixels outside
   the approved coat alpha.
2. **Clear the remaining one-arm gates after acceptance.** Separate the tail,
   remove 149 static orphan pixels, re-author airborne, and use MLS only if its
   four artwork folds remain. Resolve pose-local holes relative to neutral.
3. **Finish the layered source gate.** Apply the accepted method to the second
   arm, split footwear, obtain complete legs/tail, and bind legs through
   hip/knee/foot. Keep skeleton-conditioned ML deferred.
4. **Render and import replacement clips.** Preserve the stable Blueprint, six
   state keys, 32×64 collider, timing, and playback contracts.
5. **Editor import flow.** Expose the proven headless import boundary without
   adding remote animation generation.
6. **Human gate.** Record idle, locomotion, direction, jump/fall, landing,
   slopes, walls, and ceilings in Catacombs.

Only torso and one arm currently consume artwork. The four-pose evidence remains
a one-arm stress test, not a gait test.

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

- Reuse `SdlSubsystem` for editor SDL ownership when that composition next
  changes. `SdlWrapper` moved under `src/platform/sdl` on 2026-09-07.
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
