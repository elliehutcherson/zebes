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
- Application composition roots (`EditorEngine` and standalone
  `main`/`SdlGameHost`) connect concrete platform implementations to
  platform-neutral interfaces. `GameRuntime` consumes those interfaces and is
  itself platform-neutral.

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
handles live in the texture store and are fetched by ID at the point of use.
Authoring callers normally use `Api::GetTextureHandle`; runtime level loading
uses `LevelAssetLoader` through `AssetWorkspace`, so the game does not walk
manager pointer/null conventions. Callers that need a definition and its handle
together pair them explicitly — `ResolvedSprite` for entity rendering,
`AtlasBinding` for palettes, and `LevelRenderResources` for a loaded runtime
graph — rather than caching a handle on the definition, which previously meant
a pure data struct had to include `engine/texture_handle.h`.

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

Engine input logic consumes `InputSnapshot`, `Key`, and `InputSource`.
`SdlInputSource` translates SDL state and may notify an injected native-event
observer; `EditorEngine` uses that observer to forward events to ImGui, while
`GameRuntime` supplies none. Camera and other engine systems must not inspect
`SDL_Event`, SDL scancodes, or ImGui IO.

This separation allows engine input behavior to be tested using ordinary fake
snapshots without initializing a window or ImGui context.

## Game runtime ownership

The standalone game's process composition root loads config and creates an
RAII `SdlGameHost`. The host owns SDL initialization, the window and renderer,
input source, and runtime texture store. It injects only `InputSource`,
`TextureResourceStore`, and `GameRenderer` into the platform-neutral
`GameRuntime`. Runtime boot opens a read-only `AssetWorkspace` profile and
resolves the shipped level's complete graph into a frozen
`LoadedLevelAssets` value. `LoadedLevelContent` contains only copied authored
definitions; `LevelRenderResources` contains the texture handles needed to
render them. All file access and GPU resource creation finishes before `Run`
begins.

The main-thread loop polls the injected input source, performs one bounded
non-blocking `GameEngine::Run`, composes a platform-neutral `GameSceneFrame`,
and hands it to the injected renderer. In the standalone executable those
adapters are SDL-backed. `SdlGameRenderer` alone resolves opaque texture handles
to `SDL_Texture`; ImGui is not linked into the game path. Destruction reverses
ownership: `GameRuntime` releases simulation, frozen handles, and its workspace
before `SdlGameHost` releases the renderer, input, texture store, window, and
finally the SDL subsystem.

`AssetWorkspace::LoadProfile::kRuntime` is explicitly read-only. It loads
runtime catalogs (textures, sprites, colliders, blueprints, levels, parallax
themes, and tilesets) while leaving generation and authoring recipe catalogs
empty. A missing definition or texture handle in the selected level fails boot
instead of producing a partially initialized running loop.

### Loaded-level graph boundary

Individual resource managers remain authoritative for loading and owning one
definition kind. `LevelAssetLoader` is a coordinator beside those managers: it
resolves the level's tileset, entity sprites and colliders, parallax themes, and
texture handles, validates lookup identity and handle availability, and copies
the complete result before returning. `AssetWorkspace` owns every participating
manager and exposes the operation as `LoadLevelAssets(level_id)`.

This is deliberately not an `Api` dependency inside `resources`: that would
reverse the existing dependency direction. It is also not game code: the game
chooses which level graph it wants, but resource loading and validation remain
inside the workspace/resource boundary. `LoadedLevelAssets` is atomic for
failure and lifetime purposes while its `content` and `rendering` members keep
simulation inputs separate from GPU bindings.

### Known transitional SDL placement

`SdlWrapper` predates the platform directory and still lives under
`src/common`; `EditorEngine` also retains its own manual SDL initialization and
shutdown. The standalone game no longer depends on either compromise:
`SdlGameHost` and `SdlSubsystem` own its native lifecycle under
`src/platform/sdl`. When editor platform composition is next changed, move the
wrapper under that platform directory and reuse the RAII subsystem. This is
structural debt, not a reason to broaden the M2 movement checkpoint.

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

Three rules are uniform across every editor tab, so that authoring one asset kind
teaches you the others.

**A resource editor never traps the author in its detail view.** Tabs that keep
their catalog visible need no extra navigation. Tabs that replace the catalog
with an asset workspace expose an explicit Back or Close action outside any
scrolling region; selector-based editors keep their existing-resource selector
visible. Leaving a dirty workspace follows the snapshot rule below rather than
silently discarding work.

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
The reusable scene types, parallax layout, entity ordering, visible-tile chunk
culling, and parallax resource binding live under `src/engine/`. Level Editor's
`ViewportScene` decorates those batches with selection, placement ghosts,
collision overlays, and zone gizmos; headless curation consumes the common
batches directly. Neither common consumer depends on ImGui, SDL, or an editor
target.

