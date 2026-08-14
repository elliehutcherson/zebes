# Zebes Architecture

This document records the intended dependency boundaries and ownership rules in
Zebes. It describes the architecture we want new code to preserve, including
places where the current implementation is intentionally transitional.

## Dependency direction

The primary dependency direction is:

```text
editor/UI ───────► API and resource managers ───────► domain objects
    │                         │
    └────────────► platform adapters ◄───────────────┘
                              │
                              ▼
                     SDL, ImGui, and other libraries
```

The important rules are:

- Domain and engine logic use Zebes-owned types, not SDL or ImGui types.
- Resource managers depend on platform-neutral interfaces.
- Platform implementations may depend on external libraries.
- ImGui belongs in editor/UI code. It should not determine engine data models.
- The application composition root, currently `EditorEngine`, connects concrete
  platform implementations to platform-neutral interfaces.

## Texture metadata and runtime resources

Texture metadata and renderer resources have different responsibilities:

```text
Texture metadata
  id, name, path
         │
         ▼
TextureManager
  persistence and metadata cache
         │ Load(path) / Unload(handle)
         ▼
TextureResourceStore
  platform-neutral ownership contract
         │ implemented by
         ▼
SdlTextureStore
  owns SDL_Texture instances
```

`TextureManager` reads and writes texture definitions. It asks a
`TextureResourceStore` to load and unload runtime resources, but it does not
create, cast, query, or destroy `SDL_Texture` objects itself.

The serialized metadata contains only `id`, `name`, and `path`, and so do the
in-memory `Texture` and `Sprite` structures. Neither carries a `TextureHandle`:
handles live in the texture store and are fetched by ID through
`Api::GetTextureHandle` at the point of use. Callers that need a definition and
its handle together pair them explicitly — `ResolvedSprite` for entity
rendering, `AtlasBinding` for palettes — rather than caching a handle on the
definition, which previously meant a pure data struct had to include
`engine/texture_handle.h`.

`SdlTextureStore` owns every managed `SDL_Texture`. It allocates stable numeric
resource IDs, stores the mapping from ID to native pointer, and destroys the
native resource on unload or store destruction.

The composition root must outlive objects that use the store. In
`EditorEngine`, the required lifetime order is:

```text
SdlWrapper
  └── SdlTextureStore
        └── TextureManager
```

Destruction happens in the opposite order: manager, store, then SDL wrapper.

## TextureHandle and the SDL adapter

`TextureHandle` is the engine-owned identifier passed through resource and
editor code. It contains:

- a numeric resource ID;
- an opaque pointer to the resource store that created it.

It does not contain an `SDL_Texture*`, and engine code cannot access its owner
directly. The owner is retained so the platform adapter can route the handle
back to the store that owns the resource.

The managed texture flow is:

```text
1. TextureManager asks TextureResourceStore::Load(path).
2. SdlTextureStore creates an SDL_Texture.
3. SdlTextureStore stores textures_[numeric_id] = SDL_Texture*.
4. SdlTextureStore returns TextureHandle{numeric_id, this store}.
5. Engine/resource code copies the opaque handle without inspecting SDL state.
6. SDL editor rendering calls SdlTextureHandleAdapter::ToNative(handle).
7. The adapter retrieves the recorded owner and asks that SdlTextureStore to
   resolve the numeric ID.
8. The resolved SDL_Texture* is used only at the SDL/ImGui rendering boundary.
```

After `Unload(handle)`, the store removes the ID-to-pointer mapping. Resolving
that handle then returns `nullptr`, rather than returning a destroyed native
pointer.

### Handle invariant

The current adapter relies on this invariant:

> A handle passed to `SdlTextureHandleAdapter` was created by a live
> `SdlTextureStore`.

The handle records its owner as `const void*` to keep backend types out of the
engine type. The SDL adapter converts that owner back to `SdlTextureStore*`.
C++ cannot validate this conversion at runtime. Passing a handle from a
different store implementation, or resolving it after its owner has been
destroyed, is invalid.

Consequently:

