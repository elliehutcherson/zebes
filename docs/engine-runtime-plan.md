# Game runtime plan

M1–M3 are complete, including live movement and animation review. Runtime
expansion is parked behind character animation in the [roadmap](roadmap.md).

[Runtime architecture](architecture/runtime.md) owns existing composition,
frozen content, mutable runtime state, fixed-step behavior, snapshot boundaries,
and shutdown order. [Rendering architecture](architecture/rendering-and-platform.md)
owns shared scene composition and SDL/texture lifetime. Do not duplicate those
contracts or completed collision implementation steps here.

## M4 — Thread split

- Move the existing bounded `GameEngine::Run` onto `EngineRunner`. Keep one
  owner per subsystem; no job graph, ECS, or locks on per-frame asset access.
- Publish value input and immutable frame snapshots through latest-wins slots.
  Start with atomic `shared_ptr<const FrameSnapshot>` exchange; consider a
  triple buffer only if measured contention warrants it.
- Give `AssetEngine` ownership of transitions and streaming. Submit blocking
  file reads/decode to a bounded I/O executor and consume notified completions.
  Main-thread SDL work blocks only inside presentation.
- Marshal GPU creation/destruction to the main thread. Share frozen loaded
  content across transitions until the last frame releases it; native handles
  must remain within their resource store's lifetime.
- Use bounded queues with explicit backpressure for commands/completions that
  cannot be dropped. Keep snapshot freshness separate from reliable commands.
- Keep audio on its own device callback with an explicitly owned ring buffer
  when audio work is introduced.
- Stop producers, join runners, then destroy engines and notifications before
  their borrowed host resources.

Acceptance: a level transition without a main-thread frame hitch, clean shutdown
under TSan, and unchanged M1–M3 headless behavior. Measure queue saturation and
resource retirement, not just steady-state playback.

## M5 — Measured host tuning

Expose simulation rate, queue capacities, and thread options through validated
`EngineConfig` only after profiling a real workload. Current defaults remain
60 Hz, at most four fixed steps per pass, and 250 ms maximum accumulated lag.
Preserve dropped-lag/overrun reporting and fractional interpolation in [0, 1).

## Deferred scope

The current runtime materializes authored entities. Before adding projectiles
or spawned props, introduce a live roster with monotonic IDs and layer membership,
transactional spawn/despawn across registries and spatial indexes, and scene
composition that enumerates that roster. Profile before considering an ECS.

Networking, rollback, a scripting runtime, fiber scheduling, and a runtime debug
UI are outside this plan. New input keys follow gameplay intents through Zebes
types; platform key codes stay in the adapter.

## Verification

Use focused tests for pacing, overload, collision/movement, runtime-world state,
animation transitions, scene composition, and game-runtime integration. M4 must
also exercise backpressure, level transitions, and shutdown. Follow
[AGENTS.md](../AGENTS.md) for affected-target checks, lint, and final validation.
