# Game runtime plan

**Status: Milestones 1 and 2 are complete. The Milestone 3 implementation is
complete, but its live visual gate remains open.
`run_game` boots the shipped Catacombs level, advances the fixed-tick player
through continuous sparse-tile collision, follows the player camera, and
presents runtime transforms through the shared scene and SDL host. The live M2
movement gate was accepted on 2026-08-29. The runtime and asset/content tracks
proceed in parallel. After the M3 live proof, the animation artwork pipeline is
a required follow-on before M4.**

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
runtime scene aggregate, SDL game presentation path, swept collision response,
and motion integration now exist. The runtime also has an immutable loaded-level
blueprint graph, fixed-tick animation cursors, explicit blueprint-state
selection, semantic Blueprint state keys, player idle/run/airborne selection,
and runtime sprite/frame presentation overrides. Catacombs uses a six-state
multi-frame proof asset. Runtime asset streaming and level transitions do not
exist yet; the M3 implementation still awaits its live visual acceptance gate.

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
them.** `AssetWorkspace::LoadLevelAssets` delegates transitive graph resolution
to `LevelAssetLoader`, which coordinates the authoritative managers rather than
making game code walk their mutable pointer-returning APIs. The result is one
frozen `LoadedLevelAssets` value: `LoadedLevelContent` contains copied level,
tileset, sprite, collider, and parallax definitions, while
`LevelRenderResources` contains their opaque texture handles. M1 owns that value
uniquely in `GameRuntime`; the M4 snapshot handoff will share it as
`shared_ptr<const>` without changing the definitions. The asset engine submits
file reads and decoding to the
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
hundreds. These maps are component storage, not yet a runtime entity lifecycle:
the current checkpoint materializes authored entities only. Before adding
projectiles or spawned props, introduce one runtime roster with monotonic IDs,
layer membership, and transactional spawn/despawn across every component and
spatial index. Game scene composition must then enumerate that roster rather
than treating the authored layer entity maps as the live population. An ECS is
a redesign to be argued from a profile, not a starting point.

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
  staged LoadedLevelAssets ───────► sim
  GPU upload requests ────────────► main