- do not manually construct texture handles;
- do not retain handles beyond the owning resource store's lifetime;
- do not pass fake or non-SDL-store handles to the SDL adapter;
- keep native conversion inside SDL/editor rendering code;
- use an appropriate platform adapter for any future renderer backend.

This owner-routing design is intentional for the current single-renderer
architecture. If Zebes gains multiple simultaneous renderer backends or handles
that routinely outlive stores, replace it with an explicitly injected resolver
or a backend-checked handle representation.

## Transient texture previews

An imported image preview is not yet a managed engine resource. `TextureEditor`
therefore owns its preview `SDL_Texture*` directly and destroys it when the
preview changes or the editor is destroyed. It must not create a `TextureHandle`
for this temporary pointer.

Once an import is accepted, normal texture creation loads the copied asset
through `TextureResourceStore`, and subsequent rendering uses the managed
handle flow above.

## Input boundary

Engine input logic consumes `InputSnapshot`, `Key`, and `InputSource`. SDL event
translation and ImGui event forwarding live in `SdlInputSource`. Camera and
other engine systems must not inspect `SDL_Event`, SDL scancodes, or ImGui IO.

This separation allows engine input behavior to be tested using ordinary fake
snapshots without initializing a window or ImGui context.

## Camera responsibilities

`Camera` is a platform-neutral view transform: a world-space center, a zoom,
and the current viewport dimensions used for coordinate conversion. It does
not own input behavior or decide whether it is an editor or gameplay camera.

The owning controller supplies that policy:

- `Canvas` owns editor navigation and deliberately permits zoom from 0.1 to
  10.0 so an author can inspect unusually large or small level features.
- `CameraController` owns gameplay input and defaults to a narrower 0.1 to 5.0
  range. Its options may supply a different validated `CameraZoomRange` for a
  specific game camera.
- `Level` is persistent authoring data and does not embed transient camera
  state. A runtime world or editor view owns its camera separately.

The logical game view is the project setting `EngineConfig::game_view`. It is
independent of the SDL window and ImGui canvas: authors can configure the game
camera's width and height without changing the physical display resolution.
Saving through `Api::SaveConfig` updates the EditorEngine-owned configuration,
so long-lived editor views observe the new dimensions without being recreated.
The level editor uses the logical size only to calculate a pure `CameraGuide`;
ImGui renders the resulting rectangle and crosshair.

`WindowConfig` likewise uses Zebes-owned coordinates and booleans. It contains
no SDL position sentinels, flag bits, or headers. `SdlWrapper` is responsible
for translating those values into `SDL_WindowFlags` and centered positions at
the platform boundary. Config loading recognizes the former numeric format
only as a backwards-compatible migration path.

## Editor models

Stateful editor screens separate authoring behavior from presentation:

```text
ImGui panel ─────► editor model ◄───── editor/controller ─────► API
   rendering       state and pure          persistence
                   calculations
```

Editor models own selections, editable asset copies, deterministic catalogs,
and calculations such as preview bounds or atlas-cell snapping. They must not
depend on ImGui, SDL, or `Api`. Panels render model state and report persistence
intents; the containing editor coordinates those intents with `Api`.

Two rules are uniform across every editor tab, so that authoring one asset kind
teaches you the others.

**A destructive action asks before it acts, against a remembered target.**
`ConfirmPrompt` (`src/editor/confirm_prompt.h`) replaces the button with a
question the moment it is pressed and returns true only once the user answers.
The target is remembered rather than a bare flag: a question belongs to the
thing it was raised against, and without that, moving the selection while a
Confirm is on screen would leave it primed to destroy whatever is selected now.
Asking in place rather than through a modal is deliberate — `GuiInterface` has
no `OpenPopup`/`BeginPopupModal`, and growing the interface, `Gui`, and
`MockGui` for one dialog buys nothing an inline question does not, while
costing the ability to drive the whole interaction from a panel test. The line
for what needs asking is whether the work can be trivially recreated: a tile is
an Add and a click away and is not confirmed; a 47-mask rule table is not.

