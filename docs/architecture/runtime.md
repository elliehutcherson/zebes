# Runtime architecture

Read with [`../engine-runtime-plan.md`](../engine-runtime-plan.md) for M4/M5
sequencing.

## Ownership

`GameRuntime` is the process composition root below the executable. It owns the
loaded content, runtime simulation, scene composition dependencies, and game
host in destruction-safe order. `SdlGameHost` owns SDL initialization, window,
renderer, texture store, input source, and presentation; SDL and GPU operations
stay on the main thread.

`LevelAssetLoader` coordinates managers to load one immutable
`LoadedLevelContent` graph: Level, Tileset, Sprites, Colliders, Blueprints,
Textures, and ParallaxThemes. Managers remain authoritative for each definition
kind. `LoadedLevelContent` is separate from native `LevelRenderResources`, so
simulation does not depend on renderer objects.

## Runtime world

`RuntimeWorld` borrows the frozen loaded graph and owns mutable instance state in
flat entity-ID registries:

- transforms;
- motion;
- controller state;
- grounded/contact state;
- resolved Blueprint state bindings;
- presentation selection;
- animation playback cursors.

Authored `Entity` definitions are never mutated. Runtime spawn/despawn must add
or remove every required registry entry as one checked operation; partial
entities are invalid. An ECS requires profile evidence, not anticipation.

## Player and animation

Boot resolves stable Blueprint-local keys for `idle-*`, `run-*`, and
`airborne-*` into checked handles. Fixed ticks select handles from grounded
state, horizontal velocity, and remembered facing without string or catalogue
lookup. State changes reset playback only when the semantic key changes.

Sprites explicitly choose loop or hold-last playback. The production player set
must preserve one exact 32×64 collider across all states. Generated animation is
deprecated; imported/manual sheets feed the production frame-set pipeline.

## Fixed-step execution

`GameEngine::Run` is bounded and non-blocking. `SimulationPacer` accumulates
monotonic elapsed time, emits fixed steps, clamps overload, retains whole-step
debt, and reports interpolation from only the fractional remainder. Simulation
rate is independent of render cadence.

M1–M3 run the engine inline for debugging. M4 moves it to `EngineRunner` without
changing simulation behavior.

## M4 thread boundary

```text
main / SDL                     simulation EngineRunner
  poll input       ─────────►    latest InputSnapshot
  render latest    ◄─────────    immutable FrameSnapshot

asset EngineRunner             bounded I/O executor
  submit work      ─────────►    file read / decode
  consume events   ◄─────────    notified completions
  upload requests  ─────────►    main-thread GPU queue
```

Input and frames use latest-wins slots because stale intermediate values are not
useful. Commands that cannot be dropped—level transitions, load completions,
upload requests—use bounded queues with explicit backpressure.

Assets are immutable after load. Level transitions publish a new frozen graph;
old graphs live until the final immutable frame snapshot releases them. No lock
guards per-frame asset access.

## Shutdown

Stop producers, stop and join each runner, then destroy engines and their
notification sets. Member declaration order must make every borrowed store,
queue, and notification outlive its borrower.

## Tests

Keep pacing, overload, movement, collision, runtime-world, animation, state
transition, and scene composition tests headless. Threading changes must leave
M1–M3 suites unchanged. M4 additionally requires transition hitch evidence and
clean TSan shutdown.