Headless curation uses the same boundary at the process level. `AssetWorkspace`
is the common composition root for every authored manager and `Api`; the editor
supplies `SdlTextureStore`, while `curate_assets` supplies
`HeadlessTextureStore`. Domain-specific `CurationReviewer` adapters share a
bounded nearest-neighbour RGBA compositor and atomic review-bundle publisher.
The parallax adapters reuse `ParallaxLayout`, camera-route, seam, repetition,
and coverage logic; prop, sprite, terrain, and tileset adapters resolve their
complete API-owned graphs. The level adapter resolves one persisted `Level` and
reuses the same environment resolver, parallax layout, tile batch, entity item,
camera transform, and world-layer order as Level Editor. Its PNG route frames,
contact sheets, isolated passes, and layout map contain no editor presentation
state and never parse the environment-build specification. Large review sets
flow through the generic streamed publication sink: each PNG is validated,
digested, and encoded inside the private atomic staging directory, its decoded
pixels are then released, and `manifest.json` is written before the one final
rename. Small reviewers retain the simpler in-memory `Review()` path.
Candidate evidence uses the same publication boundary. Prop creation and
regeneration share one platform-neutral preparation/validation module with
standalone Prop review and focused Level review; the latter substitutes the
prepared Sprite and pixels for one entity in a copied `Level`. It preserves
the entity's position, layer, and order, does not manufacture a texture handle,
and has no persistence authority. Only the Prop reviewer may cross the existing
compensated creation or regeneration transaction after review.
`generate_assets`
uses the same workspace and shared atomic publisher to produce strict creation
candidates. Their staged pixels are retained only inside the compensated
new-asset transaction after review. See
[`headless-curation.md`](headless-curation.md) for the command and extension
contract.

`AssetWorkspace` also owns the cross-process catalog snapshot boundary. A
reader holds a shared asset-root lock while all managers load and releases it
after `Api` validation; a writer acquires the exclusive form before loading and
holds it until every borrowing manager is destroyed. This lets generation and
review agents run concurrently while preventing a writer from committing an
in-memory catalog that became stale while it waited.

Complete catalog loading is the default and the only writable profile. Focused
level curation may explicitly request the read-only `referenced-level` profile:
Texture, Sprite, Level, Parallax Theme, and Tileset managers retain their normal
loaders and validation, while unrelated authoring managers are constructed but
left empty so `Api` keeps one interface without pretending the full catalog was
validated. The profile cannot commit and cannot review regeneration candidates,
which require live Source Artwork, Prop Recipe, and Blueprint state. This is a
composition-root policy, not a second parser or a relaxed manager invariant.

Complete level/theme authoring uses the versioned `EnvironmentBuildSpec` and
generic `build_environment` composition root. Specifications resolve unique
resource names to manager-owned IDs, assign only local IDs, and upsert themes
and levels without exposing GUID allocation to scripts. Placed entities name
their world layer, Blueprint, and Blueprint state while carrying a stable local
entity ID; the builder and Level Editor share the same domain entity factory so
their Sprite and Collider snapshots cannot diverge. Level serialization orders
sparse tile chunks by chunk ID, keeping repeated headless builds byte-stable.
The production Catacombs spec under `assets/authoring/environments/` is the
source of truth; there is no environment-specific C++ executable.

Level viewport authoring rules live in the platform-neutral `ViewportModel`
module. Entity picking, stable ID allocation, tile mutation, and grid snapping
do not depend on ImGui, SDL, or `Api`; Blueprint-to-Entity construction lives
one layer lower in `objects/entity_factory`. `ViewportTab` resolves resources,
draws the results, and translates UI gestures into those operations.

Authoring modes are mutually exclusive and ordered terrain, tile, blueprint.
Each write is deduplicated by cell and operation for the duration of a drag, so
holding a button over one cell does not rewrite it every frame — which for
terrain would also re-resolve its eight neighbours every frame.

`ViewportTab` translates the canvas into a per-frame `ViewportInteractionInput`.
`ViewportInteractionController` owns mode priority, continuous paint/erase and
entity-drag state, and discrete placement, selection, and deletion results. The
controller may mutate tiles and existing entity positions in one explicit
`WorldLayer`, but it depends only on Zebes domain types; ImGui button state and
`Api` resource lookup remain in `ViewportTab`.