**Closing an editor compares against a snapshot, not a dirty flag.** Each panel
model holds the asset as it stood when editing began or was last saved, and
`has_unsaved_changes()` compares. No mutating path can forget to mark itself —
which matters most for level painting, since tiles go straight into the chunk
map — and editing a value back to its original correctly reports clean. The
comparison only runs on the frame the user tries to leave, so even a whole-level
compare costs nothing per frame. It is why the pure aggregates in
`src/objects/` carry defaulted `operator==`.

Failures follow the same uniformity: every tab surfaces them in the UI through
a dismissible banner, with the message held on the panel model wherever one
exists so the failure logic stays testable without SDL or ImGui.

Rendering layout follows the same boundary when it can be expressed as pure
geometry. For example, the level viewport calculates a `ParallaxLayout` from a
Zebes `Camera`, layer settings, and texture dimensions. The ImGui/SDL view only
resolves the native texture and emits the tiles described by that layout. This
keeps first-frame, viewport, zoom, and repetition behavior headlessly testable.

Level viewport authoring rules live in the platform-neutral `ViewportModel`
module. Entity picking and construction, stable ID allocation, tile mutation,
and grid snapping do not depend on ImGui, SDL, or `Api`. `ViewportTab` resolves
resources, draws the results, and translates UI gestures into those operations.

Authoring modes are mutually exclusive and ordered terrain, tile, blueprint.
Each write is deduplicated by cell and operation for the duration of a drag, so
holding a button over one cell does not rewrite it every frame — which for
terrain would also re-resolve its eight neighbours every frame.

`ViewportTab` translates the canvas into a per-frame `ViewportInteractionInput`.
`ViewportInteractionController` owns mode priority, continuous paint/erase and
entity-drag state, and discrete placement, selection, and deletion results. The
controller may mutate level tiles and existing entity positions, but it depends
only on Zebes domain types; ImGui button state and `Api` resource lookup remain
in `ViewportTab`.

Viewport scene composition is separate from presentation. `ViewportScene`
builds platform-neutral entity and zone render items with validated world-space
bounds, selection state, and opaque `TextureHandle` values. `ViewportRenderer`
is the UI boundary that converts those handles to SDL textures and emits ImGui
draw commands. `ViewportTab` orchestrates the two. Picking and rendering share
one entity-bounds calculation so invisible and textured entities do not acquire
different interaction geometry.

Blueprint placement previews use the same entity render description and native
renderer path as persistent entities, with an explicit placement mode selecting
their translucent presentation. A blueprint without a sprite is a valid
placeholder; a referenced sprite with no frame or managed texture is invalid and
stops the render pass instead of silently changing appearance.

Level tiles follow the same boundary. `ViewportScene` culls offscreen chunks
before scanning their cells and emits a `TileRenderBatch` containing one opaque
atlas handle plus the visible world rectangles, pixel source rectangles, and
collision shapes. Placement previews use the same description. Atlas queries,
UV normalization, tinting, and collision-overlay drawing live exclusively in
`ViewportRenderer`. Tile mutation rejects negative coordinates so invalid
world positions cannot become out-of-bounds chunk-array indices.

Parallax-zone activation is also a pure editor/runtime rule: resolve one zone
from a world-space reference point, currently the camera center, and then render
that zone's theme. Viewport intersection and zoom must not change the active
environment. Zone outlines are editor gizmos and are rendered independently.
Zone selections use stable zone IDs; selecting or explicitly framing a zone may
move the editor camera without changing activation semantics.

For the active parallax theme, `ViewportTab` resolves authored texture IDs into
opaque handles once per frame and `ViewportScene` binds them to a
`ParallaxRenderBatch`. `ViewportRenderer` alone converts those handles, queries
native texture dimensions, calculates the already headlessly tested parallax
layout, and emits draw commands. Missing referenced themes, textures, or runtime
resources fail the render pass; an empty texture ID remains a valid incomplete
authoring layer and is omitted.

The level viewport may explicitly preview the active zone, the selected theme,
or the selected layer. These modes only choose the parallax batch shown behind
the level; they do not change zone activation, selection, or persistent level
data. A layer preview is represented by an optional layer index in the
platform-neutral batch request, while native rendering remains unchanged.

