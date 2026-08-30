# Active handoff

Updated 2026-08-29. [`roadmap.md`](roadmap.md) remains the source of truth for
sequencing; this document records the concrete state needed to resume either
active workstream. Older chronological handoffs remain under
[`history/`](history/README.md).

## Current state

Two tracks intentionally proceed in parallel:

- **Asset/content track:** the production Catacombs route has accepted finite
  parallax coverage through 0.5×, its own deterministic masonry terrain,
  distributed player-scaled decor, A/B floor and foreground silhouettes, and
  a distinct middle ceiling frieze. The complete 343-artifact route review has
  no objective findings. More variants are content polish, not an engine
  prerequisite.
- **Game-runtime track:** Milestone 1 is complete. `run_game` loads Catacombs,
  and presents through SDL. Milestone 2 is complete: the
  fixed-tick player simulation owns `RuntimeWorld`, uses continuous collision
  against a sparse local tile query, resolves simultaneous and one-way contacts,
  follows the player camera, and renders runtime transforms without mutating the
  authored level. The live Catacombs movement review was accepted on 2026-08-29.
  Milestone 3 is also complete: it has stable semantic Blueprint state keys,
  player idle/run/airborne selection with remembered facing, and a six-state
  multi-frame proof asset in Catacombs. Its automated and live gates were
  accepted on 2026-08-29.

The most recent runtime cleanup is split into three reviewable commits:

- `b741ee9` — status propagation, render option aggregates, runtime-world
  construction cleanup, and a small `GameEngine::Run`;
- `a6a3bf0` — `AssetWorkspace`-owned level-graph coordination through
  `LevelAssetLoader`, with `LoadedLevelContent` separated from
  `LevelRenderResources`; and
- `7aa880f` — platform-neutral `GameRuntime` dependencies and RAII SDL ownership
  in `SdlGameHost`.

The M3 closeout is split into five reviewable commits:

- `d247511` — animation artwork pipeline plan and sequencing;
- `a9f0d23` — semantic player animation states and runtime presentation;
- `a1abcec` — single-source runtime Blueprint ownership;
- `38ebbd3` — stable Abseil hashed runtime catalogs; and
- `bc61c28` — explicit loop/hold-last Sprite playback and corrected proof timing.

## Pick up next

### Engine: accepted Milestone 3 boundary

The reusable animation cursor now belongs to the engine. The frozen loaded-level
graph is the single runtime source of Blueprint definitions and all referenced
Sprites, textures, and Colliders; `RuntimeWorld` borrows it and owns only
per-instance bindings, playback, and other mutable runtime state. Scene
composition presents runtime-selected sprites and frames without mutating
authored entities. Sprites explicitly select looping or final-frame holding;
headless tests cover both modes, multi-frame timing, state changes, reset
behavior, invalid transitions, and the complete render-composition path.

Blueprint states now have required, migrated, unique semantic keys. The key is
stable programmatic identity within one Blueprint; the name remains editable
display text. Authored entities persist the key instead of a numeric state
index. Runtime boot resolves `idle-*`, `run-*`, and `airborne-*` to checked
handles, then fixed ticks select among those handles from grounded state,
horizontal velocity, and remembered facing. The Catacombs player is the
six-state Player Animation Proof, which reuses existing multi-frame artwork
while retaining one exact 32x64 collider across every state. Its idle clips use
15 ticks per frame and its airborne clips hold their final frame.

The live Catacombs gate accepted the slower idle, looping run, hold-last
airborne, left/right transitions, landing reset, and unchanged collision
behavior on 2026-08-29. No further M3 engine work is pending. Preserve these
runtime and serialization contracts while replacing the proof artwork.

The likely shared files are `src/game/game_runtime.*`, `src/game/game_scene.*`,
and their tests. Coordinate those before running asset work that changes the
same production level; generation, curation, and artwork libraries otherwise
remain an independent track.

### Assets/runtime: start the animation artwork feasibility gate

