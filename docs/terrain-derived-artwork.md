# Derived terrain artwork

Generated terrain artwork becomes a pure function of collision geometry, cached
by content rather than enumerated by key. This replaces the slope-connectivity
phase, which was an incremental patch on the thing that is actually wrong.

## 1. The root cause

`TerrainRenderer` is a pure function:

```
(shape, eight neighbour shapes, phase) -> pixels
```

It is already correct for every input; `RenderShapeTileInContext` is that
function with nothing inferred.

The atlas is a **cache of that function**, and it is keyed on the 47-mask. That
key is lossy: it can say "the neighbour is the same terrain" but not "the
neighbour is a wedge". So the cache is keyed on something that cannot represent
the function's own domain, and `AutoContext` exists to paper over the gap by
guessing what the missing information was.

Everything measured in the join matrix follows from that one fact. A slope
ending at air is drawn as buried because the key cannot say "air is there". The
ground beside a ramp is banded against a square because the key cannot say "a
wedge is there". Adding a `SlopeJoin` enum, or a four-state edge classification,
or treating slopes as air, are all ways of making the lossy key slightly less
lossy — each bounded by whatever terrain looks like on the day it is written,
and each needing redoing the next time terrain gets richer.

## 2. The invariant

> A tile's artwork is a pure function of collision geometry and style. Nothing
> about how a tile looks may depend on information the level does not have.

Collision geometry is the source of truth; artwork is derived from it. The
current architecture inverts that — artwork is baked from a guess about
collision geometry, and the level is then obliged to match the guess.

## 3. The design

### 3.1 The key is the real input

```cpp
// Everything a generated tile's appearance depends on, and nothing else.
struct TerrainCellKey {
  TileShape shape;
  // Indexed by Neighbor bit position: N, NE, E, SE, S, SW, W, NW.
  // kNone is air; a neighbour of a different terrain is also kNone.
  std::array<TileShape, kNeighborCount> neighbors;
  // Phase of the periodic field, 0 when variant_period is 1.
  int phase;
};
```

No normalization, no collapsing, no "these two are probably the same". Any rule
that folds one key onto another is a claim about the renderer that would have to
be re-proved every time the renderer changes, and re-proving it is exactly the
work this design exists to stop doing.

### 3.2 Deduplication is by content, not by key

Distinct keys sometimes produce identical pixels. Rather than encode which ones
as a rule, **render first and compare the result**:

1. Compute the key for the cell.
2. In-memory memo: has this key been rendered this session? If so, reuse.
3. Otherwise render the tile.
4. If an existing tile in the atlas holds exactly those pixels, reuse it.
5. Otherwise append a new tile to the atlas and the tileset.

This is lossless by construction, and nobody has to be right in advance about
which keys collide.

**Comparison is exact, and a near miss earns its own tile.** Two ramps meeting
at a peak render two pixels apart in a thousand — very nearly the same picture,
and not the same picture. They do not collapse.

That is deliberate. An approximate comparison needs a threshold, and a
threshold is a claim about how much difference the eye forgives: the same kind
of guess about the renderer that this whole design removes, reintroduced one
layer down. Paying an occasional extra tile is cheaper than owning that number,
especially since deduplication is a saving rather than something correctness
rests on — the atlas is bounded by the neighbourhoods a level contains either
way.

**The content index is derived, not stored.** At load, the editor hashes each
tile's rect out of the atlas texture and builds `hash -> tile_id`. PNG is
lossless, so the hash is exact. No new field on `Tile`, no cache file, no format
change — which is the only way to add this without adding a format invariant to
defend.

The key memo is in-memory only and rebuilt per session. A session's first paint
of each distinct neighbourhood renders one tile, which is milliseconds; the
whole-atlas cost that makes `Create` block for seconds never arises here.

### 3.3 The brush stays generator-free

`terrain_brush` must not depend on `terrain_generator` — it is level-editor
logic, and the generator is an algorithm with its own build unit and headless
tests. The seam is an interface:

```cpp
// Resolves the artwork for a cell whose collision geometry is known.
class TerrainTileProvider {
 public:
  virtual ~TerrainTileProvider() = default;
  virtual absl::StatusOr<int> TileForKey(const TerrainCellKey& key) = 0;
};
```