Managed texture thumbnails use `TexturePreviewRenderer` at the editor boundary.
Panels supply an opaque `TextureHandle`; the renderer resolves SDL state,
queries source dimensions, calculates an aspect-preserving layout, and emits
the ImGui image. Panels that compose their own draw-list geometry, such as the
tile and terrain palettes sampling individual atlas cells, request an
`AtlasBinding` instead: the renderer still performs every SDL query and hands
back only an ImGui texture ID plus native dimensions. Panels and domain models
never receive `SDL_Texture*` values.

## Terrain

`src/terrain/` holds the terrain autotiling rules, and depends only on Abseil
and `objects/`. It is shared by the offline atlas tools and the level editor so
generated artwork and painted levels cannot disagree about what a mask means.

`terrain_mask` owns the neighbour bit layout, the normalization that clears a
corner bit whose two flanking edges are absent, and the resulting table of 47
distinct masks in ascending order. Atlas index *i* holds the artwork for
`Blob47MaskTable()[i]`; that ordering is the contract binding the compositor,
the importer, and the brush, so it is pinned by a golden test. The same module
maps a mask and a quadrant to one of five appearances, which is what lets 20
authored sprites generate all 47 tiles.

`blob47_compose` composites a quadrant sheet into a finished atlas plus a
manifest. Quadrants are authored at half tile size; a quadrant cell left
transparent in a variant inherits from variant 0, so adding visual variety costs
only the cells that differ. Composition happens offline and bakes concrete
tiles, so rendering stays one draw per cell and level data keeps storing plain
tile IDs.

`terrain_generator` draws an atlas instead of compositing one. It renders each
tile inside a 3x3 block of its neighbours and measures depth into the solid with
a distance transform, so a flat top, a 45-degree hypotenuse and a concave notch
all get a correct surface band from identical code -- which is why slopes cost
almost nothing here. Band width is modulated by a field that is exactly periodic
(`terrain_field`), so two adjacent tiles sample the same phase and agree along
their shared border with no seam bookkeeping. It holds no SDL or ImGui
dependency by rule: it is an algorithm the editor drives, and its tests run
headless. `tile_shape_geometry` is the single definition of every `TileShape`
polygon, shared with the editor's collision overlay so drawn artwork and
declared geometry cannot drift apart.

Generator appearance separates artistic intent from raster policy.
`TerrainMaterial` owns the palette and surface treatment;
`TerrainSurfaceConfig` owns facing-aware edge coverage and wall treatment;
`TerrainInteriorConfig` owns the independently selectable interior passes;
`TerrainPixelProfile` says how that intent should be quantised for chunky 16px,
balanced 32px or detailed 64px art; and `ResolvedTerrainStyle` validates the
configuration and turns reference measurements into concrete pixels once
before rasterisation. Rendering passes consume only the resolved measurements.
This keeps a 16px material deliberately designed rather than a noisy downsample
of a 32px result, and gives future passes one place to make their scale policy
explicit.

Surface coverage is sampled from the distance-field normal rather than from a
tile-edge label. `TerrainSurfaceConfig` authors independent top, side and
underside depths; the renderer interpolates between them with a continuous
up-facing amount, so a slope naturally falls between a flat top and a vertical
wall. The same orientation field controls an optional wall layer after the
contact shadow. Surface, contact, wall and interior remain separate semantic
palette roles, which lets later rocky or rooted wall styles change artwork
without duplicating Blob47 geometry or reimplementing normal calculation.
Wall darkness is a bounded exponential blend from the substrate toward the
authored outline colour. It is deliberately not an unbounded ramp step: very
dark substrates otherwise reach RGB zero midway through the control and lose
both their hue and the material's warm outline choice.
The wrapping ruffle field mixes nearby whole frequencies rather than using the
same frequency in every direction. It remains exactly periodic, but a straight
top edge no longer reduces the two-dimensional field to one repeated sine and
an artificial comb of identical teeth.

