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
  advances a bounded fixed-step free-fly simulation, composes the shared scene,
  and presents through SDL. Milestone 2 has a runtime-world foundation,
  deterministic player input intent, the exact 32×64 player collider contract,
  and allocation-free static AABB-versus-`TileShape` overlap queries. The
  movement solver and runtime integration remain.

The most recent runtime cleanup is split into three reviewable commits:

- `b741ee9` — status propagation, render option aggregates, runtime-world
  construction cleanup, and a small `GameEngine::Run`;
- `a6a3bf0` — `AssetWorkspace`-owned level-graph coordination through
  `LevelAssetLoader`, with `LoadedLevelContent` separated from
  `LevelRenderResources`; and
- `7aa880f` — platform-neutral `GameRuntime` dependencies and RAII SDL ownership
  in `SdlGameHost`.

## Pick up next

### Engine: complete Milestone 2

1. Add a pure swept AABB-versus-tile movement solver. Query only the player's
   authored world layer, use `tile_shape_geometry` as the shape authority,
   define one-way behavior explicitly, and make contact ordering deterministic.
2. Integrate intent, acceleration, velocity, sweep/response, grounded state,
   and jump behavior into `RuntimeWorld` without mutating the authored `Level`.
3. Replace the runtime's free-fly-only simulation path with a player simulation
   that owns `RuntimeWorld`, consumes one fixed-tick input snapshot, and drives
   the follow camera.
4. Compose entity positions from runtime transforms so movement appears without
   writing transient state back into serialized entities.
5. Gate the pass with headless ground, wall, ceiling, slope, one-way,
   high-speed/tunneling, deterministic-order, and failure-path tests before the
   live Catacombs run/jump review.

The likely shared files are `src/game/game_runtime.*`, `src/game/game_scene.*`,
and their tests. Coordinate those before running asset work that changes the
same production level; generation, curation, and artwork libraries otherwise
remain an independent track.

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

- Complete `game_engine_test`, `simulation_pacer_test`,
  `tile_collision_test`, `runtime_world_test`, and scene-composition consumer
  tests passed.
- `loaded_level_assets_unit_test`, `game_level_assets_test`, and all three
  `AssetWorkspace`-affected executables passed.
- The headless `game_runtime_test`, SDL input and texture-store tests, and the
  display-backed `sanity_test` passed; `run_game` built successfully.
- Every edited C++ translation unit passed scoped clang-tidy and
  `git diff --check` was clean.