```

Shutdown follows the documented order: stop producers, stop and join each
runner, destroy each engine, which destroys its notification set. The process
composition root declares `SdlGameHost` before `GameRuntime`, so the injected
store, renderer, and input source outlive every engine that borrows them.

## Milestones

Each milestone ends runnable and gated; none depends on a later one.

**M1 — `run_game` walks a level — complete.** New platform-neutral `GameRuntime` beside
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
no ImGui dependency. The standalone process owns `SdlGameHost`, whose RAII
subsystem, renderer, input source, texture store, window, and native renderer
outlive the platform-neutral runtime borrows. A headless runtime integration
test boots the shipped level and renders a frame through fakes without SDL.

**M2 — A player — complete.** Input → intent → kinematic character controller:
AABB-versus-`TileShape` collision including slopes via
`tile_shape_geometry`, `Motion` integration in `RuntimeWorld`, camera follow.
The Mouse Player Placeholder with its exact 32×64 collider is the test body.
Gate: run and jump through Catacombs with correct slope traversal; headless
simulation tests for ground, wall, slope, and ceiling contacts, each failure
path included.

Implemented: `RuntimeWorld` preserves the authored `Level` beside entity-ID-keyed
runtime transforms, motion, and controller state; raw input snapshots derive
held movement and a fixed-tick-consumed jump edge; and boot validates the player,
its authored layer, collider, tileset identity, and every occupied tile ID. The
collision path now combines a platform-neutral dynamic-SAT narrow phase with a
sparse local tile query, deterministic simultaneous-contact response, explicit
one-way policy, and bounded allocation-free fixed-tick traversal. The running
game owns this world through `PlayerSimulation`, follows the committed player
transform with its camera, and composes runtime transform overrides without
mutating serialized entities.

The automated M2 gate is complete: collision and movement tests cover ground,
wall, ceiling, slopes, one-way passage, high-speed tunneling, bordering contacts,
internal seams, deterministic order, invalid geometry, and transactional
failure; runtime integration proves input changes the rendered player while the
authored level remains unchanged. The live Catacombs run/jump review was
accepted on 2026-08-29 after exercising standing, acceleration/deceleration,
jumping and landing, walls, ceilings, slope seams, and camera follow.

### Milestone 3 animation and blueprint-state implementation slice

The loaded-level graph now contains one frozen copy of each placed Blueprint,
validates its selected state, and resolves every non-empty Sprite and Collider
reference across every Blueprint state. `RuntimeWorld` borrows that graph
instead of maintaining a second long-lived copy. Resource-manager ownership
remains outside the runtime; no state transition performs I/O or retains
manager pointers.

`AnimationCursor` is an engine-owned, platform-neutral playback cursor. Each
runtime entity with authored sprite frames has an entity-ID-keyed cursor and
frame index in `RuntimeWorld`; `PlayerSimulation` advances those cursors once
per fixed simulation tick. Authored entities store a Blueprint-local state key,
not a state-vector index. Runtime boot resolves the player's six semantic keys
to checked state handles; fixed ticks select a handle without string lookup,
catalog lookup, or state-vector scanning. Applying a different handle changes
the selected sprite and resets playback only when the state changes.
`GameRuntime` passes the selected sprite and frame overrides into scene
composition, leaving serialized entities and the authored level intact.

Headless coverage exercises frame durations, looping, empty and changing frame
lists, multi-state selection, reset behavior, invalid transitions, runtime
presentation, shipped blueprint references, semantic-key migration, and the
six-state player policy. Catacombs now instantiates the Player Animation Proof,
which reuses existing multi-frame sprites for `idle-*`, `run-*`, and
`airborne-*` states while retaining one exact collider. The implementation and
automated M3 gates are complete; full acceptance still requires confirming the
visible transitions in the running level.

### Milestone 2 movement and collision implementation plan

The M2 movement path has two distinct responsibilities. The narrow phase asks
when one translating AABB first contacts one convex tile polygon. The broad
phase finds only the tile cells that the player can reach during the current
fixed tick. Keeping those boundaries separate lets later projectile and entity
indexes reuse the continuous geometry without coupling them to a tile-layer
query or the player controller.

#### Continuous collision algorithm

“Sweep” means continuous collision detection over one proposed displacement,
not a second kind of collider. For a starting box and displacement `d`, the
solver considers every position `start + t * d` for `t` in `[0, 1]` and returns
the first time of impact, its separating normal, and the stable identity of the
surface that was hit. Testing only the destination overlap is rejected because
a body can cross a thin wall or slope completely between fixed ticks.

Use dynamic separating axis theorem for the narrow phase. Project the AABB and
the convex polygon from `TileShapePolygon` onto the two box axes and every tile
edge normal. Relative motion gives an entry and exit time on each axis; the
latest entry is the time of impact when it does not exceed the earliest exit.
The axis that owns that latest entry supplies the contact normal. This retains
`tile_shape_geometry` as the one shape authority and handles full blocks, half
blocks, and every slope family without replacing them with special-case height
tables. Invalid, degenerate, or non-finite input is an error.

The existing static overlap primitive remains responsible for validating and
describing positive overlap at `t = 0`. Runtime boot must reject a player that
starts embedded in a blocking tile and name the entity and tile coordinate.
Small numerical overlap created by response within a tick may use a bounded
depenetration path; exhaustion is an explicit failure rather than permission to
continue with a partially resolved transform.

#### Sparse tile broad phase

Collision work must be proportional to the local swept region, never to the
number of tiles or chunks in the level. The player's authored `WorldLayer`
already stores sparse 32-by-32 `TileChunk` values keyed by `ChunkKey`. For each
movement iteration:

1. Form the conservative bounds of the start and destination AABBs, expanded
   only by the solver's contact tolerance.
2. Convert those bounds to a clamped tile-coordinate range.
3. Resolve only the intersecting chunk keys and only the covered cells inside
   each present chunk.
4. Skip tile ID zero immediately and resolve occupied IDs through an immutable
   tile-ID-to-collision-definition lookup built once during
   `RuntimeWorld::Create`.
5. Visit cells in row-major tile-coordinate order. Never derive collision order
   from `flat_hash_map` iteration.

The hot path performs no catalog lookup, level-wide scan, tile-definition scan,
or heap allocation. Character speed and terminal fall speed are bounded, so a
normal 60 Hz tick covers a small tile rectangle even in a level containing
100,000 occupied cells. A deterministic broad-phase test must prove that adding
distant occupied chunks does not change the queried coordinate set. Do not use
wall-clock timing as a correctness assertion.

Loop nesting is intentional only where each dimension is bounded or outside
the hot path. Runtime boot walks stored layers, allocated chunks, and their
fixed 32×32 cells once to validate occupied tile IDs; asset loading similarly
walks placed entities and Blueprint states once. A fixed tick loops at most
eight response passes, the small swept cell rectangle, and each candidate's
convex axes. Animation visits only entities with active cursors. Tile scene
composition derives the visible chunk-coordinate rectangle from the camera and
performs direct sparse-map lookups; it does not scan every stored chunk or tile
in the level. Entity rendering still visits the current live entity population,
so a spatial index becomes necessary if measured runtime-created entity counts
grow beyond the current hundreds-scale contract.

The editor's current `GetTileAt` helper is not an engine dependency. Move or
extract read-only sparse-cell access to the platform-neutral level boundary, or
give the movement library an equivalent collision-specific visitor. Do not
make `src/engine` or `src/game` depend on `src/editor`.

This rectangle query is the M2 character broad phase, not a universal fast-body
index. A future projectile that crosses many cells diagonally should use a
supercover/DDA traversal expanded by its collider extent, so work grows with
the crossed path rather than the area of its bounding rectangle. Static
collidable props can be indexed once in a uniform spatial grid; moving entities
can update a separate dynamic spatial hash. Only their candidate production is
different—the continuous narrow-phase query can use relative displacement and
remain shared. Non-collidable artwork never enters either index.

#### Multiple and one-way contacts

A movement operation tests every local candidate but responds to the earliest
time of impact. All contacts at that time within a documented tolerance form
one manifold. Stable tile coordinate and normal ordering breaks exact ties;
selecting whichever hash entry happens to arrive first is invalid. Shared or
covered tile faces must not act as exposed walls, and the test matrix must cross
block seams and every multi-tile slope seam in both directions.

Advance to the manifold, remove the components of remaining displacement and
velocity that point into its blocking normals, and sweep the unconsumed portion
of the tick again. This permits wall sliding and slope traversal while resolving
floor/wall and floor/slope corners together. Bound the response iterations with
a small named constant. Exceeding it is a diagnostic failure with the entity,
position, displacement, and contacts; silently accepting the last partial
position would make the result frame-rate- and ordering-dependent.

One-way tiles collide only when relative motion approaches their supporting
side and the starting AABB is outside that side within tolerance. Upward and
horizontal passage remains unblocked. Grounding requires a resolved supporting
normal with a negative Y component in the engine's screen-space coordinates;
an overlap discovered after the body has already crossed from the pass-through
side does not retroactively become ground. Although the current shipped
tilesets do not exercise one-way tiles, this behavior is a headless gate rather
than a live-review assumption.

#### Runtime ownership and render integration

`RuntimeWorld::Create` must derive and retain the player's authored layer ID,
validate the level's occupied tile IDs against the supplied `Tileset`, and build
the immutable collision lookup. Its fixed-tick player step then owns the order:
consume one input intent, calculate controller acceleration, apply gravity and
validated speed bounds, sweep and respond, update velocity and grounded state,
and commit the runtime transform only after the complete step succeeds. The
authored entity in `Level` remains unchanged.

Add a platform-neutral player simulation implementing `GameSimulation`. It
owns `RuntimeWorld`, reads the current `InputSnapshot` once per fixed tick, and
updates a gameplay camera that follows the committed player transform. M2
movement constants remain gameplay-owned validated defaults; they do not enter
`EngineConfig` before M5 has measurements that justify host tuning.

Scene composition receives runtime transform overrides through a
platform-neutral options or lookup boundary. It must not depend on
`RuntimeWorld`, copy and mutate the serialized `Level`, or expose GPU resources
to simulation. Entities without a runtime transform continue to render from
their authored transform; an override changes only the composed frame.

#### Implementation sequence and gates

1. **Pure sweep — implemented.** Add the dynamic-SAT time-of-impact result beside the static
   primitive, or split `tile_movement.{h,cc}` when keeping pairwise queries in
   `tile_collision` would mix narrow phase with layer traversal. Cover no-hit,
   touching, initial-overlap, full/half blocks, every slope polygon, parallel
   motion, high-speed crossing, non-finite input, and stable normals.
2. **Tile-layer movement — implemented.** Add the immutable collision lookup, direct sparse
   chunk query, one-way filter, simultaneous-contact manifold, and bounded
   remaining-motion response. Cover distant-chunk independence, unknown tile
   IDs, seams, corners, deterministic insertion order, tunneling, and
   non-convergence.
3. **`RuntimeWorld` controller — implemented.** Retain the player layer, integrate intent,
   acceleration, drag, gravity, speed bounds, jump impulse, grounded state, and
   transactional transform commit. Cover held input across catch-up ticks,
   single-consumption jump edges, landing, wall and ceiling response, slope
   ascent/descent, and every failure path.
4. **Running-game integration — implemented.** Replace `FreeFlySimulation` in `GameRuntime`
   with the player simulation, drive the follow camera, and compose entity
   bounds from runtime transforms. Extend the headless runtime test to prove
   that input moves the rendered player without changing the loaded authored
   entity.
5. **Verification — complete.** Run complete affected executables for collision,
   `RuntimeWorld`, player simulation, game scene, game runtime, and every shared
   scene-composition consumer; format and lint all edited translation units;
   finish with the live Catacombs run, jump, wall, ceiling, and slope review.

M2 does not add entity-versus-entity response, a general physics world, an ECS,
or projectile simulation. Its reusable result is the continuous convex sweep;
future object categories add spatial indexes only when their collision behavior
exists and can be measured.

**M3 — Animation playback and blueprint-state behavior — implementation
complete; live acceptance pending.** Frame timers, per-entity playback state,
one frozen loaded-level Blueprint graph, all state-referenced assets, semantic
state keys, boot-resolved player state handles, and runtime sprite/frame
selection are implemented and covered headlessly. Catacombs ships a six-state
multi-frame proof. The remaining gate is confirming its visible transitions in
the running level.

**Post-M3 — Animation artwork pipeline.** Run the animation-generation
feasibility gate, then build the deterministic frame-set processing, retained-source recipe,
transactional asset bundle, headless curation, and provider/editor workflow.
The first production player set must be processable, reviewable, regenerable
byte-stably, and visibly accepted in Catacombs before M4 begins. See
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md).

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

## Current handoff

The baseline M2 input vocabulary is resolved as A/D movement plus Space held
and pressed intent. Add new physical keys only when a gameplay intent requires
them; do not expose platform codes. The detailed next files, parallel asset
track, known platform debt, and last verification are recorded in
[`handoff.md`](handoff.md).
