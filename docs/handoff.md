# Handoff: derived terrain artwork

State as of 2026-08-15. Work is on the branch **`terrain-derived-artwork`**,
**not merged to `main`**. 622 C++ tests and 22 Python tests pass through
`scripts/build_and_test.sh`.

The design document is [`terrain-derived-artwork.md`](terrain-derived-artwork.md).
It has been trued up against the implementation and no longer trails it.

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

## What is left

### In this phase

1. **Nobody has run the editor.** Every layer is tested headlessly and the
   artwork invariant is asserted end to end, but no test drives a real window.
   Worth walking: paint a derived terrain, place a ramp and a ledge, watch the
   atlas grow, save, reopen, and confirm the artwork survived. Also confirm the
   shape picker greys out with no terrain selected.

2. **Merge to `main`.** The branch was made because committing to the default
   branch is discouraged; `main` has not moved since.

Done in this phase: the manifest true-up (all four items, plus the shape range,
which was replaced by naming the two shapes a unit cannot be rather than by
deleting validation), and the design-doc true-up.

### Carried over from before this phase

- **The Autumn Forest visual check.** Wall darkness became a bounded blend
  toward the authored outline colour and the preset was retuned 1.8 -> 1.2.
  Tests pin both endpoints; nothing automated can judge whether it looks right.
- **`lucinda_cave`.** The texture, its tileset definition and one terrain recipe
  are still untracked. The tileset has been migrated in place, so it opens.
  Whether it ships is undecided.
- **`Create` blocks for seconds** with no progress indication. It still renders
  all 47 masks per phase up front. The real fix is moving generation off the
  render thread.
- **`kSlope45*` names describe the taper end, not the right angle.**
  `kTileShapeIdentifiers` is a tool contract that asset pipelines parse, so
  renaming means changing that contract deliberately.

### Deliberate limitations

- **Slopes ignore `variant_period`.** A derived terrain's key carries the phase,
  so this is fixed for derived artwork; a hand-drawn terrain still has one
  drawing per slope shape whatever the period.
- **Compaction does not exist**, per the fragmentation note above.
- Phase 3's edge-detail limits remain: edge motifs inherit the material's
  surface palette rather than owning a tint, and short/dry grass and snow favour
  upward-facing edges while moss may continue onto walls. Neither should be
  removed by overloading existing controls; a future edge palette or
  facing-policy control should be explicit recipe state.

---

## Where things are

| Path | What |
|---|---|
| `src/objects/tileset.h` | `TileShape`, adjacency, `TerrainCellKey`, `DerivedTile`, `Terrain` |
| `src/terrain/terrain_generator.{h,cc}` | The rasteriser. `RenderShapeTileInContext` is the whole function |
| `src/terrain/terrain_content_index.{h,cc}` | Which tile of an atlas already holds a picture |
| `src/terrain/terrain_placement.{h,cc}` | The shapes a palette may offer, and what a terrain can paint |
| `src/editor/level_editor/terrain_brush.{h,cc}` | Key computation, `TerrainIndex`, the provider seam |
| `src/editor/level_editor/derived_tile_provider.{h,cc}` | Render on miss, dedup by content, append |
| `src/editor/level_editor/derived_terrain_session.{h,cc}` | Opening, showing, committing |
| `tests/editor/derived_artwork_test.cc` | The invariant: painted artwork equals artwork for the real neighbourhood |
| `scripts/render_slope_matrix.cc` | Renders the join sheet for looking at |
| `scripts/migrate_definitions.py` | One migration per field; run it after any format change |
