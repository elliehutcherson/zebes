# Level world layers

**Status: implemented on 2026-08-16.**

This document designs ordered world layers for a level. It follows the Track
4 direction in [`roadmap.md`](../roadmap.md): the one level-wide tile grid and
entity map move into ordered depth slices, while `Entity::sort_order` remains
the ordering rule among entities in one slice.

The domain type is called a **world layer** to distinguish it from a parallax
layer. A parallax layer belongs to a theme and moves relative to the camera. A
world layer belongs directly to a level, uses level/world coordinates, and
participates in the level's ordinary rendering and authoring.

## 1. Why this is the next boundary

The level viewport currently draws three fixed passes:

```text
parallax -> the level-wide tile grid -> the level-wide entity map
```

`Entity::sort_order` can put one entity in front of another, but it cannot put
an entity behind terrain. This is the blocker identified by
[`prop-artwork.md`](../prop-artwork.md) §7: a canopy or foreground prop needs to be
in a depth slice in front of the player while other props can sit behind the
terrain.

Layers change the content portion of the pipeline to:

```text
parallax -> [layer 0 tiles -> layer 0 entities]
         -> [layer 1 tiles -> layer 1 entities]
         -> ...
         -> editor-only overlays
```

Layers are stored and rendered back to front. A later layer always draws in
front of an earlier layer. Inside one layer, tiles draw first and entities draw
second; entities with a greater `sort_order` draw later. Equal sort orders keep
ascending entity-ID order, preserving today's behavior.

This is deliberately a small compositing model rather than an arbitrary global
z-coordinate. It gives authors predictable ordering without sorting every tile
and entity in a level into one heterogeneous list.

## 2. Why world and parallax layers are not one stored type

The standard pattern is a common ordered compositor containing specialized
layer types, not one record with fields for every kind of content:

