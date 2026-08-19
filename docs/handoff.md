# Handoff

> Historical snapshot: this file is not the current feature backlog. Use
> [`roadmap.md`](roadmap.md) for sequencing and the linked feature design—such
> as [`prop-artwork.md`](prop-artwork.md)—for durable decisions and TODOs.

## Current: The generated-source editor flow is wired end to end

As of 2026-08-19, [`roadmap.md`](roadmap.md) remains the source of truth for
sequencing. Tracks 0-3 are complete. Track 4 layers, imported-source prop
artwork, attachment modes, and the developer feedback loop are implemented.

Milestone 5 is implemented, editor surface included. Image-generation
requests validate capabilities before reaching an adapter and return move-only,
cancellable polling handles with stable candidate provenance. Credentials cross
the boundary as move-only `SecretString` values, loaded per request so no secret
outlives its request. The HTTP seam enforces HTTPS, timeouts, bounded responses,
and prompt cancellation; its libcurl implementation uses the multi interface and
requires asynchronous DNS so `Poll` never performs a blocking name lookup.

`ImageGenerationEngine` is the first production owner of the engine-runner
infrastructure: created at editor startup, destroyed at shutdown, owning every
in-flight request. Bounding outstanding requests to eight is what makes event
delivery infallible — a slot is reserved at `Submit` and released at
`NextEvent`, so a request always has room for its one event. A rejected spec or
failed provider start is that request's event, never a `Run` failure, because a
failing `Run` would take every other in-flight request down with it.

`OpenAiImageClient` targets `gpt-image-2`: one synchronous POST returning base64
PNG candidates, so an operation wraps a single transport request. That model
rejects transparent backgrounds, so the adapter reports no transparency support
and isolation removes the background as it already does for imported sources.
Reverting to `gpt-image-1` for real alpha is an `OpenAiImageConfig` change.

Two gaps in `common` surfaced and were closed. `Engine` reported only
`kDidWork`/`kIdle`, which forced any engine with a source it can only poll to
claim `kDidWork` and spin a core; passes now carry an optional `wake_deadline`
and `NotificationSet::WaitUntil` sleeps until a notification or that time. And
`image_io` had no in-memory decode despite Milestone 1 recording one, so
`DecodeImage` was added — it reads dimensions from the header before decoding,
refusing an oversized image without allocating its surface.

Common now also contains the reusable long-lived polling-job infrastructure.
`BlockingCallbackThread` owns a non-templated `absl::AnyInvocable<void()>`
worker. An `Engine` owns a `NotificationSet` holding every wake source it
exposes, and performs bounded non-blocking `Run` passes; `EngineRunner` borrows
the set, adds its stop source, seals it, then arms every source, rechecks work
to close the hardware-interrupt race, and blocks until work or `Stop`. `Notify`
costs a syscall only while a thread is parked: the arm flag and the recheck pass
form a store-then-load handshake, so producers must publish before they notify.
The set uses `epoll`/`eventfd` on Linux, `kqueue`/`EVFILT_USER` on macOS, and
waitable events on Windows, and accepts borrowed native handles for NIC-style
interrupt sources. `MpscQueue` is fixed-capacity, preallocated, and
lock-free with explicit backpressure; `MpscNotifyQueue` signals only after a
successful publication. The macOS native path and the platform-neutral race
contracts have focused coverage; Linux and Windows need execution on their
native CI runners when those are available — including the `WaitUntil`
remainder-recompute on interrupted waits and the Windows `WAIT_TIMEOUT` arm,
which only macOS exercises today.

The error-handling cleanup made unchanged `Status` and `StatusOr` propagation
use the common macros consistently, while resource write/update paths now use
RAII compensation for temporary files, textures, and handles. Scoped
clang-tidy now caches successful analyses by translation unit, transitive
include contents, compile command, lint configuration, tool binary, and
platform arguments. The measured three-file check is 8.87 seconds cold and
0.31 seconds warm; failures are never cached and `--no-cache` forces analysis.

