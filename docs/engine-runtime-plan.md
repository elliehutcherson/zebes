# Game runtime plan

**Status: Milestone 1 implemented. `run_game` boots the shipped Catacombs level,
drives a fixed-step free-fly simulation, and presents the shared scene through
SDL. Milestone 2 foundations now exist; player movement integration remains.
The runtime and asset/content tracks proceed in parallel.**

Design for the Zebes game runtime: the executable that loads a shipped level
and plays it. The editor, curation, and generation stacks are out of scope
except where the runtime reuses their boundaries. This document decides the
thread topology, the ownership and snapshot contracts, and the milestone
order; it deliberately does not design gameplay systems beyond the first
player controller.

## Starting point

The runtime is not built on new infrastructure. The concurrency substrate
already exists in `src/common/` and has a production tenant:

- `Engine` is one bounded, non-blocking `Run` pass; `EngineRunner` repeats
  passes on a long-lived thread and parks on a sealed `NotificationSet` only
  after the engine reports idle, with the arm/recheck handshake that closes
  the lost-wakeup race.
- `RunResult::wake_deadline` exists precisely for a fixed timestep that is due
  whether or not anything notifies.
- `MpscQueue` and `MpscNotifyQueue` are the lock-free handoff between threads;
  `BlockingCallbackThread` owns the worker, join, and exception boundary.
- `ImageGenerationEngine` and `ImageGenerationService` demonstrate the
  ownership shape: a service composes and owns the engine, its runner, and the
  thread, in declaration order, so each outlives what borrows it.

The data model is already runtime-ready. Definitions in `src/objects/` are
pure serializable structs; `Motion` (velocity, acceleration) is split from
authored `Body` and never serialized; `Entity` names its sprite and collider
by ID; `tile_shape_geometry` is the single definition of collision polygons;
and the editor's `ViewportScene`/`ViewportRenderer` split proves the pattern
of platform-neutral render batches consumed by a thin native boundary.

The platform-neutral `SimulationPacer`, fixed-step `GameEngine`, M1 simulation,
runtime scene aggregate, and SDL game presentation path now exist. What does
not exist: a player-controlled runtime world, collision response, animation
playback outside the editor, runtime asset streaming, or level transitions.

## Design decisions

Settled for this plan — raise a flag before deviating.

**D1 — The runtime is a small set of single-owner engines, not a job graph.**
Each long-lived subsystem is an `Engine` on its own `EngineRunner` thread,
owning its state exclusively and communicating only through queues and
immutable snapshots. No shared mutable state, therefore no locks on any
per-frame path. Thread count grows with the number of subsystems — main,
simulation, assets, audio — not with core count. A 2D platformer tick is
microseconds of work; the scaling problem is isolating everything that can
block (disk, network, GPU upload, audio) from the frame loop, not
parallelizing the simulation. Occasional bounded CPU work may continue to use
`common/BackgroundTask`. Potentially blocking runtime I/O is instead dispatched
to a bounded I/O executor; a subsystem engine only submits work and consumes
notified completions. Fibers do not make synchronous file calls non-blocking on
their underlying OS thread and are not introduced without a measured need for
a fiber-aware asynchronous I/O scheduler.

**D2 — The main thread owns SDL; no subsystem engine blocks on I/O.** SDL
requires the event pump and rendering on the main thread on macOS. The main
thread pumps events, publishes input, consumes the latest simulation snapshot,
renders, and presents; VSync present is the render-frame pacer. GPU state is
created and destroyed only here, which is the same rule the editor already
enforces for its commit path. During the running loop the main thread never
waits on work and blocks only inside presentation. `Engine::Run` remains one
bounded, non-blocking pass. `EngineRunner` may park an idle engine on its sealed
notification set, while blocking file calls are contained inside the bounded
I/O executor and report completion through a notifying queue.

**D3 — `GameEngine` is an `Engine` from day one, but simulation ticks are not
render frames.** The simulation uses a small independently tested pacer. In
real-time mode it accumulates monotonic elapsed time and returns zero or more
fixed-duration steps plus interpolation alpha; in unpaced mode it produces
bounded batches of fixed steps without waiting on wall time. Tick duration is
independent of render cadence. A maximum step count bounds each `Run` pass, and
a maximum lag bounds real-time catch-up so sustained overload is reported and
clamped instead of entering a spiral of death or silently taking one unstable
variable step. Whole-step debt left by a bounded pass is retained for the next
pass, but interpolation uses only the fractional remainder and therefore stays
in `[0, 1)`. The pacer returns a relative wake delay; `GameEngine` translates
that delay into `RunResult::wake_deadline`, keeping civil clock timestamps out
of simulation accumulation.

