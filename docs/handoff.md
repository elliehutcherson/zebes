# Active handoff

Updated 2026-08-30. [`roadmap.md`](roadmap.md) remains the source of truth for
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

### Assets/runtime: generated animation gate hard-failed; preserve the evidence

The bounded gate in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md) is implemented:
the platform-neutral processor, ten-frame locomotion preset, four-frame idle
preset, evidence CLI, deterministic manual controls, and disposable asset-root
stager all work. Both manual controls pass and rerun byte-stably. The five-request
image budget is exhausted. Generated idle attempt 2 fails visible fixed-origin
and subtle-motion review after both earlier attempts failed equal-square-cell
layout. Generated locomotion attempt 2 passes the structural processor but
hard-fails live review: the sequence is discontinuous, is not fluid, and does
not read as a person running. Do not spend more requests or repair individual
frames.

The copied locomotion root is
`build/animation-feasibility/live-proof-locomotion-02/assets`. `run_game` now
accepts an explicit `--asset_root` because changing the working directory was
shown to load the executable's asset symlink instead. The copied definitions
were validated and the generated run was reviewed through this root. Keep the
command as reproducible failure evidence, not as an acceptance step:

```bash
build/dev/bin/run_game \
  --asset_root="$PWD/build/animation-feasibility/live-proof-locomotion-02/assets"
```

The full generated-source gate is closed as a hard failure. Keep imported/manual
sheets as the production path and generation as research evidence only. Do not
add provider-specific animation behavior to that locked run. The processor and
staging code are useful experimental seams but must not be silently promoted as
the production animation contract.

A separately reviewed generation experiment now has its production
infrastructure and disposable runner implemented; see the
[pose-conditioned experiment plan](animation-pose-conditioned-experiment-plan.md).
It uses all 12 poses in the supplied sheet as one animation unit and produces
one image per pose, each conditioned by both the locked identity board and its
exact pose cell. Reusable ordered reference support belongs in the production
generation and headless boundaries; 12-frame orchestration remains disposable.
OpenAI and Codex transport ordered references; the headless resolver accepts
confined files or bounded managed artwork and retains exact provenance. The
fake-provider workflow proves the frame-0/frame-6 pilot, reviewed approval gate,
fresh 0..11 batch, atomic failure evidence, 12-frame assembly, and 576 x 44
packing. The exact 12-pose/identity input kit is locked under
`build/animation-feasibility/pose-conditioned-v1/`. Its deterministic
guide-only processing and four-tick live Catacombs gate passed. `pilot-01`
exposed that `codex-imagegen` was incorrectly used as a worker model instead of
the separately supplied skill; the adapter now validates paginated
`model/list`, defaults to `gpt-5.6-sol`, and retains structured turn errors.
`pilot-02-sol` exposed that Codex returns a provider-native 1254 x 1254 square
rather than the assumed 1024 x 1024. The batch path now preserves that native
canvas without resize and raises only its exact assembled-sheet bounds.
`pilot-03-sol-native` completed fresh frames 0 and 6 but failed human review:
frame 0 is visibly stouter, frame 6 is leaner, and their arm and belt
construction differ. The separated-view follow-up then supplied right-side,
front, and back identity crops before the pose. Its fresh pair also failed:
helmet and waist sizes differ, and the poses read as nearly the same phase with
one arm moved. No batch is authorized. The disposable multi-identity runner
expansion was removed; production ordered references remain.

The next experiment is local first: a pure parametric proportion-guide tool
with fixed helmet, torso, waist, backpack, limb lengths/widths, joint positions,
origin, ground line, and z-order. It emits SVG and PNG from one resolved
primitive list and proves only the two opposing contact poses before any remote
request. If accepted locally, one right-side identity crop plus one exact guide
gets a two-call pilot. Sequential conditioning remains later work.

For the next production implementation pass, use imported/manual frame sheets
and promote only the source-neutral processing behavior that survives review:
shared registration, palette processing, packing, retained provenance, and
transactional bundle persistence. M3's separate left/right Sprite bindings
remain the runtime contract.

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