Edge details are a semantic pass after surface classification, not another
distortion of the band field. `terrain_motifs` owns normalized fringe profiles
for short grass, dry grass, moss and snow, including reduced-complexity
`Chunky16` banks. The renderer projects each profile along the dominant local
tangent and extends it from the band's inner boundary along the distance-field
normal. Its clump grid is fitted to the full atlas period and keyed by the
global cell, so motif choice and occupancy remain stable across tile seams.
The pass may replace colour indices only where occupancy is already solid; it
cannot add alpha outside the collision silhouette.

The interior itself has three ordered concepts: a continuous base treatment
(flat, mottle, soil clods or cobbles), a repeating substrate pattern (pebbles,
flecks, crosses, diamonds or a weighted mixed-earth bank), and sparse semantic
details (flowers, roots, flakes or crystals). Each concept has its own
configuration, motif bank where applicable, cached periodic placements and
render pass. A new substrate family therefore does not expand the cellular base
algorithm or masquerade as a decorative object. The semantic pass treats
already-painted substrate motifs as occupied, preserving the lower layer
instead of overwriting it.

Substrate marks also own palette roles separate from semantic decorations.
Pattern contrast can therefore fade marks into the interior without flattening
roots or flowers. Every pattern family has a compact bank capped at three-pixel
motifs for `Chunky16`; the 32px/64px policy is free to use the larger five-pixel
crosses and diamonds rather than relying on image scaling.

Both motif layers carry a `TerrainAccentMode` and an integer size. The mode
decides whether a layer's auto-shaded pixels take the layer's own material ramp,
the material's accent pair flat, or a gradient swept between that pair across
each motif. It is configuration rather than a property of a motif bank, so any
family can reach the accent colours; the detail sets that are normally authored
in accent colours say so through `DefaultAccentModeFor` instead of the renderer
hardcoding a list. The gradient occupies a fixed-length palette ramp whose
endpoints are exactly the two authored colours, is mixed in HSV along the
shorter hue arc so intermediate steps stay as saturated as the endpoints, and is
normalised over each motif's opaque extent rather than its bounding box so a
shape with transparent corners still reaches both ends. Size magnifies a stamp
by reading source pixels at `sx / scale`, which keeps the banks pixel art at
every size and keeps a magnified motif's lighting and gradient derived from the
stamp alone, so every wrapped copy of it agrees across a tile seam. Placement
spacing scales with size; whether a magnified stamp still fits its tile depends
on the motif bank, so `TerrainRenderer::Create` rejects that case rather than
`ResolveTerrainStyle`, which cannot see the banks without a dependency cycle.

Those responsibilities are also separate build units. `terrain_style` owns
authoring configuration, preset construction, validation and resolution;
`terrain_motifs` owns typed, profile-specific pixel motifs; `terrain_field`
owns reusable wrapping numerical fields; and `terrain_generator` applies those
resolved inputs to Blob47 geometry. Motif pixels name semantic colour roles
rather than private palette-array offsets, so adding a motif cannot silently
couple its data to renderer implementation order.

Generated terrain authoring state is a separate, versioned `TerrainRecipe`
resource under `definitions/terrain_recipes`. A recipe records the complete
`TerrainGenConfig` plus the texture, tileset and terrain IDs it produced;
runtime `Terrain` objects remain limited to the rules needed by the brush.
Recipe parsing is strict within a schema version, so adding a generator field
requires an explicit persistence decision instead of silently substituting a
new default when old work is reopened.

Recipe schema v3 stores top, side and underside depths plus edge-detail family,
amount, length, clump size, lean and highlight. Its explicit v1 migration
converts the former surface depth and underside bias into the three samples of
the same linear facing curve and disables the new wall layer. The v2 migration
disables edge details, preserving its pixels. Saving either migrated format
writes v3 once; unknown future schemas and incomplete documents still fail
rather than falling back to current defaults.

Regeneration preserves all resource and tile IDs by replacing only the atlas
pixels. It validates the recipe's asset binding before writing and rejects tile
size or variant-period changes because those alter atlas topology; the editor's
Save As path creates new IDs for those changes. Recipe persistence is committed
atomically, texture replacement is decoded before its atomic file swap, and a
failed texture swap rolls the recipe back to its prior configuration.