`Level::layers` is the persistent world-depth model. It is ordered back to
front, and every `WorldLayer` owns one sparse tile grid and one entity map.
Tiles draw before entities inside a layer; `Entity::sort_order` orders only the
entities in that layer. Entity IDs remain unique across the whole level. The
active layer plus transient hidden/locked sets belong to `WorldLayerModel`, not
the serialized definition. Parallax layers remain specialized, reusable theme
content: the viewport composes the resolved parallax theme first, then visible
world layers, then editor-only overlays.

`ParallaxThemeManager` owns string-identified theme resources under
`definitions/parallax_themes/`. A `ParallaxZone` stores only a theme resource
ID, and `Level` owns no mutable theme definitions. Theme edits use an explicit
asset draft and Save, while Level edits only assign zone references. This keeps
shared mutation visible and prevents saving a level from publishing an
unrelated theme draft. The level loader refuses the retired embedded `themes`
field and directs authors to the deterministic migration instead of maintaining
two ownership paths.

Saved themes open in a read-only preview state. Layer/element selection and
camera navigation remain available there, but draft mutation and canvas drag
require an explicit Edit Theme transition. Texture catalogue clicks stage a
candidate ID and only Apply Texture changes the element. Discard Changes
restores the model's saved snapshot in place and returns to preview, so visual
review never requires risking a live draft mutation.

Each theme layer owns an ordered stable-ID `ParallaxElement` composition.
Elements carry texture ID, layer-local position, and scale; the layer owns its
camera-relative transform and an explicit repeat period, where zero leaves an
axis finite. Repetition copies the complete composition, never each element
independently. Element placement therefore remains reusable theme content—not
level or zone state—and terrain-aligned scenery remains a world-layer prop.

Theme Editor and Level Editor communicate through stable-ID navigation requests
routed by `EditorUi`, which owns both. Neither editor borrows the other's model
or draft. A duplicate-and-assign operation creates the new catalog resource
before changing the zone ID in the level draft; failure to create leaves the
draft unchanged. Viewport composition receives copied immutable theme snapshots
resolved for the current frame rather than retaining manager-owned pointers.

Level creation and parallax-zone creation are separate editor transactions. A
new level remains an unpublished `LevelPanelModel` draft until Create passes
intrinsic validation and persistence succeeds. A new zone remains a transient
`ParallaxZoneCreationModel` draft until its name, stable theme reference, and
in-world bounds validate; cancel and failed commit do not append to
`Level::zones`. One platform-neutral readiness result categorizes save,
placement, and zone-creation blockers. The toolbar, scene hierarchy, viewport,
inspector, and placement palette render those facts rather than defining their
own prerequisite policies.

The Level Editor presents one authored world as `Level Contents`; it does not
model or imply multiple scenes per `Level`. Level Settings, ordered World
Layers, and Parallax Zones are always-visible siblings in that hierarchy.
Selecting a world layer also makes it the active placement target. Inspector
surfaces share `InspectorPropertyGrid`, which owns the two-column ImGui table
contract: permanent human-readable labels and units are separate from hidden
widget IDs and full-width controls. This presentation helper owns no domain
validation; panels continue to mutate their explicit draft/model and render the
shared readiness result.

Viewport scene composition is separate from presentation. `ViewportScene`
builds platform-neutral entity and zone render items with validated world-space
bounds, selection state, and opaque `TextureHandle` values. `ViewportRenderer`
is the UI boundary that converts those handles to SDL textures and emits ImGui
draw commands. `ViewportTab` orchestrates the two. Picking and rendering share
one entity-bounds calculation so invisible and textured entities do not acquire
different interaction geometry.

Blueprint placement previews use the same entity render description and native
renderer path as persistent entities. Each blueprint state owns an explicit
placement mode: grid snapping maps the entity origin to the hovered tile's
bottom-center for grounded, top-center for ceiling, or center for free
placement. Sprite render bounds and collider bounds never feed back into that
origin calculation because both are already authored relative to the origin;
therefore adjusting a sprite render offset remains visible in the Level Editor.
The preview and committed entity share the resulting origin. A blueprint
without a sprite is a valid placeholder; a referenced sprite with no frame or
managed texture is invalid and stops the render pass instead of silently
changing appearance.

Existing entities are never moved implicitly when placement semantics change;
their saved coordinates may contain intentional composition. The entity
inspector instead offers an explicit resnap operation that maps the current
origin to the nearest valid placement anchor. This uses an anchor lattice, not
the pointer's containing cell, so resnapping is idempotent.

World-layer tiles follow the same boundary. `ViewportScene` culls offscreen chunks
before scanning their cells and emits a `TileRenderBatch` containing one opaque
atlas handle plus the visible world rectangles, pixel source rectangles, and
collision shapes. Placement previews use the same description. Atlas queries,
UV normalization, tinting, and collision-overlay drawing live exclusively in
`ViewportRenderer`. Tile mutation rejects negative coordinates so invalid
world positions cannot become out-of-bounds chunk-array indices. Chunk-key
encoding belongs to the level domain because it is serialized and validated;
terrain neighbourhood queries receive one layer explicitly and never connect
cells across depth slices.