Where the generation code lives:

| Path | What |
|---|---|
| `src/common/engine.h` | `Engine`, `RunFeedback`, and `RunResult` with its `wake_deadline` |
| `src/common/engine_runner.{h,cc}` | Arm, recheck, then sleep — with or without a deadline |
| `src/common/notification_set.{h,cc}` | The native wait set; `Wait` and `WaitUntil` |
| `src/common/image_io.{h,cc}` | `WritePng`, `ReadPng`, and in-memory `DecodeImage` |
| `src/editor/image_generation/image_generation.{h,cc}` | Provider-neutral spec, capabilities, candidates, RAII request |
| `src/editor/image_generation/image_generation_engine.{h,cc}` | The session-lifetime owner of in-flight requests |
| `src/editor/image_generation/openai_image_client.{h,cc}` | The `gpt-image-2` adapter and its typed config |
| `src/editor/image_generation/http_transport.{h,cc}` | Bounded HTTPS seam and `SuggestedPollDelay` |
| `src/editor/image_generation/curl_http_transport.cc` | libcurl multi implementation; derives the poll delay |
| `src/editor/image_generation/credential_source.{h,cc}` | `SecretString` and the environment-backed source |
| `src/editor/image_generation/image_generation_service.{h,cc}` | Assembles and owns the whole stack; starts and stops the thread |

`ImageGenerationService` is the composition-root piece. It owns the transport,
credential source, adapter, engine, runner, and thread in the one declaration
order that keeps each alive for its borrower, and its destructor stops the
runner before joining. `EditorUi` owns one for the process and hands
`PropArtworkEditor` only the engine reference, declared before the editor so
the editor abandons its in-flight request while the engine still runs. That
ordering also settles the startup hazard: `Stop` can only be reached from the
service destructor, long after `Run` has entered, and a stop that landed first
would leave the runner stopped and `Run` returning `FailedPrecondition`.

The editor flow converges with the imported one. A finished generation becomes
a `PropGenerationReview` the model holds beside — not inside — recipe state,
and the candidate under review owns the preview until it is accepted or
discarded. Accepting retains the pixels through `Api::CreateSourceArtwork` with
`GeneratedArtworkProvenance`, then points the model at them; a failure anywhere
removes the source it created. The `ImageGenerationResult` to
`GeneratedArtworkProvenance` mapping is field-for-field, so there was no schema
change and no migration. Cancelling does not retire the request: the engine
still owes exactly one event, and dropping the id early would let that event be
mistaken for a later request's.

### Pick up here next

The credential-gated opt-in integration test against the real endpoint. It is
the only thing that will exercise the curl negative-timeout branch, and the
only remaining check that the adapter's request shape matches what
`gpt-image-2` actually accepts — every test so far stops at a fake.

After that, Milestone 6 operational hardening: shutdown under a live request,
retries that are safe to retry, provider error UX, and crash leftovers in
staging.

### What remains

- **Track 4:** Milestone 6 operational hardening remains. Parallax zone seaming
  is still the smallest independent feature. See [`roadmap.md`](roadmap.md) and
  [`prop-artwork.md`](prop-artwork.md).
- **Polling adoption:** `ImageGenerationEngine` is the first production owner
  and the shape later pollers should follow. The game loop is the expected
  second: a fixed timestep is the same "idle, but due at T" the deadline was
  added for.
- **Deferred terrain tool:** atlas compaction remains unjustified until real
  atlas growth becomes uncomfortable. It must be explicit because compaction
  renumbers tile IDs that levels store.

---

## Earlier: derived terrain artwork

Merged to `main`. Three phases landed together, each with its own design
document:

| Phase | Document | State |
|---|---|---|
| Derived terrain artwork | [`terrain-derived-artwork.md`](terrain-derived-artwork.md) | Implemented; doc trued up against the code |
| Safe asset deletion | [`asset-deletion.md`](asset-deletion.md) | Implemented, checks and buttons both |
| Prop artwork from generated images | [`prop-artwork.md`](prop-artwork.md) | Milestones 0-4a implemented; provider work remains |