Two implementations, one per `TerrainScheme`:

- **`kBlob47`** — hand-drawn terrain. Reduces the key to a mask, looks up the
  rule, picks the variant by phase or weight exactly as `SelectVariant` does
  today. A wedge neighbour reduces to "same terrain", which is the correct
  answer for art authored against a mask.
- **`kDerived`** — generated terrain. Renders on a miss, per 3.2.

Both answer the same question. That is what keeps one brush, one level format,
and one downstream renderer.

### 3.4 Slopes stop being a special case

Today a slope is a *member*: a tile the brush counts but never writes, placed by
hand from the Tiles palette. That whole mechanism exists because the artwork for
a slope could not be resolved from the level. Now it can.

For a `kDerived` terrain the user picks a **shape** — collision geometry, which
is the thing they actually mean — and the artwork follows. A slope cell is an
ordinary painted cell whose shape happens not to be `kFullBlock`. Re-resolving
it is safe and correct, because re-resolving only ever changes artwork; the
shape is authored state that the brush reads and never writes.

Consequences, all of them simplifications:

- `TerrainIndex`'s paintable/member split is deleted, along with
  `FindPaintableByTileId`. There is one question left: which terrain owns this
  tile.
- `Terrain::member_tile_ids` survives, but only for `kBlob47` and with its
  meaning changed. It is no longer "tiles the brush must not write" — those
  tiles become paintable like any other. It is now how a hand-drawn terrain
  declares the tiles its mask-keyed rules do not produce, which paired with
  `Tile::shape` is that scheme's shape-to-artwork table. It is renamed
  `shape_tile_ids` to stop the old meaning being read off the name; a field
  called "member" outlasting the concept of membership is how the `Tileset`
  comment came to describe tile IDs as table indices.
  A `kDerived` terrain leaves it empty, because it renders any shape on demand.
- `RefreshNeighbors` stops skipping anything.
- The "collision geometry never changes under the user" constraint is enforced
  in one place — the brush writes artwork for a shape it was given, and has no
  path that chooses a shape.

### 3.5 What the user picks

Terrain mode gains a second axis. Today the user picks a terrain and the shape
is implicitly `kFullBlock`; now they pick a terrain **and** a shape, because the
shape is the authored thing and the artwork follows from it.

What they pick is a single shape, and painting writes a single cell.

An earlier draft made a gentle ramp one two-cell stamp, on the theory that a
half placed without its partner was broken collision. It is not, and the stamp
would have made a real arrangement unreachable. The pieces compose because their
edge heights line up: a gentle ramp's lower half ends at half tile height, a
flat half block sits at half tile height across its whole width, and the upper
half starts there. So

    lower half -> half block -> half block -> upper half

is one continuous surface with a landing in the middle. Stamping pairs would
have forbidden it to prevent something that was never damage in the first place
— a lone lower half is a ramp up to a half-height ledge, which is a level
feature.

Nothing here can leave the level broken, and that is a consequence of the rest
of this design rather than a rule enforced by the palette. Artwork is rendered
from the neighbourhood that actually exists, so whatever is placed is drawn
correctly; collision is whatever was placed. There is no state where the two
disagree, which is the only thing "broken" could have meant.

The choice list is a property of the terrain rather than a hardcoded table: a
`kDerived` terrain renders any shape and offers all of them, a `kBlob47` terrain
offers only shapes it holds artwork for. A palette can therefore never offer a
piece that would render nothing, and a future scheme changes the answer without
touching the UI.

**Exact tile placement stays.** Tiles mode is unchanged and remains the escape
hatch for the case the rules do not cover — the same split Tiled, Godot and LDtk
all keep, and for the same reason.

**Shape is never inferred from a gesture.** Dragging diagonally to mean "ramp"
guesses at intent, which is the exact class of thing this phase removes.

Deferred deliberately, so this phase stays an architecture change: a Shift+wheel
accelerator to cycle shapes over the viewport (plain wheel is zoom and `Canvas`
claims it via `SetItemKeyOwner`), and any grouping of derived tiles in the Tiles
palette.

### 3.6 What the atlas becomes

