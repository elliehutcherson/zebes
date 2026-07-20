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

The serialized metadata contains only `id`, `name`, and `path`. The current
in-memory `Texture` and `Sprite` structures also carry a transient
`TextureHandle` for convenient editor rendering; that field is never serialized.
This is a transitional runtime projection, not persistent asset data. If runtime
and authoring models are separated later, move the handle into the runtime view
without changing the stored JSON format.

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

Rendering layout follows the same boundary when it can be expressed as pure
geometry. For example, the level viewport calculates a `ParallaxLayout` from a
Zebes `Camera`, layer settings, and texture dimensions. The ImGui/SDL view only
resolves the native texture and emits the tiles described by that layout. This
keeps first-frame, viewport, zoom, and repetition behavior headlessly testable.

Level viewport authoring rules live in the platform-neutral `ViewportModel`
module. Entity picking and construction, stable ID allocation, tile mutation,
and grid snapping do not depend on ImGui, SDL, or `Api`. `ViewportTab` resolves
resources, draws the results, and translates UI gestures into those operations.

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
the ImGui image. Panels and domain models never receive `SDL_Texture*` values.

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