---

## What this phase was, and why it is not what it started as

It began as terrain Phase 4: slope-connectivity variants, roughly forty new
tiles plus a way to choose between them, because slope-meets-slope was believed
to be drawn as slope-meets-wall.

Measuring it first killed that plan. `scripts/render_slope_matrix.cc` rendered
every join twice -- as the baked atlas drew it, and against the neighbours
actually present -- and reported the difference per cell:

| Join | Wrong by |
|---|---|
| Two ramps meeting at a peak | **2 of 1024 pixels** |
| A ramp running uphill into ground | 0 |
| A ramp ending at open air | **172 of 1024** |
| Ground beside any slope | **50 to 115 of 1024** |

So the peak, the case the phase was scoped around, was already right. What was
wrong was a slope ending at air -- drawn as buried interior against open sky --
and the ground beside any slope, which is a *blob* tile and therefore
unreachable by any amount of slope artwork.

Both are the same defect one level down. `TerrainRenderer` is a pure function of
a cell's shape, its neighbours' shapes and the phase. The atlas was a cache of
that function keyed on the 47-mask, which cannot say "the neighbour is a wedge"
or "air is there", so `AutoContext` existed to guess what the key left out.
Adding join variants would have made the key slightly less lossy and needed
redoing the next time terrain got richer.

The phase became: **make the cache key lossless and fill it on demand.**

---

## What is implemented

### The key and the provider

`TerrainCellKey` (`objects/tileset.h`) is a cell's own shape, its eight
neighbours' shapes and the phase. Nothing is normalized or collapsed. It sits in
`objects/` because it is serialized and because adjacency -- the `Neighbor` bit
layout and `kNeighborOffsets` -- is data about tiles rather than an algorithm;
`terrain_mask.h` keeps only the blob-47 scheme built on top.

`TerrainTileProvider` (`editor/level_editor/terrain_brush.h`) is the seam a
scheme plugs into. `Blob47TileProvider` projects the key down to a mask and
looks it up, which is complete for artwork authored against a mask.
`DerivedTileProvider` renders on demand.

`ComputeTerrainCellKey` replaced `ComputeTerrainMask` as the brush's primitive;
the mask survives as that key projected down.

### Growing an atlas while a level is painted

`DerivedTileProvider` answers in three layers: a session memo on the key, a
content lookup so a picture already in the atlas is reused whatever asked for
it, and append -- the only path that grows anything.

`DerivedTerrainSession` connects that to the editor. It opens from the tileset's
recipe, uploads artwork the moment a painted cell references it, and writes the
atlas and tileset only when the level is saved.

### Choosing what to paint

The terrain palette has a shape picker. Painting writes one shape into one cell.
`PaintableShapesOf` decides what a terrain can offer: a derived terrain offers
every shape whether or not artwork exists yet, an authored one offers only what
its tiles hold.

### What was deleted

`AutoContext`, `ApplyPartner`, the `SlopePair` table, `RenderShapeTile`, the
pre-baked slope block in `GenerateBlob47Atlas`, `SceneContext`'s two-mode scene
rendering, and `TerrainIndex`'s paintable/member split.

---

## Design choices worth knowing

**Deduplication is by content, and exact.** Two keys that render identically
share a tile, discovered by comparing pixels rather than by a rule asserting
which keys collide -- such a rule would be a claim about the renderer needing
re-proof whenever the renderer changed. Comparison is byte-for-byte, so a peak
(two pixels from a wall) keeps its own tile. An approximate comparison would
need a threshold, and a threshold is a claim about how much difference the eye
forgives. Deduplication is a saving rather than something correctness rests on.