Surface texture, interior structure and both motif placement layers sample
atlas-global, wrapping coordinates. Substrate motifs and semantic details are
chosen independently once for the whole variant period and then clipped by each
tile's semantic regions, so changing a cell's Blob47 mask does not reshuffle
either layer in otherwise unchanged material. Those passes may change colour
indices inside the collision silhouette but never its alpha; artwork and
`TileShape` geometry therefore keep the same boundary.

All discrete motif grids fit a whole cell count into that same variant period.
A requested five-pixel feature does not necessarily divide a 96-pixel repeat;
fitting the grid prevents the final phase from resetting midway through a
scallop or tuft. Cellular interiors, substrate placements and semantic-detail
placements are likewise built once when the renderer is created, then reused by
every mask in the atlas.

Terrain authoring is its own editor tab rather than a section of the tileset
editor. Tuning a terrain is a picture-first activity -- the controls only mean
anything beside a preview large enough to judge -- so it needs a viewport, and a
navigator column at a fifth of the window cannot give it one. The tab produces a
finished tileset, which is why it needs nothing to exist beforehand: a tileset
names exactly one texture, so a generated terrain and the tileset carrying it
are made together or not at all.

`terrain_detect` turns an atlas into a `Terrain`. Both sources converge on
`BuildTerrainCandidate`, which is the only place tile naming, rule ordering and
slope membership are decided; manifest import parses a described atlas back into
the same shape the generator produces in memory. Manifest import is the exact
path and involves no guessing. The layout scan is the fallback for atlases with
no manifest, and deliberately finds nothing in hand-authored tilesets of slopes
and one-off pieces rather than reporting a false positive.

Terrain resolution happens at paint time, not render time. `TerrainBrush` sits
beside `ViewportModel` as a second platform-neutral authoring-rules module: it
computes a cell's mask from its eight neighbours, picks a variant
deterministically from the cell's coordinates so repainting never reshuffles
artwork, writes a concrete tile ID, and re-resolves the neighbouring cells of
the same terrain. A terrain missing a rule for a computed mask fails the paint
rather than substituting wrong artwork. `TerrainIndex` is the reverse lookup
from tile ID to owning terrain, rebuilt each frame from Api-owned storage.

Because resolution is a paint-time rule, `ViewportScene` and `ViewportRenderer`
are unchanged by terrains, and the game runtime never learns they exist.

## One tileset per level

A level stores bare tile IDs. Those IDs mean something only against the tileset
the level is bound to, so a level resolves exactly one tileset and a palette
selection from any other is unusable — painting one would store artwork the
level cannot resolve, and the editor would show the palette's art while the
saved data named something else.

`ResolvePaletteBinding` is where that is decided, from the level alone: it
returns the tileset the level should carry, the selections that may be painted,
and the tileset being refused so the editor can explain the refusal instead of
silently doing nothing. A level that has never been bound adopts the palette's
tileset, since it has no IDs to reinterpret. Otherwise the binding changes only
through the level's own `Tileset` field, which discards placed tiles on
confirmation rather than reinterpreting them.

`ViewportTab` therefore resolves one tileset per frame — the level's — for the
scene, the placement preview, and the terrain ghost alike.

A terrain distinguishes the tiles it *paints* from the tiles it merely *counts*.
Rules hold paintable tiles; `member_tile_ids` holds hand-placed pieces such as
slope units. Both contribute to a neighbour mask, so painted ground reads a
slope as the same material and continues into it instead of capping off with an
edge. Only paintable cells are ever re-resolved, so refreshing around a slope
never overwrites its artwork. Painting directly onto a member cell is a
deliberate replacement and is allowed.

Slopes are not an autotiling problem and no mask scheme produces them. They are
modular units: one tile per `TileShape`, drawn at full size rather than composed
from quadrants, because the diagonal surface band is exactly the detail that
does not composite well. `compose_blob47` appends them to the same atlas and
manifest, and the importer registers them as terrain members automatically. A
long ramp is that unit repeated, which also keeps each tile's collision shape
equal to its artwork.

