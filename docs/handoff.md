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

The most recent runtime cleanup is split into three reviewable commits:

- `b741ee9` — status propagation, render option aggregates, runtime-world
  construction cleanup, and a small `GameEngine::Run`;
- `a6a3bf0` — `AssetWorkspace`-owned level-graph coordination through
  `LevelAssetLoader`, with `LoadedLevelContent` separated from
  `LevelRenderResources`; and
- `7aa880f` — platform-neutral `GameRuntime` dependencies and RAII SDL ownership
  in `SdlGameHost`.

## Pick up next

### Engine: implement Milestone 3 animation playback

Move the reusable animation cursor out of the editor dependency boundary, add
per-entity playback and blueprint-state selection to `RuntimeWorld`, and compose
the runtime-selected sprite frame without mutating authored entities. Expand the
loaded-level graph to retain referenced blueprints and all state sprites and
colliders needed after boot. Do not infer behavior from display names or make
game code depend on `editor/animator.h`.

The current Mouse Player Placeholder has one state and one frame, so headless
multi-state and multi-frame fixtures must prove the runtime first. A production
animated entity or player-art follow-up is then required for the live M3 gate.

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