**A derived terrain has no rule table.** Resolving even a full block by mask is
the lossy step; leaving it for the common case would have left the
ground-beside-a-slope defect unfixed. Its generated tiles are still real artwork
and are listed as owned.

**A derived tile carries its key.** `Terrain::derived_tiles` pairs each tile with
the neighbourhood it depicts. Regeneration redraws every tile from its own key,
wherever it sits in the atlas. Without this, regeneration could only rewrite the
tiles generation produced and would leave everything a level asked for stale.

**This is a tagged union, not an optional field.** `TerrainScheme` says which
variant a terrain is: `kBlob47` has `rules` and `shape_tile_ids`, `kDerived` has
`derived_tiles`. A reader knows which set to demand before it reads them. The
rule is now written down in [`style-guide.md`](style-guide.md).

**Visible and durable are separate.** `ShowTexturePixels` uploads to the GPU
without touching disk, because a tile the GPU has not seen renders as a hole,
while nothing should reach disk until the level is saved. Abandoning an edit
leaves no artwork behind that no level references.

**Collision geometry is authored; artwork follows.** A refresh hands each cell
back the shape it already had, so it can change how a cell looks but never what
the player collides with. That is what made the paintable/member split
unnecessary.

**Painting writes one cell.** An earlier draft stamped a gentle ramp as one
two-cell unit, on the theory that a half without its partner was broken
collision. It is not -- a lone lower half is a ramp to a half-height ledge --
and the stamp would have made a real arrangement unreachable: lower half, flat
half blocks, upper half is one continuous surface, because every piece meets its
neighbour at half tile height.

**The atlas fragments, deliberately.** Painting is sequential, so a cell is
first drawn for a half-finished neighbourhood and redrawn as its neighbours
arrive; the earlier tile stays. Reclaiming those would renumber tiles that
levels already name, so compaction must be an explicit tool rather than
something that happens on its own. It does not exist yet.

---

## Bugs the work surfaced

Worth knowing because most were invisible to reading:

- **Every manager's save freed the object it was saving**, replacing the cached
  `unique_ptr` rather than assigning through it, so every pointer handed out by
  `Get*` dangled. Six of them. Found by pressing Save in a live editor and
  watching the terrain palette blank.
- **The terrain ghost created the artwork it previewed**, so hovering grew the
  atlas and then drew a tile the GPU had not been given yet. Found by hovering.

- **Every `LoadAll*`** used to swallow per-file failures and return OK, so a
  definition the editor could not parse vanished from the catalogue.
- **Two collider definitions shared one ID**, making Samus's collision box
  depend on directory iteration order.
- **The neighbour offset table was defined twice**, privately, in the generator
  and the brush. They agreed by coincidence.
- **`DerivedTileProvider` never told the terrain it owned the tiles it added**,
  so the brush read every new tile back as foreign material.
- **`TerrainIndex` was a snapshot** while a derived provider invents tiles
  mid-paint, so a refresh looked up a brand-new tile, missed, and treated the
  cell as empty.
- **`ASSERT_OK`/`EXPECT_OK` evaluated their argument twice** -- the second time
  only on failure, to build the message. `ASSERT_OK(CreateLevel(std::move(x)))`
  moved from an already-moved value at exactly the wrong moment.
- **`compose_blob47`, `image_io_test` and `image_io` each had their own copy of
  stb PNG coding.** The test decoded with a third copy in order to check the
  encoder, so it could pass while the production reader was broken.
- **`Api::Create` validated every manager except `BlueprintManager`.**
- **absl drops `LOG(INFO)` before stderr** unless the threshold is raised, so
  `compose_blob47` and `generate_blob47_mask` had never printed their progress
  lines. All three tools set the threshold now.
- **`TerrainEditorModel::TileCount` counted slope units generation no longer
  bakes**, so the Terrain Editor promised 67 tiles where Create writes 47. Its
  test asserted the stale number, which is why it passed. Found while truing up
  the manifest.

---

## What the walk found