The M1 constants are a 60 Hz step, at most four steps per `Run`, and at most
250 ms of accumulated lag. They remain code constants until M5 has measurements
that justify host configuration. `GameTimingState` counts completed steps and
reports cumulative dropped lag and overrun count so overload cannot disappear
into logs or an unstable timestep.

Milestone 1 drives `GameEngine` inline from the main loop — one bounded `Run`
call per rendered frame, no second thread — because single-threaded debugging
is worth more than concurrency while the milestone is "make it exist."
Moving it onto its own `EngineRunner` in Milestone 4 is a composition-root
change, not a rewrite, because the pass shape and snapshot boundary are fixed
now. The simulation consumes only Zebes domain types, so real-time, overload,
and faster-than-real-time behavior remain headlessly testable.

**D4 — Threads exchange immutable snapshots through latest-wins slots.**
Input flows main → simulation as a value `InputSnapshot` in a single
atomic slot; the simulation reads the current state each tick and derives
edges by comparing consecutive snapshots. The simulation publishes an
immutable `FrameSnapshot` — camera, render batches, interpolation alpha —
through an atomic `std::shared_ptr<const FrameSnapshot>` swap. Render always
takes the latest and never waits; simulation never waits for render to
consume. A skipped snapshot is correct behavior, not loss: both sides want
freshest-state semantics, which is why these are slots and not queues.
Commands that must not be dropped (level transition requests, asset-load
completions) go over `MpscQueue`, whose backpressure is explicit. If
`shared_ptr` swap contention ever shows up in a profile, the fallback is a
triple buffer; do not start there.

**D5 — Assets are immutable by invariant after load; no lock ever guards
them.** Loading a level resolves the complete referenced asset graph —
tileset, atlas pixels, sprites paired with handles, parallax themes — into one
frozen `LoadedLevelAssets` value. M1 owns that value uniquely in `GameRuntime`;
the M4 snapshot handoff will share it as `shared_ptr<const>` without changing
the definitions. The asset engine submits file reads and decoding to the
bounded I/O executor and consumes notified completions; the main thread
performs the GPU upload, honoring D2. A level transition is a message and an
atomic swap; the old snapshot dies when the last `FrameSnapshot` referencing
it is retired, which the `shared_ptr` already expresses. Texture handles obey
the existing store contract: created by the runtime's `SdlTextureStore`, never
manufactured, never outliving it.

**D6 — Simulation state lives in flat registries keyed by entity ID, not an
ECS.** `RuntimeWorld` holds the loaded `Level` (untouched authored data)
beside per-entity runtime maps: `Motion`, animation playback, controller
state. This extends the split the objects layer already made — `Body` versus
`Motion`, definitions versus playback — and matches entity counts measured in
hundreds. An ECS is a redesign to be argued from a profile, not a starting
point.

**D7 — The runtime scene builder is factored from `ViewportScene`, not
duplicated and not borrowed whole.** The platform-neutral core — shared scene
geometry, chunk culling, tile and entity item construction, parallax resource
binding, and parallax layout — now lives under `src/engine/`. Headless curation
consumes that core directly. Editor-only presentation (selection overlays,
gizmos, placement ghosts, collision overlays, zone outlines) remains in
`ViewportScene` as a thin decoration layer. `ComposeGameSceneFrame` now owns
runtime world-layer orchestration around that shared core. The runtime renderer
is an SDL presentation layer under
`src/platform/sdl/`; ImGui does not enter the runtime.

**D8 — Host-specific tuning arrives last and lives in `EngineConfig`.**
Thread placement, pool sizes, and simulation rate become configuration only in
Milestone 5, once there is a measured workload. Before that, constants.

## Thread topology (Milestone 4 end state)