Parallax-zone activation and fading are pure level-domain rules. From a
world-space reference point, currently the camera center,
`ResolveParallaxEnvironment` preserves the existing half-open active-zone ID
and returns either its theme or one stable left-to-right/top-to-bottom pair
with a normalized fade weight. Exact shared edges define fade seams; later
authored area overlaps retain priority, while intersecting fade bands and
bands passing through a third-zone overlap fail validation. Viewport
intersection and zoom do not change the result. Zone outlines are editor
gizmos rendered independently. Zone selections use stable IDs; selecting or
explicitly framing a zone may move the editor camera without changing
activation semantics.

For the resolved parallax environment, the viewport resolves one or two
immutable theme snapshots and each unique authored element texture ID once per
frame, then `ViewportScene` binds each theme to a `ParallaxRenderBatch` with
platform-neutral opacity. The stable primary batch draws first and an optional
secondary batch overlays it at the resolved weight. `ViewportRenderer` alone
converts handles and opacity to native texture/tint values, queries dimensions,
calculates the already headlessly tested composition layout, culls instances,
and emits repeat cells in authored order. A missing theme resource, texture
definition, runtime handle, invalid opacity or repeat period, or excessive
visible instance count fails the render pass.
Incomplete layer drafts live only in Theme Editor and are never published to
the resource catalog.

The Level viewport may preview the active or selected zone's resolved theme.
Theme Editor renders complete-theme, isolated-layer, or isolated-element
composition into the configured logical game view, aspect-fitted into the
physical ImGui region.
Resizing the editor therefore never changes simulated camera coverage. Its
optional level/zone context supplies world bounds and an authored route; the
preview intersects that route with camera centers actually reachable inside
the level at the selected zoom. Manual route endpoints are explicitly camera
centers. These preview choices never change zone activation, theme drafts, or
persistent level data, and both modes use the same platform-neutral batch
request and renderer as the Level viewport.

An incomplete Theme Editor element remains draft-only: preview input omits that
element while preserving every renderable element and layer, but the normal
theme validator still prevents saving. The canvas selects and moves elements
from the selected layer with no-jump left dragging. Canvas arrow/WASD and
middle-drag navigation project back into the normalized route controls, so
direct movement and fast Travel X/Y scrubbing are two views of one camera
state rather than competing navigation modes.

Managed texture thumbnails use `TexturePreviewRenderer` at the editor boundary.
Panels supply an opaque `TextureHandle`; the renderer resolves SDL state,
queries source dimensions, calculates an aspect-preserving layout, and emits
the ImGui image. Panels that compose their own draw-list geometry, such as the
tile and terrain palettes sampling individual atlas cells, request an
`AtlasBinding` instead: the renderer still performs every SDL query and hands
back only an ImGui texture ID plus native dimensions. Panels and domain models
never receive `SDL_Texture*` values.

The Level Editor blueprint palette is a searchable, deterministically sorted
thumbnail grid rather than a name strip. Its platform-neutral model owns the
filter and stable blueprint-ID selection; the panel resolves the selected ID
through the resource API instead of retaining a pointer across catalogue
refreshes. Each card previews the first state's first sprite frame through
`AtlasBinding`, matching the state used for placement. A blueprint with no
previewable artwork remains selectable through an explicit placeholder, so
incomplete authoring data cannot hide an asset from the catalogue. Blueprint,
tile, and terrain palettes share the same thumbnail-grid layout and item-frame
presentation helpers. Tile and terrain palettes also share one tileset selector
whose authoritative state is a stable resource ID; it resolves a transient
resource pointer each frame and clears dependent brush selection if that
tileset disappears or changes.

Entity origins and their attachment surfaces use one platform-neutral anchor
gizmo geometry calculation. A thin ImGui adapter renders that geometry in the
Blueprint, Level, Prop Artwork, and Sprite editors. The Blueprint Editor always
shows the origin; the Level Editor shows it for placement ghosts and selected
entities. Grounded and ceiling modes add a directional surface bracket, while
free or unresolved modes show only the origin cross. Gizmo geometry is constant
in screen space and does not participate in authored coordinates, picking, or
snapping.

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

Headless terrain authoring uses a separate versioned `TerrainBuildSpec` under
`assets/authoring/terrains/`. `terrain_builder` shares the persisted recipe's
single explicit `TerrainGenConfig` conversion, validates the complete document,
and resolves an existing bundle by unique recipe name. `build_terrain` creates
through the same prepared/committed transaction as Terrain Editor or regenerates
through its ID-preserving replacement boundary. It refuses duplicate names,
mismatched spec/material names, renamed ownership links, and topology changes
rather than creating a partially related bundle.

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