**The editor walk is done, and it earned its keep.** Painting cell by cell, ramps
and ledges of every shape, save, reopen, both Delete buttons, the Autumn Forest
preset, the shape picker with no terrain selected, and the atlas growing
mid-stroke were all driven in a live window and all behave.

Two bugs came out of it that nothing headless had caught:

- **Hovering created artwork.** The ghost resolved through the same call a paint
  uses, so mouse movement appended tiles and grew the atlas. The grown atlas is
  not uploaded until the frame ends, so the ghost then drew itself against a
  texture still at the old size and failed the frame -- and the failure skipped
  the upload that would have fixed it, so it repeated every frame forever.
  Previewing is now its own question; see `PreviewForKey`.
- **Saving freed what the editor was holding.** Every manager's save assigned a
  fresh `unique_ptr` over its map entry, so every pointer `Get*` had handed out
  dangled. Saving a level with derived terrain hit it every time, because
  committing artwork saves the tileset. Six managers; each now has a test that
  the address survives a save.

Also done in this phase: the manifest true-up (all four items, plus the shape
range, which was replaced by naming the two shapes a unit cannot be rather than
by deleting validation), the design-doc true-up, the merge to `main`, and
`lucinda_cave` shipped as a derived terrain.

Deletion is finished and needs nothing further. Its buttons went in behind the
checks deliberately, so there was never a frame in which the editor could strand
a reference.

## What is left

In [`roadmap.md`](roadmap.md), which carries what this section used to: the
terrain carry-overs, the layers phase, and the limitations this phase accepted
on purpose. One of them is closed — the `kSlope45*` names now describe the tall
side rather than the taper end.

---

## Where things are

| Path | What |
|---|---|
| `src/objects/tileset.h` | `TileShape`, adjacency, `TerrainCellKey`, `DerivedTile`, `Terrain` |
| `src/terrain/terrain_generator.{h,cc}` | The rasteriser. `RenderShapeTileInContext` is the whole function |
| `src/terrain/terrain_content_index.{h,cc}` | Which tile of an atlas already holds a picture |
| `src/terrain/terrain_placement.{h,cc}` | The shapes a palette may offer, and what a terrain can paint |
| `src/editor/level_editor/terrain_brush.{h,cc}` | Key computation, `TerrainIndex`, the provider seam |
| `src/editor/level_editor/derived_tile_provider.{h,cc}` | Render on miss, dedup by content, append. `PreviewForKey` is the same answer without creating anything |
| `src/editor/level_editor/derived_terrain_session.{h,cc}` | Opening, showing, committing |
| `tests/editor/derived_artwork_test.cc` | The invariant: painted artwork equals artwork for the real neighbourhood |
| `src/resources/asset_references.{h,cc}` | What names an asset, and the refusal text when something does |
| `src/api/api.cc` | `DeleteGeneratedTerrain` -- pre-flight, then recipe, tileset, artwork in that order |
| `src/objects/entity.h` | `sort_order`, which is within-layer ordering rather than depth |
| `scripts/render_slope_matrix.cc` | Renders the join sheet for looking at |
| `scripts/migrate_definitions.py` | One migration per field; run it after any format change |

## What else landed on the way

Not part of the terrain phase, but merged with it:

- **`Entity::sort_order`** decides what a prop draws in front of. Named for
  ordering within a depth slice, not depth: the passes are fixed at parallax,
  then tiles, then entities, so no value puts a prop behind the terrain. Layers
  are the thing that would, and the argument for them is in `prop-artwork.md` §7.
- **`Level::parallax_layers` is gone.** Themes and zones replaced it; the format
  still demanded and validated it while no panel could author it.
- **`ParallaxZone::fade_length` is still unimplemented**, and deliberately kept.
  It is the zone seaming control: `ResolveActiveParallaxZone` returns one zone by
  a half-open bounds test, so transitions are hard cuts. Making it real means
  returning two zones plus a blend weight and giving the parallax draw a tint
  parameter it does not have.
