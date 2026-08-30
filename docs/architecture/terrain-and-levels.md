# Terrain and level architecture

## Level model

A `Level` owns world dimensions, spawn, one tileset ID, ordered `WorldLayer`
values, and parallax zones. Each world layer owns a sparse tile grid and entity
map. Layer order is world depth; `Entity::sort_order` orders entities only inside
one layer.

Tile IDs are meaningful only against the level's single tileset. Scene,
placement preview, tile brush, terrain brush, loading, validation, and headless
review all resolve that same tileset. Missing IDs are errors, not empty cells.

## Terrain definitions

`src/terrain/` contains platform-neutral autotiling, masks, motif/field
generation, palette/style snapshots, atlas composition, and detection. It
depends on domain values rather than editor or renderer code.

A terrain distinguishes tiles it paints from tiles it counts as neighborhood
context. Painting resolves the affected cell and neighbors deterministically;
previewing does not mutate or grow managed atlases.

`tile_shape_geometry` is the single collision-polygon definition used by editor,
runtime, tests, and tools. Do not duplicate slope geometry in controllers or
renderers.

## Sparse access and interaction

Read-only tile access is sparse and allocation-free on runtime collision paths.
Editor drags deduplicate writes by cell and operation so holding a pointer over
one cell does not repeatedly rebuild terrain or append atlas content.

Viewport interactions use one platform-neutral controller for paint, erase,
entity placement/drag, selection, and deletion priority. Rendering/picking share
scene order.

## Parallax zones and themes

`ParallaxTheme` is a standalone resource; `Level` stores theme IDs only through
zones. A zone owns bounds and fade geometry, not theme definitions. Active-zone
resolution is platform-neutral and follows authored overlap priority and
half-open containment rules.

Theme editing and level editing are separate transactions. Assigning a theme ID
does not save a theme draft as a side effect.

## Level loading

`LevelManager` loads strict authored data without depending on Sprite, Collider,
Texture, or Theme managers. `LevelAssetLoader` later resolves the complete
referenced graph for runtime/headless tenants. This keeps serialization narrow
and makes dangling references explicit at graph-load or API preflight.

## Review

Focused level review samples an entity/content region at supported zooms.
Complete route review covers authored tracks, fades, layout, parallax/world
passes, and objective findings. Large bundles stream through atomic publication;
the reviewer measures coverage and spacing but does not declare aesthetic
quality.

## Invariants

- World dimensions and tile size form a valid whole-cell grid before a new level
  publishes.
- New zone bounds are valid, non-ambiguous, and reference an existing theme.
- Terrain and tile references belong to the level's tileset.
- Entity origins and Sprite offsets use the shared world geometry contract.
- Runtime movement changes runtime transforms, never authored level entities.
- Level saves are explicit and cannot commit dependent resource drafts.