Full-atlas creation and regeneration run through `common/BackgroundTask`.
Workers receive copied, platform-neutral inputs and return typed `StatusOr`
results; the editor thread alone commits resource-manager, filesystem, and GPU
state. `BackgroundTask` is the standard-threading adapter and exception
boundary, so editor and engine code never own a future or use `try`/`catch`.
Its interface is deliberately submission/poll/result rather than terrain-
specific, allowing other bounded CPU work to use the same ownership model.
Regeneration carries the tileset snapshot it rendered, and the editor-thread
commit compares that snapshot with the live definition before replacing pixels.
This refuses a stale result if a level saved newly derived tiles while the
worker was running.

Long-lived polling jobs use the common engine-runner infrastructure instead.
An `Engine` performs one bounded, non-blocking `Run` pass and reports whether it
did work. `EngineRunner` repeats those passes and waits on a coalescing wakeup
only after the engine reports idle. `BlockingCallbackThread` owns the worker,
join, callback status, and standard-library exception boundary.
`EngineRunner::Stop` transitions the runner's single atomic lifecycle state and
wakes an active worker. Incoming work uses a fixed-capacity, lock-free
`MpscQueue`; `MpscNotifyQueue` couples successful queue publication to its
`Notification`, while a full queue reports backpressure without consuming the
rejected value. Its slots and its producer and consumer cursors are each
cache-line aligned, and each producer probes from a thread-local cursor rather
than a shared counter, so concurrent pushes contend only when they land on the
same slot. Alignment makes a slot cost a full cache line, which is the reason
`Capacity` is a deliberate bound rather than a generous one.

The engine owns everything and exposes it to the runner. During construction an
engine creates a `NotificationSet`, adds one source per wake reason, and builds
its queues and wait handles around the returned notifications;
`Engine::notification_set` exposes the set. `NotificationSet` owns one native
blocking facility — `epoll` plus `eventfd` on Linux, `kqueue` plus `EVFILT_USER`
on macOS, waitable events on Windows — and every `Notification` that feeds it. A
`Notification` binds its sink at construction and never rebinds, so `Notify` is
correct from any thread at any point in the set's lifetime. Software sources
share the set's coalescing wake primitive and cost no native handle; external
sources contribute a borrowed file descriptor or handle that the engine owns and
keeps open, plus interrupt arm/disarm callbacks. `EngineRunner::Create` adds its
own stop source and seals the set, which fixes the source list before a worker
can start and rejects a second runner for the same engine.

`Notify` wakes the native primitive only while a thread is parked on it. `Arm`
publishes an arm flag with a sequentially consistent fence and `Notify` reads it
behind the matching fence, so the two form a store-then-load handshake: a
producer that reads the set unarmed sends nothing, and the runner's recheck pass
is what delivers its work. That makes both halves of the idle path mandatory —
after an idle pass `EngineRunner` arms every source, runs a second `Run` pass,
and rechecks its stop state before blocking. The same recheck closes the race
where a NIC queue receives work immediately before its interrupt is armed, and
`EngineRunner::Stop` relies on it too, publishing the stopping state before it
reads the arm flag. The ordering requirement this places on producers is that
work must be published before `Notify` is called; `MpscNotifyQueue` does that by
construction. Sources do not need to share a notification, and producer
notification remains lock-free and, while the runner is busy, syscall-free.

An idle pass reports how long the runner may sleep. No deadline means sleep
until a notification fires, which is the zero-cost state a process-lifetime
engine spends nearly all its time in: one parked thread, no wakeups. A deadline
means sleep until a notification fires or that time passes, and it is how an
engine stays honest about a source it can only discover by polling — a remote
transfer with no registered descriptor, or a fixed timestep that is due whether
or not anything notifies. Without it such an engine has to claim `kDidWork` to
be rescheduled, which returns the runner to another pass with no sleep at all
and spins a core. `NotificationSet::WaitUntil` carries the deadline down to the
native facility's own timeout, recomputing the remainder across an interrupted
wait so a stream of signals cannot extend a bounded sleep. A timeout and a wake
both return `OkStatus`: the caller cannot act on the difference, because a wake
still has to poll its sources to learn which one fired.

A deadline is a bound on sleeping and not a wake source, so it never removes the
requirement that every notifiable source have a notification in the set. It is
the answer for the sources that cannot have one.