- [Tiled](https://doc.mapeditor.org/en/stable/manual/layers/) presents one
  ordered layer tree, but its tile, object, image, and group layers remain
  distinct types. Shared properties such as visibility, locking, and parallax
  factor live at the layer boundary; type-specific content does not.
- [LDtk](https://ldtk.io/docs/general/editor-components/layers/) likewise has
  ordered, typed IntGrid, tile, and entity layers.
- [Godot](https://docs.godotengine.org/en/stable/classes/class_canvasitem.html)
  gives 2D nodes common ordering through `CanvasItem`, while
  [`TileMapLayer`](https://docs.godotengine.org/en/stable/classes/class_tilemaplayer.html)
  and [`Parallax2D`](https://docs.godotengine.org/en/stable/classes/class_parallax2d.html)
  remain specialized nodes with different behavior.

Zebes has an additional ownership difference: a world layer is persistent
level geometry, while a parallax layer belongs to a reusable theme selected by
a camera-dependent zone. Moving a parallax layer directly into the level's
world-layer list would either duplicate themes per zone or make every list item
an indirect conditional reference. It would also create a wide tagged record
whose alternatives share little beyond a name and draw order:

| Concern | World layer | Parallax layer |
|---|---|---|
| Ownership | `Level` | `ParallaxTheme` |
| Selection | always present | selected through active zone/theme |
| Coordinates | world | camera-relative |
| Content | sparse tiles and entities | repeated texture |
| Gameplay | authored colliders and entities | visual only |
| Editing | paint, place, pick, drag | texture and scroll properties |

The right long-term unification point is therefore a platform-neutral
**scene composition**, built for the current frame. It orders typed render
passes produced by both systems:

```text
Level + active zone + camera
             |
             v
SceneComposition
  1. typed parallax passes from the resolved theme
  2. typed tile/entity passes from ordered world layers
  3. editor overlay passes
             |
             v
ViewportRenderer
```

That boundary can later support foreground parallax, weather, lighting, or
effects by introducing explicit composition bands without changing who owns
tile grids or theme textures. This phase does not add a generic persisted scene
graph: the current fixed background/world/overlay bands are sufficient, and a
format abstraction without a second real use would only move variant handling
into every editor operation.

The world layer itself intentionally groups one tile grid and one entity map.
It is the domain-specific equivalent of a small group containing a tile layer
and an object layer with a fixed internal order. That grouping is what lets an
author move terrain and the entities associated with its depth slice together,
while keeping the UI substantially smaller than a general-purpose map editor.

## 3. Goals and non-goals

### Goals

- Let a level own an ordered collection of world layers.
- Let every layer own an independent sparse tile grid and entity map.
- Keep layer identity stable when layers are reordered.
- Author new tiles, terrain, and entities into one explicit active layer.
- Render placement previews at the active layer's real depth.
- Preserve every shipped level's appearance through an explicit migration.
- Keep asset references, tileset switching, and derived terrain correct across
  all layers.
- Keep the model and tests platform-neutral; ImGui remains in the panel.

### Non-goals

- Multiple tilesets in one level. Tile IDs remain scoped by the level-wide
  `tileset_id`.
- Per-layer coordinate systems, offsets, parallax, opacity, or blend modes.
- Tile-by-tile depth sorting or interleaving tiles with entities inside a
  layer.
- Inferring collision from visual depth.
- Persisting the active layer or other editor-session state in a level
  definition.
- Changing the prop-artwork pipeline. Layers only provide the depth boundary
  it needs.

## 4. Domain model

Add a domain type with a stable ID, a display name, and the two collections
that `Level` owns directly today:

```cpp
struct WorldLayer {
  int id = -1;
  std::string name;
  absl::flat_hash_map<int64_t, TileChunk> tile_chunks;
  std::map<uint64_t, Entity> entities;

  bool operator==(const WorldLayer& other) const = default;
};

struct Level {
  // Existing level-wide metadata, tileset binding, dimensions, spawn point,
  // themes, and zones stay here.
  std::vector<WorldLayer> layers;
};
```

`WorldLayer` is intentionally distinct from the existing `ParallaxLayer`.
Editor names should make the distinction explicit too: rename the existing
selection enum value from `kLayer` to `kParallaxLayer`, and use `kWorldLayer`
for the new selection kind.

### Invariants

- A level has at least one world layer.
- Every layer has a non-negative ID and a non-empty name.
- Layer IDs are unique within a level and do not change on reorder.
- Layer names are not identity and need not be unique.
- Entity IDs remain unique across the entire level, not merely within a layer.
- Entity ID zero remains invalid.
- A layer cannot contain duplicate tile-chunk keys or duplicate entity IDs.
- Terrain neighbours are read only from the layer being painted. Tiles at the
  same coordinate in another layer never affect a terrain mask or derived key.
- The level's `tileset_id` backs every layer's tile grid.

Stable layer IDs prevent a reorder from retargeting selection or editor view
state. Keeping entity IDs level-wide preserves their existing meaning as
runtime-safe identities and lets asset-reference reports continue to identify
an entity unambiguously.

New layer IDs should be one past the greatest existing ID. Refuse overflow
rather than reusing an ID that stale editor state could still name.

## 5. Serialized form and migration

Replace the root `tile_chunks` and `entities` fields with one required `layers`
array. Every layer record requires all four fields, including empty
collections:

```json
{
    "id": "level-uuid",
    "name": "Forest",
    "tileset_id": "tileset-uuid",
    "layers": [
        {
            "id": 0,
            "name": "Base",
            "tile_chunks": [],
            "entities": []
        },
        {
            "id": 1,
            "name": "Foreground",
            "tile_chunks": [],
            "entities": []
        }
    ]
}
```

The rest of the level record is unchanged. Layer order in the JSON array is
draw order, back to front.

Extend `migrate_level` in `scripts/migrate_definitions.py` to wrap the old root
collections without changing their contents:

```text
tile_chunks + entities
        |
        v
layers: [{id: 0, name: "Base", tile_chunks: ..., entities: ...}]
```

The migration removes the two old root keys. It is idempotent: a document with
`layers` and neither old key is already current. A document containing both the
new array and either old collection is ambiguous and must be refused rather
than guessed at. Likewise, an old document missing either required collection
must fail instead of inventing it.

`LevelManager` should serialize and strictly parse the new shape with `.at()`.
Parsing should detect duplicate layer IDs, duplicate chunk keys, invalid or
duplicate entity IDs across layers, and mismatched entity map keys before any
entry can overwrite another silently. Saving should apply the same validation
through one shared `ValidateLevel` boundary so create, update, and load enforce
the same domain rules.

The existing shipped-assets test already loads every level definition. The
migration tests must additionally pin the wrapper's exact output, preservation
of old contents, idempotence, and refusal of half-migrated documents.

## 6. Domain and editor operations

Add platform-neutral helpers rather than spreading vector scans and ownership
changes through ImGui code:

- `FindWorldLayer(level, id)` for mutable and const lookup.
- `NextAvailableWorldLayerId(level)` with overflow reporting.
- `FindEntity(level, entity_id)` returning both its layer and entity.
- `NextAvailableEntityId(level)` scanning every layer.
- `MoveEntityToLayer(level, entity_id, destination_layer_id)` preserving the
  entity ID and failing before mutation if either side is invalid.
- `LevelHasTiles(level)` and `CountPlacedTiles(level)` aggregating every layer.
- Optional per-layer overloads for panel counts and delete confirmations.

Tile access should name its ownership explicitly:

```cpp
absl::Status SetTileAt(WorldLayer& layer, int tile_x, int tile_y, int tile_id);
absl::StatusOr<int> GetTileAt(const WorldLayer& layer, int tile_x, int tile_y);
```

Terrain operations need level geometry for bounds and one layer for
neighbourhood content. Their signatures should accept both rather than finding
an implicit active layer:

```cpp
ComputeTerrainCellKey(const Level& level, const WorldLayer& layer, ...);
PaintTerrain(const Level& level, WorldLayer& layer, ...);
EraseTerrain(const Level& level, WorldLayer& layer, ...);
```

This keeps active-layer policy in the editor and prevents a lower-level brush
from accidentally consulting or modifying another layer.

## 7. Layer editor model and panel

Add a platform-neutral `WorldLayerModel` and a small ImGui `WorldLayerPanel`.
The model owns transient authoring state keyed by stable layer ID:

- the active layer;
- temporarily hidden layers, if decision D2 selects transient visibility;
- temporarily locked layers, if locking is included;
- reconciliation when a level opens or its layers change.

It also owns tested operations for add, rename, reorder, delete, and activation.
The panel renders those operations and uses `ConfirmPrompt` for deletion. The
last layer cannot be deleted. Creating or opening a level selects its backmost
layer; adding a layer creates it immediately in front of the active layer and
makes it active.

The navigator should contain a `World Layers` section separate from
`Parallax`. Each world layer expands to show its entities. Selecting an
entity records its entity ID and activates its owning layer. The entity
inspector gains a layer selector that transfers the entity through
`MoveEntityToLayer`.

Deleting a layer reports how many painted cells and entities it will remove and
remembers the layer ID in the confirmation prompt. After any delete or reorder,
selection is reconciled by ID rather than by vector index.

The layer panel does not resolve textures, sprites, colliders, or tilesets. It
works only with `Level` and editor state, matching the repository's editor-model
boundary.

## 8. Viewport rendering and interaction

The active layer must be passed explicitly in `ViewportRenderOptions`; the
viewport should fail fast when it is missing or names no layer. Hidden and
locked state, if supported, is passed separately as editor-only options.

Refactor `RenderScene` from one tile pass and one entity pass into a loop over
visible world layers. Reuse the current per-grid and per-entity composition
functions, but give them a `WorldLayer` rather than reaching through `Level`.

Placement preview belongs inside that loop:

1. Draw parallax, the grid, and level bounds.
2. For each visible layer from back to front:
   1. draw its tiles;
   2. draw its entities in `sort_order`/ID order;
   3. if it is active, draw its tile, terrain, or entity placement preview.
3. Draw selection outlines, zone gizmos, the camera guide, and other editor
   chrome above all scene artwork.

Drawing the preview after the entire scene would make a ghost for a background
layer appear in front of foreground artwork and misrepresent the saved result.
Conversely, editor selection chrome should remain visible even when the selected
content is behind another layer. This requires separating selected borders from
the entity-art draw item instead of relying on the current combined pass.

Entity sprite resolution can be shared across the frame by collecting the
distinct sprite IDs from every visible layer. Rendering still happens one layer
at a time. Picking must use the same ordering rule as drawing so overlapping
entities never select something visually underneath the chosen target.

All edit operations target the active layer only:

- tile placement and erasure;
- terrain painting, erasure, and neighbour refresh;
- blueprint placement;
- entity dragging and deletion, subject to decision D1.

`DerivedTerrainSession` remains level/tileset-wide because it owns atlas
artwork, not painted cells. Its brush queries receive the active layer, so the
derived neighbourhood is isolated correctly while identical artwork can still
deduplicate across layers in the shared tileset atlas.

## 9. Cross-cutting consumers

Several systems currently assume the collections live on `Level` and must be
changed deliberately:

- `asset_references.cc` walks every layer. Reference descriptions should say
  `layer '<name>', entity <id>` or aggregate painted tile counts across layers.
- Tileset-change confirmation counts tiles across all layers and clears every
  layer's tile chunks only after confirmation. Entities remain.
- Level dirty-state equality naturally includes layer order and contents
  through defaulted value equality.
- Entity selection, drag, deletion, inspector lookup, and placement use the
  level-wide entity helpers and active layer.
- Scene composition and viewport tests build explicit layers rather than
  relying on root collections.
- New levels are created with one valid default layer before authoring begins.

No API or resource-manager dependency is added to `Level` or `WorldLayer`.
Both remain pure definitions containing asset IDs.

## 10. Collision and runtime semantics

Depth controls rendering, not physics:

- a tile contributes the collision shape declared by its tile definition;
- an entity contributes its authored collider and behavior regardless of its
  visual layer;
- a visual-only prop uses no collider, and a visual-only tile uses a tile whose
  collision shape is `kNone`;
- hiding or locking a layer in the editor does not change the saved game.

This keeps collision attached to the content that defines it instead of adding
a coarse layer switch whose effect differs between tiles, static props, and
active entities. The current repository has no runtime system that consumes a
level's tile/entity collections, so this phase establishes the contract and
implements it at the persistence and editor boundaries without inventing a
second runtime representation.

## 11. Implementation plan

Each step should land with its focused tests passing. Do not migrate shipped
definitions until the strict reader and migration tests are ready in the same
change.

1. **Domain and validation.** Add `WorldLayer`, layer/entity lookup helpers,
   shared `ValidateLevel`, and focused object/resource tests. Keep the viewport
   compiling through temporary explicit access to layer 0 only within this
   in-progress branch; do not ship that compatibility path.
2. **Format and migration.** Change `LevelManager`, extend
   `migrate_definitions.py`, migrate both shipped level files, and test strict
   parsing plus every shipped definition.
3. **Layer-aware tile and terrain boundaries.** Move `SetTileAt`, `GetTileAt`,
   terrain neighbourhood queries, placement, erasure, and tile-count helpers to
   explicit `WorldLayer` parameters. Test that identical coordinates in two
   layers are independent and never connect derived terrain across depth.
4. **Layer-aware entities and references.** Make IDs level-wide, update
   placement/picking/drag/delete/inspector operations, add entity transfer, and
   walk all layers in asset-reference checks.
5. **Rendering.** Compose and render every layer back to front, preserve
   within-layer entity order, insert placement ghosts at the active depth, and
   put editor overlays above the completed scene. Test exact draw/composition
   order at the platform-neutral scene boundary.
6. **Editor model and panel.** Add active-layer state and tested
   add/rename/reorder/delete behavior, then wire the navigator and inspector.
   Apply the decisions in §12.
7. **Verification and documentation.** Run the complete affected test
   executables (`level_manager_test`, `shipped_assets_test`, migration tests,
   asset-reference tests, and the level-editor/viewport tests), use
   `scripts/test.sh --affected-target level_editor` to catch its consumers,
   lint the edited translation units, and run `git diff --check`. Update the
   roadmap and this document from draft to implemented only after the editor
   workflow is complete.

This is a broad format-and-editor change, but it should remain a sequence of
small boundaries rather than one rewrite. The comprehensive build is warranted
before handoff because `Level` is a broadly consumed serialized type.

## 12. Decisions

The owner accepted all recommended choices on 2026-08-16. They are recorded
here because they are part of the authoring and runtime contract, not transient
implementation details.

### D1. Viewport editing: active layer only

Only entities in the active layer can be picked, dragged, or deleted in the
viewport. Other layers remain visible; selecting one in the navigator makes it
editable. This prevents an overlapping foreground prop from stealing clicks
while authoring a background layer.

### D2. Visibility and locking: transient editor state

Visibility and lock controls reset when the level is reopened. Every saved
layer always participates in the game. No new serialized fields are added.
Both controls are included in the initial panel because overlapping artwork is
difficult to author without them.

### D3. Collision: content-specific only

There is no layer-wide collision flag. Tiles and entities retain their own
explicit collision definitions. Visual-only content uses no-collision assets.

### D4. Panel ordering: frontmost at the top

Store and render back to front, but show the vector in reverse, matching
familiar graphics editors. Controls should say `Move Forward` and `Move
Backward` so array direction never leaks into the UI.

The migration and new-level default call layer 0 `Base`.
