# Rendering and platform architecture

## Scene composition

Engine scene code produces platform-neutral render values:

- parallax batches ordered far to near;
- world layers ordered back to front;
- tiles before entities inside a world layer;
- entity items with resolved Sprite frame geometry and opaque texture handles;
- camera/view transforms and opacity.

`ComposeGameSceneFrame` owns runtime orchestration. Editor `ViewportScene`
decorates the shared core with grids, selection, placement ghosts, collision
overlays, anchors, and zone outlines. Headless curation consumes the shared core
without editor presentation state.

Missing referenced definitions, Sprite frames, managed texture resources, or
handles fail the frame rather than drawing a misleading partial scene.

## Camera

`Camera` is a platform-neutral world-space center, zoom, and viewport. It owns
world/screen conversion and clamping inputs, not input policy or renderer state.
`CameraController` translates input/intent into camera movement; runtime follow
uses simulation state. Editor framing commands explicitly choose the authored
world, zone, or selection.

## Sprite geometry

An entity transform places its origin in world space. `SpriteFrame::offset_x`
and `offset_y` place the rendered top-left relative to that origin:

```text
rendered_position = entity_origin + frame_render_offset
```

Atlas coordinates select pixels only. Render size and offsets determine world
geometry. Picking uses the same ordering and bounds as drawing.

## SDL boundary

`SdlGameHost` owns SDL subsystem lifetime, window, renderer, native texture
store, input source, and presentation. `SdlGameRenderer` alone resolves
`TextureHandle` to `SDL_Texture`. ImGui is not linked into the standalone game.

The editor currently has transitional SDL ownership: `SdlWrapper` remains under
`src/common` and editor initialization predates `SdlSubsystem`. Move it under
`src/platform/sdl` only when that composition is next touched; do not create a
third SDL lifecycle.

## Input

Engine input consumes `InputSnapshot`, `Key`, and `InputSource`.
`SdlInputSource` converts SDL events/state and may notify an injected native
observer so the editor can forward events to ImGui. Game code never consumes
`SDL_Event` directly.

## Texture lifetime

Native texture creation, upload, lookup, and destruction happen on the owning
main/native thread. Opaque handles never outlive their store. Transient unsaved
preview textures are owned and destroyed by their editor preview; managed
textures go through resource stores.

## Parallax

Parallax themes contain ordered layers and repeated artwork elements. Zones own
bounds, theme IDs, and fade lengths. Platform-neutral resolution determines the
active primary/secondary theme and weight. Composition resolves each unique
texture once per frame, preserves far-to-near order, and emits copied immutable
batches. Renderer opacity applies to the complete batch.

## Headless rendering

Headless review uses `HeadlessTextureStore`, shared scene geometry, and software
raster paths. It publishes deterministic PNGs, passes, contact sheets, and
manifests atomically. It does not emulate editor overlays or make aesthetic
judgments.

## Another backend

A new renderer belongs under `src/platform/`, implements existing resource and
presentation contracts, and consumes unchanged scene values. Do not add backend
types to objects, terrain, resources, editor models, or runtime simulation.