Game simulation timing is split across `SimulationPacer` and `GameEngine`.
`SimulationPacer` is platform-neutral and deterministic: it consumes monotonic
elapsed durations, returns a bounded batch of fixed-duration steps, retains
bounded whole-step debt, and reports discarded lag explicitly. Its interpolation
alpha is derived only from the fractional remainder, so overload cannot turn
interpolation into extrapolation. `GameEngine` samples the process monotonic
clock, translates the pacer's relative wake delay into `RunResult::wake_deadline`,
owns the `GameSimulation` it advances, and accumulates timing diagnostics. A
simulation step must be bounded and cannot wait or perform I/O; a failure ends
the engine pass after diagnostics record only the successfully completed steps.
Unpaced operation ignores elapsed time and returns the configured bounded batch
on every pass, allowing tests and offline work to run faster than real time
without changing simulation step duration.

Owners stop producers before destroying a queue, stop and join the runner
thread, then destroy the engine, which destroys its notification set and every
notification in it.
A stop request ends the runner without draining queued work; an owner that needs
draining must wait for its own completion acknowledgement before requesting it.
Stop latency is bounded by the stop notification rather than by any engine
deadline, so a long poll interval never makes shutdown slower.

Generated prop authoring follows the same thread and ownership boundary.
`SourceArtworkManager` owns editor-only retained PNG inputs under neutral,
ID-backed `source_art/<source-id>.png` paths and their strict definitions; it never creates
renderer resources. `PropRecipeManager` owns the versioned deterministic build
record, including a resolved terrain-style snapshot and stable output IDs.
`ParallaxArtworkRecipeManager` owns the corresponding one-source/one-texture
background build record. Its platform-neutral coordinator shares matte,
nearest-neighbour resize, palette, alpha, and repetition-review primitives but
does not enter the prop subject-isolation or anchoring workflow. Creation
publishes the Texture before the recipe and compensates on failure;
regeneration replaces only recipe-owned texture pixels after snapshot checks.
Parallax source redraw is a separate review-and-commit path: it accepts a
staged PNG plus generation provenance, then replaces the retained source,
recipe digest, and managed Texture in one compensated transaction while
preserving every ID and resource path. Direct writes to `source_art/` remain
invalid because they bypass the source digest and leave derived pixels stale.
Renaming updates the recipe and managed Texture display names through one API
operation with compensation, while recipe, Texture, source, and path identity
remain stable; an existing name mismatch fails before writing.
Generated texture paths are ID-backed and grouped by their owning authoring
domain as `textures/<category>/<texture-id>.png`; `TextureManager` enforces that
generic shape without hard-coding a single recipe kind's category.
These managers are singletons owned by `EditorEngine` and exposed through
`Api`, so editors cannot create competing caches over the same directories.
Source deletion scans every recipe kind first, and an attached terrain recipe
cannot be deleted until the artwork style is detached or its bundle is removed.
Runtime texture, sprite, and blueprint definitions remain ordinary engine
assets; authoring resources do not enter runtime rendering.

Artwork persistence shares filesystem and identity primitives, not schema
policy. Resource managers use `LoadJsonDefinitions` and
`WriteTextFileAtomically`; digest producers and validators use the shared image
digest module. Each versioned recipe still owns an explicit, strict parser and
serializer so unknown fields, invalid enums, and migration decisions fail at
the correct domain boundary. Do not introduce a generic recipe manager or
workflow graph merely to reduce similar-looking control flow: prop, terrain,
and background transactions have different outputs and rollback obligations.
`source_art/` is one flat authoring-input store; semantic roles live in recipes
and metadata rather than directory names.

Validation follows the same boundary. Public domain validators compose small
validators for cohesive concerns such as geometry, policy enums, matte
settings, and asset-graph identity. Each check uses an early return with a
specific error; unrelated invariants do not belong in one compound condition.
An ORM is not part of this architecture: authoring definitions are versioned
files in small in-memory catalogs, with no relational database, query planner,
or unit-of-work boundary for an ORM to own. If persistence later moves to a
database, evaluate a repository layer against those concrete requirements
rather than wrapping the current JSON resources in database-shaped machinery.

Post-generation image cleanup is a platform-neutral artwork operation rather
than editor UI or resource-manager logic. `generated_artwork_postprocessor`
accepts copied RGBA input plus a copied palette-reference image and returns
typed intermediate and final images with diagnostics. It removes an explicitly
declared solid backdrop, decontaminates partially covered edges against the
reference palette, reuses the premultiplied artwork resizer and Oklab
quantizer, and validates binary alpha and clear canvas borders. The command-line
adapter reads and writes PNGs; the Parallax Artwork editor calls the same
operation on a worker and commits its prepared result on the editor thread
rather than growing a second processing path. Palette references define
allowed output colors but do not become retained handles or extend a resource
store's lifetime. Solid-matte removal keeps tolerant exterior cleanup
border-connected, then also seeds enclosed components from pixels matching the
explicit matte color. That clears holes in arches without classifying every
isolated foreground color inside the broader fringe tolerance as background.