Do not move directly from the M3 live proof to the M4 thread split. Animation
artwork generation is a required, non-trivial follow-on milestone. Begin with a
bounded feasibility gate before building resource managers or editor UI: compare
one coherent generated frame sheet with an imported/manual baseline under the
same fixed canvas, origin, palette, frame-count, and loop contract. Never issue
independent generation requests per frame.

The next agent should start at milestone 2 in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md): define the
fixed player reference, idle and locomotion clip contracts; produce coherent
generated-sheet and imported/manual candidates outside the asset root; and use
a disposable platform-neutral processor to compare registration, palette,
clipping, adjacent frames, and loop closure. Use one direction for the bounded
spike; M3's separate left/right Sprite bindings remain the runtime contract.
Do not add an `AnimationArtworkRecipe`, persistence API, manager, or editor
panel until that evidence settles the source-sheet and frame-geometry shape.

If the generated clip cannot preserve identity and temporal coherence, keep the
imported/manual source path and reassess the generation technique instead of
building a polished workflow around bad candidates. Once feasible, implement
the deterministic frame-set processor, retained-source recipe and transactional
bundle lifecycle, headless animation review, provider/editor flow, and first
production player set described in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md). M4 begins only
after that set can be processed, committed, regenerated byte-stably, and
verified live.

The M3 closeout baseline passed 144 C++ test executables, 95 Python tests, the
SDL UI sanity test, scoped clang-tidy, definition migration dry-run, and
`git diff --check`. Re-run only the affected targets during the feasibility
spike; the full suite is required once serialized recipe or broadly consumed
resource contracts change.

### Assets: next finite variation pass

Generate one additional floor-scatter silhouette and one additional
foreground-shroud silhouette only where the existing focused evidence shows a
repeat. Preserve the current entity density, player-scale contract, layer,
sort order, and collider counts. Use transient focused candidate review at
0.5×, 1×, and 2× before persistence, then rebuild the environment
byte-stably and finish with the complete 343-artifact route gate. Do not place
world-space stains on parallax masonry; wall aging must be baked into the wall
artwork or share its parallax scroll factor.

## Known technical debt and follow-up

- `SdlWrapper` still lives under `src/common` although it is a native platform
  adapter. Move it under `src/platform/sdl` when touching the editor's SDL
  composition, and reuse `SdlSubsystem` there instead of retaining the editor's
  separate manual `SDL_Init`/`SDL_Quit` lifecycle. This does not block M2.
- `viewport_model.h` still has a linear lookup that should become a spatial
  index only when measured level size makes it material.
- Tile deletion has reference-safe refusal but still lacks the shared
  `ConfirmPrompt` used by other destructive editor actions.
- The accepted cave-composition gates retain experimental Slant, Floor Ridge,
  pilot, and pre-pilot catalog assets. Reprocess promoted assets through the
  current cave-palette pipeline and quarantine unused graphs through the
  reference-checked lifecycle; do not delete their files directly.
- Provider follow-up remains deliberately outside CI: finish the real Codex
  editor accept/discard/cancel/shutdown walk, run the credential-gated OpenAI
  integration check, and add Windows Codex process transport before claiming
  Windows support.

The raw `SDL_Texture*` used by an unaccepted Texture Editor preview is an
intentional transient-resource exception, not debt. The optional project skill
around the documented headless CLI loop is likewise ergonomics work, not a
required runtime or asset feature.

## Last verified checkpoint

- The complete collision dependency slice passed: `tile_collision_test`,
  `tile_movement_test`, `runtime_world_test`, `player_simulation_test`,
  `game_level_assets_test`, and `game_runtime_test`.
- All eight `scene_composition`-affected executables and all four
  `input_manager`-affected executables passed. `level_test`,
  `viewport_model_test`, and `viewport_interaction_test` also passed after
  moving sparse read-only tile access to `objects/level`.
- `run_game` built successfully, and the live Catacombs movement review was
  accepted on 2026-08-29.
- Every edited production C++ translation unit passed one scoped clang-tidy
  invocation; `git diff --check` was clean.