## Definition and runtime data

Serialized definitions in `src/objects/` hold no runtime state. This boundary is
deliberate and is the prerequisite for building the engine against stable data.
It is now complete: `src/objects/` includes only Abseil and its own headers, so
`grep -rn "engine/" src/objects/` returning nothing is the standing check.

- `Texture` and `Sprite` carry an identity and reference their artwork by path
  or ID. The GPU handle lives in the texture store and is fetched through
  `Api::GetTextureHandle`.
- `Entity` references its sprite and collider by ID. Rendering and picking
  resolve those once per frame into a `SpriteLookup` — a map to `ResolvedSprite`,
  which pairs the definition with its handle — and pass the result explicitly,
  so `LevelManager` needs no asset managers to load a level.
- `Body` holds authored physical properties — mass, drag, and whether the body
  is static. Velocity and acceleration are simulation state, live in `Motion`,
  and are never serialized: saving a level must not capture how fast something
  happened to be moving.
- `Entity` holds no animation playback state. A frame index and timer are
  simulation state; `editor/animator.h` owns playback today, and the engine
  should follow that pattern rather than reviving fields on the definition.

Levels written before these splits still load; the removed keys — `vx`, `vy`,
`ax`, `ay`, and `current_frame_index` — are ignored rather than restored.

### The formats have no optional fields

Every writer in `src/resources/` emits every field unconditionally, and every
reader requires it with `.at()`. A definition missing a field is corruption, not
permission to substitute today's default. This was settled once for
`Terrain::variant_period` and then extended to the whole format, because the
alternative is silent reinterpretation: a tile whose `shape` was absent used to
load as `kNone` — solid artwork that collides with nothing.

Two consequences are load-bearing:

- **An absent list and an empty one must not both be spellable.** Collections
  that used to be omitted when empty — a terrain's `member_tile_ids`, an
  entity's `sprite_id` and `collider_id`, a tileset's `terrains` — are written
  either way. Offering the reader two spellings of one state is exactly what
  forces it to guess.
- **Something has to notice.** `tests/assets/shipped_assets_test.cc` loads every
  shipped definition of every kind and checks the count against the files on
  disk, so a definition left behind by a format change fails there rather than
  in front of whoever opens the editor next. Strict parsing without that test is
  a trap rather than an invariant.

Data that would otherwise hold the invariant back is migrated rather than
tolerated. `scripts/migrate_definitions.py` holds one migration per field,
naming the value that reproduces what the old file did; migrations are
idempotent and match the managers' own JSON layout so a migrated file and one
the editor re-saves are byte-identical. The terrain recipe parser therefore
reads exactly one schema version: carrying a translation for a version no file
uses would mean the parser's shape was decided by data that is not there.

Bulk loads report what they could not read. Each `LoadAll*` reads every file, so
one bad definition cannot hide the others, then returns an error naming all the
failures at once. Returning OK and logging a warning made a definition the
editor could not parse simply vanish from the catalog, which is the failure
strict parsing exists to surface.

Named asset catalogs use the shared `AssetCatalogKey`, ordered by display name
and then stable asset ID. This preserves duplicate names while providing
deterministic UI iteration without sorting every frame. Selection is stored by
stable asset or tile ID rather than by a vector position that can change after
refreshing or editing.

## Testing boundaries

- Domain and manager tests should use fake platform-neutral interfaces.
- SDL adapter/store tests may mock `SdlWrapper` and verify native ownership.
- ImGui interaction tests belong to the UI test preset.
- Headless tests must not require a display, SDL window, or ImGui context.
- Boundary tests should verify invalid handles, missing resources, destruction,
  and dependency lifetime assumptions.

## Adding another backend

A new renderer should add its implementation below `src/platform/` and
implement the platform-neutral resource contracts. Do not add the new
renderer's native types to `Texture`, `Sprite`, engine interfaces, or resource
manager public APIs.

The composition root chooses the concrete implementation. Backend-specific
conversion belongs in that backend's presentation or adapter layer.