In-context prop placement is transient editor state. Its preview retains an
unmodified terrain layer and recomposites the finished prop while it is
dragged; grounded and ceiling movement resolves against terrain alpha, while
free movement follows both axes. The recipe and prepared artwork never receive
this scene position. A platform-neutral `PointerDragController` owns the
no-jump grab offset for both this interaction and Level Editor entity dragging;
the callers retain picking, constraints, object identity, and persistence.

Remote image generation uses generation-owned, platform-neutral request
contracts under `src/generation/`. Only the lifecycle panel remains under
`src/editor/image_generation/`; the provider contracts, service, engine,
transports, prompts, and clients have no editor dependency and are shared by
the interactive editor and `generate_assets`.
`ImageGenerationClient` validates requested capabilities before an adapter can
start work and returns an RAII request that is polled without blocking and
cancels unfinished work on destruction. Provider adapters receive credentials
through `CredentialSource` and move them into bounded `HttpTransport` requests;
raw secrets and external HTTP/provider types do not cross into editor models,
project configuration, deterministic artwork stages, or provenance. A credential
is loaded per request rather than held by the adapter, so no secret outlives the
request that used it and a missing key fails one request instead of startup.
Concrete transports must make cancellation prompt rather than joining remote
work on the editor thread, which is why remote operations do not use
`BackgroundTask`.

`ImageGenerationEngine` is the first production owner of the engine-runner
infrastructure, and the shape later long-lived pollers should follow. It is
created once at editor startup and destroyed at shutdown, owns every in-flight
request, and exposes only `Submit`, `Cancel`, and `NextEvent` — producers never
touch its queues. A targeted `NextEvent(request_id)` parks events for other
request IDs, so two editor surfaces sharing one engine cannot consume each
other's results. Submissions arrive over an `MpscNotifyQueue`; results leave
over a plain `MpscQueue` with no notification, because the editor drains it on
its own frame schedule and never sleeps on the engine. Bounding outstanding
requests is what makes event delivery infallible: `Submit` reserves a slot
before queueing and `NextEvent` releases it, so a request always has somewhere
to put its one event, and the queue cannot overflow. A rejected specification
or a failed provider start is reported as that request's event, never as a
`Run` failure, because a failing `Run` ends the runner and discards every other
request with it.

A remote transfer has no descriptor the runner can wait on, so the engine
reports an idle pass with a deadline derived from the soonest
`SuggestedPollDelay` across its requests, and no deadline at all once none are
in flight. That is bounded polling rather than socket-driven wakeup, chosen
because registering transport sockets dynamically would require mutating a
sealed notification set while a thread is armed.
`CurlHttpTransport` implements that contract with a poll-driven libcurl multi
handle per request, verified HTTPS without redirects, receive-time byte limits,
and immediate handle removal on cancellation. It requires libcurl's
asynchronous DNS feature so a first poll cannot block on name resolution.

`ImageGenerationService` is where one provider stack is assembled and the only
thing that starts or stops it. It owns the transport, the credential source,
the adapter, the engine, its `EngineRunner`, and the `BlockingCallbackThread`
the runner blocks on, in that order, so each outlives what borrows it;
destruction stops the runner before joining. A process composition root owns
the service: `EditorUi` independently composes Codex and OpenAI services, while
`generate_assets` composes the provider selected for one operation, including
the deterministic credential-free fake. The editor owns one shared registry of
provider-neutral names, availability, and engine references. Each generated-artwork surface
owns an `ImageGenerationRequestController` that borrows this registry and owns
that surface's provider selection, single in-flight request, request ID,
cancellation, candidate navigation, and retry-safe accept/discard state. Prop
and Parallax editors retain separate prompts and deterministic processing
models. The services and registry are declared before the editors, so each
controller abandons its in-flight request while the selected engine still
runs. Editors never see a transport or credential.

Reference-image redraw is the same generation boundary with an existing
retained source in `ImageGenerationSpec`. The OpenAI adapter serializes that
source as an in-memory PNG multipart image-edit request. Redraw candidates carry
optimistic source and derived-texture digests; curation refuses a candidate if
either current definition no longer matches its captured base.

Generation presentation is split at the same boundary. The shared lifecycle
panel renders provider selection, cancellation, candidate navigation,
provenance, acceptance, and discard controls from a provider-neutral UI
snapshot. Domain editors supply their own subject prompt, system instructions,
style draft, and processing settings. Default system prompts, reusable style
presets, and background role/repetition fragments are named constants in
`artwork_generation_prompts.h`; policy text is not hidden in widget code or
model member initializers.