```text
main thread (SDL)                 sim thread (EngineRunner)
  pump events                       GameEngine::Run
  write InputSnapshot slot  ──────►   read input slot
  read FrameSnapshot slot   ◄──────   fixed ticks: intent, motion,
  render batches via                    collision, animation, camera
  SdlTextureStore, present            publish FrameSnapshot

asset thread (EngineRunner)        audio thread
  AssetEngine::Run                   device callback fed by a
  submit read/decode ──────►         lock-free ring, later work
    bounded I/O executor
  completion ◄──notify queue
  staged LevelAssets ──MpscQueue──► sim
  GPU upload requests ────────────► main
```

Shutdown follows the documented order: stop producers, stop and join each
runner, destroy each engine, which destroys its notification set. The process
composition root declares `SdlGameHost` before `GameRuntime`, so the injected
store, renderer, and input source outlive every engine that borrows them.

## Milestones

Each milestone ends runnable and gated; none depends on a later one.

**M1 — `run_game` walks a level.** New platform-neutral `GameRuntime` beside
`EditorEngine`: load `Catacombs Processional` through the existing managers
during an explicit boot phase, freeze `LoadedLevelAssets`, render parallax +
tiles + entities through the shared scene core (D7), fixed-timestep
`GameEngine` driven inline with real-time and unpaced pacing policies (D3),
and a free-fly camera on `CameraController`. The standalone process creates an
`SdlGameHost` and injects its renderer, input source, and texture store through
Zebes interfaces. Boot may load synchronously before the running loop exists;
runtime streaming and transitions must use the D2 I/O executor rather than
blocking an engine.
Gate: fly the camera through the shipped level in the game binary; headless
tests for real-time pacing, bounded overload, unpaced stepping, and the shared
scene core; the existing editor viewport tests still pass against the factored
core.

Implemented: the runtime uses the explicit read-only `AssetWorkspace` runtime
profile. Boot synchronously creates GPU textures and copies the complete
referenced render graph into `LoadedLevelAssets` before `Run`; the loop performs
no asset or config I/O. `SdlInputSource` accepts an optional native-event
observer, so the editor can forward events to ImGui while the game binary has
no ImGui dependency.

**M2 — A player.** Input → intent → kinematic character controller:
AABB-versus-`TileShape` collision including slopes via
`tile_shape_geometry`, `Motion` integration in `RuntimeWorld`, camera follow.
The Mouse Player Placeholder with its exact 32×64 collider is the test body.
Gate: run and jump through Catacombs with correct slope traversal; headless
simulation tests for ground, wall, slope, and ceiling contacts, each failure
path included.

First checkpoint implemented: `RuntimeWorld` preserves the authored `Level`
beside entity-ID-keyed runtime transforms, motion, and controller state; raw
input snapshots derive held movement and a fixed-tick-consumed jump edge; boot
copies referenced collider definitions; and a platform-neutral static
AABB-versus-`TileShape` SAT query covers block and slope contacts using
`tile_shape_geometry`. The remaining M2 solver must sweep through only the
player's world layer, enforce one-way policy, resolve contacts deterministically
without tunneling, integrate motion, and drive the follow camera. The static
overlap query is a primitive for that solver, not the solver itself.

**M3 — Animation playback and blueprint-state behavior.** Frame timers and
playback state in `RuntimeWorld`, following the `editor/animator.h` ownership
pattern; blueprint states select playback. Gate: animated entities in the
running level; headless playback tests.

**M4 — The thread split.** `GameEngine` moves onto its own `EngineRunner`
with the D4 slots carrying input and frames; `AssetEngine` owns level
transitions and streaming with GPU uploads marshaled to main; audio thread.
Gate: level transition without a frame hitch on main; clean shutdown under
TSan; the M1–M3 headless suites unchanged, since the engines did not change.

**M5 — Host tuning.** Simulation rate, queue capacities, and thread options
in `EngineConfig` with validation; measure before exposing anything.

## Non-goals

- An ECS, a job system, or a scripting runtime.
- Socket-driven wakeups for remote work — already settled in the roadmap;
  bounded polling stands.
- Networking, rollback, or save states beyond loading authored levels.
- A debug UI. If one arrives later it is an optional ImGui layer on the main
  thread, never a runtime dependency.

## Unresolved

- Input vocabulary growth (`Key` currently covers editor needs). Decided in
  M2 when the controller defines its intents.