A derived build product that grows by appending. Tile IDs are assigned in
sequence and never reused, so a level's stored IDs stay valid across every
rebuild. Unused tiles accumulate when a level is edited away from a
neighbourhood; that is fragmentation rather than corruption, and the answer is
an explicit compaction tool that renumbers and rewrites the levels it affects.
Compaction is never implicit, because implicit renumbering would silently
rewrite level data.

## 4. What gets deleted

The point of the phase. None of this is left behind "for now".

| Deleted | Why it existed |
|---|---|
| `AutoContext` | Guessed a neighbourhood the level now supplies |
| `ApplyPartner` | Special-cased the one neighbourhood the guess could get right |
| `TerrainRenderer::RenderShapeTile` | Rendering from an inferred neighbourhood |
| The pre-baked slope block in `GenerateBlob47Atlas` | Enumerated slopes ahead of demand |
| `TerrainIndex::FindPaintableByTileId` and the paintable/member split | Marked tiles the brush must not rewrite, which derived artwork makes safe |
| The `SlopePair` table private to `terrain_generator.cc` | Only `ApplyPartner` used it, and nothing replaces it: the halves of a ramp are placed independently and rendered against whatever is actually beside them |
| The shape-range check in `ParseManifestSlopes` | Half-blocks become expressible for free |
| Slope phasing as a known limitation | Phase is in the key; the limitation dissolves |

Also cleared while the schema is open, from the earlier audit: the manifest's
`slopes` key becomes unconditional, `shape` is spelled with its
`kTileShapeIdentifiers` identifier rather than a raw integer, and `tile_size` is
read with `.at()`.

## 5. Migration

Existing generated tilesets were rendered through the inference path, so their
pixels are the old answer. Both schemes must end up self-consistent, with no
tolerant reader anywhere.

1. **Add `TerrainScheme::kDerived`** and a tileset schema bump. Existing
   terrains migrate to `kBlob47`, which is what they are.
2. **Re-derive generated terrains from their recipes.** A recipe records the
   full `TerrainGenConfig`, so every tile can be re-rendered. Blob tiles come
   out pixel-identical, because a mask with square-or-air neighbours is exactly
   the key the new path computes. Slope tiles change, and only where a level
   actually placed one.
3. **Check whether any level places a slope tile at all** before assuming step 2
   is free. If none do, the migration touches no level data.
4. `member_tile_ids` is dropped by the same migration rather than being read and
   ignored.

## 6. Sequence

Each step builds and tests on its own.

1. **`TerrainCellKey` and the content index.** Key type, hashing, the derived
   `hash -> tile_id` index built from atlas pixels. No behaviour change yet.
2. **`TerrainTileProvider` and the `kBlob47` implementation.** Move today's
   `SelectVariant` path behind the interface; the brush talks to the interface.
   Pure refactor, existing tests hold.
3. **`kDerived` provider.** Renders on miss, appends to atlas and tileset.
4. **Shape-based placement.** The palette offers shapes for a derived terrain;
   `member_tile_ids` and the paintable split are deleted.
5. **Delete the inference path.** `AutoContext`, `ApplyPartner`,
   `RenderShapeTile`, the pre-baked slope block.
6. **Migration and manifest true-up.**

## 7. How it stays true

`tests/terrain/terrain_slope_join_test.cc` currently *documents* the defect — it
asserts a ledge differs from the truth by more than 10%. Under this design it
inverts into the invariant:

> For every cell of every scene, the tile the pipeline resolves must be
> pixel-identical to the tile rendered against the real neighbourhood.

Not "differs by less than a threshold" — equal. Any future change that
reintroduces a lossy key fails it on the next run, and extending the guard to a
new shape family is one line in a scene. That is the mechanism; the invariant in
part 2 is the intent.

## 8. Deliberately accepted

- **Hand-drawn terrain keeps the 47-mask key.** Its artwork is authored against
  that key, so it is not lossy there — a mask is the complete truth about a
  drawing made from quadrants. `kBlob47` is a design choice; `kDerived` is the
  one that had to change.
- **Atlas fragmentation**, answered by explicit compaction (3.6).
- **The first paint of a novel neighbourhood renders a tile.** Milliseconds, and
  memoized for the session.