Imported and generated images share one compensated retained-source transaction
beside the API boundary. `RetainSourceArtwork` writes through `Api`, reloads the
canonical definition, and invokes a domain acceptance callback; failure to
reload or accept deletes the new source. The editor's
`RetainGeneratedSourceArtwork` adapter only maps its review state into stable
generation provenance. Headless creation candidates call the same transaction,
with `Api::CreateGeneratedProp` or `Api::CreateGeneratedParallaxArtwork` as the
acceptance callback, so a failed bundle creation removes the otherwise orphaned
source. Provenance timestamps use the shared UTC formatter.

Provider construction and authentication are optional capabilities, not editor
invariants. A provider that cannot be composed is disabled immediately; a
missing credential, ChatGPT login, or enabled skill discovered on first use
also disables that provider with its reason retained for both UIs. Runtime
disablement retains the engine reference until shutdown so another surface can
still collect a request that was already in flight, while the nonempty reason
prevents new submissions. The offline import and processing path stays
available. A provider without an engine must have an unavailable reason, and
provider names must be unique.

Codex executable discovery happens while its provider is composed. It prefers
the explicit `ZEBES_CODEX_BIN` path, then `PATH`, then known macOS OpenAI
editor-extension locations; the child process itself remains lazy. Subject and
editable system prompts remain provider-neutral editor state. Codex maps the
editable prompt into its ephemeral thread's developer instructions. The Images
endpoint has no equivalent role, so the OpenAI adapter composes it ahead of the
subject request. Stable accepted-source provenance retains the user's subject
prompt.

Optional prop-art style presets populate a separate editable guidance draft.
The editor appends non-empty guidance under an `Art direction:` heading at
submission time. Keeping it separate prevents preset selection from mutating
the user-editable isolation and background requirements; direct edits mark the
style as Custom.

The subscription-backed Codex adapter preserves the same boundary without an
HTTP credential. It owns one lazily started `codex app-server` child, speaks
the default JSONL protocol over private non-blocking pipes, and refuses any
account that is not authenticated through ChatGPT. Operations share the child
through raw borrows of the session uniquely owned by `CodexImageClient`;
`ImageGenerationEngine` destroys its requests before that client. Cancellation
sends a turn interrupt and never waits for the process. A dedicated protocol
translator owns request correlation and is the only production layer that sees
JSON or catches JSON exceptions; it emits mutually exclusive success and
failure models rather than partially populated response bags. The adapter
consumes those events with exhaustive variant dispatch. Session readiness and
failure, and each operation's lifecycle phase, are also variants whose active
alternative owns the data required in that state. Cancellation removes the
operation immediately, so a retained operation is never simultaneously marked
cancelled. Any failed protocol write permanently fails the shared session;
Zebes never continues after it can no longer know which requests reached the
child. The process transport likewise has mutually exclusive created, running,
and stopped states. Only the running state owns the child identity and its
move-only pipe descriptors, so stopped and never-started transports cannot
retain live process resources.
The child receives no API key, approval requests are rejected, and every
ephemeral thread is confined to a private temporary directory. The App
Server's image tool returns files from the active Codex home's
`generated_images` cache even for those threads, so the adapter allowlists
exactly the private directory and that cache. It canonicalizes paths, rejects
symlinks and non-regular files, and applies byte and decoded-pixel limits.
Zebes removes only private-directory files it owns; Codex manages its cache.
Only decoded `RgbaImage` data crosses into editor state.

Generated and imported prop sources converge at one boundary. A finished
generation is held as a review — provider, model, submitted prompt, request id,
and the candidates — and is not recipe state; accepting a candidate retains it
through `Api::CreateSourceArtwork` with `GeneratedArtworkProvenance` and only
then points the model at it, exactly as an imported PNG does. A retention that
fails anywhere removes the source it created, so a refusal leaves no orphan.
Neither the model nor the panels hold a provider response or a native texture:
the candidate under review is shown through the same single preview sink every
pipeline stage uses.

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
- An entity transform places its sprite origin in world space. Each
  `SpriteFrame` then places its rendered top-left corner relative to that origin
  with `offset_x` and `offset_y`; therefore `rendered_position = entity_origin +
  frame_render_offset`. Atlas coordinates only select pixels and never change
  world geometry. The serialized offset names remain stable, while editor UI
  calls them render offsets to distinguish them from placement and collision.
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
refreshing or editing. Vector-returning resource catalogs and transient picker
lists use the same name-then-ID policy through `NamedAssetLess`, so duplicate
display names do not make their order depend on hash-map or input iteration.

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
