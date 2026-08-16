# Asset tools

## Where art lives

| Directory | Holds | Read by |
|---|---|---|
| `assets/source_art/` | Hand-drawn originals the tools crop and composite from | These scripts only |
| `assets/textures/` | Finished artwork the game and editor sample | A texture definition, always |

The split matters because every file in `assets/textures/` is expected to have a
definition in `assets/definitions/textures/` pointing at it. Source art has no
definition and never should: it is an input, not a shipped texture. Keeping the
two in one directory is how a tileset once ended up referencing a file that had
been replaced with unrelated artwork, with nothing to catch it.

## Make a terrain in the editor

The **Terrain Editor** tab is where terrains are authored, either way of making
one. Controls sit on the left, a pannable preview of painted ground fills the
viewport, and the right column says what will be produced. **Create** writes the
artwork, registers its texture, and saves a finished tileset -- nothing needs to
exist first and nothing is left unsaved afterwards. Open it in the Tileset
Editor if you want to rename tiles or adjust shapes.

Generated terrains also save a versioned recipe. Choose one from **Recipe** to
reopen every generator control and use **Regenerate** to update its artwork
without changing texture, tileset, terrain, or tile IDs. **Save As** starts a
copy with the same look and fresh IDs. Changes to tile size or **Repeat over**
must use Save As because they change the atlas structure and cannot safely keep
the old tile-ID mapping.

The **Source** control picks between the two routes:

- **Generate** draws the artwork procedurally from the controls on the left.
- **Import manifest** takes a `.json` written by `compose_blob47` (below). Import
  its atlas as a texture first, then name that texture in the Output column --
  a manifest describes artwork, it does not contain any.

Two controls are worth understanding before the rest:

- **Preset** applies a complete visual recipe, not only two colours. **Cozy
  Meadow** is the richer 32px starting point with scalloped grass, soil clods
  and meadow details. **Chunky Grass 16** deliberately uses a smaller palette,
  broad clusters and at most one detail per tile; it is authored for 16px rather
  than downsampled from the 32px recipe. Quality and seed are preserved when a
  preset is selected.
- **Pixel style** controls how material intent is quantised. It is separate from
  tile size so a clean 32px set or an enlarged chunky set remains possible.
  Texture and feature sizes are written in the profile's reference pixels and
  resolved to the chosen tile size by the generator.

- **Repeat over** is how many tiles the surface pattern takes to repeat. At 1,
  every tile is drawn from the same one-tile pattern and a long run has a
  visible rhythm at the tile size. At 2 the set carries 4 phases of a two-tile
  pattern and the brush lays them back down in phase, which costs 4x the tiles
  (188 instead of 47) and removes the rhythm. This is
  `Terrain::variant_period`, and it is why the field is required in every
  tileset definition.
- **Quality** is supersampling, and it sits with the output rather than the
  tuning controls because it only affects what **Create** writes: the preview
  always draws at draft quality so it can keep up with a slider.
- **Pattern accent** and **Detail accent** decide where a motif layer takes its
  colour. *Material* tints the marks like the substrate they sit in. *Accent*
  uses the two accent colours flat. *Gradient* sweeps between them across each
  mark, which is how a crystal or a diamond reads as holographic rather than as
  two flat tones. The accent colours are the material's, so both layers can use
  them; **Pattern contrast** only shapes the material ramp and is disabled in
  the two accent modes.
- **Pattern size** and **Detail size** magnify each mark by a whole number. The
  motif banks are drawn once per pixel style, so this enlarges the art rather
  than resampling it, and spacing grows to match so raising the size does not
  turn a pattern into a blob. A size that would make a mark wider than a tile is
  refused, because such a mark could never be placed and the layer would come
  out empty.

Both routes meet at the same place -- each produces a `Blob47Atlas`, and
`BuildTerrainCandidate` turns either into tiles and rules -- so nothing
downstream can tell a generated terrain from a drawn one.

## Cut a tileset by hand

Generating or importing a terrain is the fast path, but a sheet of one-off
pieces still has to be cut by hand in the **Tileset Editor**. Three gestures do
most of it:

- **Drag a rectangle across the atlas** to add one tile per cell it covers.
  Cells an existing tile already sources from are skipped, so dragging back over
  work already done adds only what is missing. Tiles added this way are
  `kFullBlock`, since a cut cell is usually solid and a shape of `kNone`
  collides with nothing; fix the exceptions in the inspector.
- **Click a single cell** with a tile selected to repoint that tile's source,
  which is what clicking has always done. Both gestures resolve when the mouse
  button comes up, because that is the first moment a click and a drag can be
  told apart.
- **Double-click a tileset** in the navigator to open it, and **middle-drag** to
  pan any viewport in the editor.

Deleting a tileset or a terrain asks first, and leaving one with unsaved edits
asks before discarding them. Deleting a single tile does not ask: it is an Add
and a click away.

## Author a terrain brush (blob-47)

A terrain brush paints the correct edge, corner, and interior artwork based on
what a cell's neighbours are. The full set is 47 tiles, but you never draw 47
tiles: every tile is four quadrants, and each quadrant has only five possible
appearances, so **20 sprites at half tile size generate the whole set**.

The quadrant sheet is 4 rows (north-west, north-east, south-east, south-west) by
5 columns (outer corner, vertical edge, horizontal edge, inner corner, fill).

You never assemble that sheet by hand. Both halves are cropped out of your atlas
at cells you name, so the atlas is the only file you draw in.

**Draw two shapes**, both aligned to the tile grid:

| Shape | Size | Supplies |
|---|---|---|
| A solid 3x3 block of terrain | 3x3 tiles | 16 quadrants |
| A 3x3 **ring** — the same block with its centre tile fully transparent | 3x3 tiles | the 4 concave corners |

A 3x3 block never contains a concave corner, which is why the ring exists: its
hole has one at each of its four corners. Only the 16x16 quadrant diagonally
touching the hole is read from each ring cell, so the ring's outer edges are
never sampled and need not look finished.

Draw the ring in the same material as the block, with the surface treatment
following the same rule. In a top-lit style that means the hole's floor gets the
grass band and the hole's ceiling gets plain dirt.

Seed all 20 quadrants in one command by giving each shape's top-left cell in
atlas tile coordinates:

```bash
# Block at cell (1, 1), ring at cell (12, 19), 32px tiles.
build/dev/bin/compose_blob47 seed \
  assets/source_art/pixel_32px.png 32 1 1 build/grass_quadrants.png \
  --inner-corners 12 19
```

The ring is validated rather than assumed: a filled centre cell, an empty wall
cell, or an off-grid origin fails with a message naming what was wrong.

Then composite:

```bash
build/dev/bin/compose_blob47 compose build/grass_quadrants.png 1 build/grass_blob47
```

This writes `build/grass_blob47.png` and `build/grass_blob47.json`. Import the
PNG in the Texture Editor, then in the **Terrain Editor** set Source to *Import
manifest*, point it at the `.json`, and choose that texture. Create makes the
tileset, all 47 tiles and the terrain in one step.

Omitting `--inner-corners` leaves column 3 blank and `compose` then refuses the
sheet, rather than emitting art that looks finished but is not. To exercise a
terrain before any corner art exists, pass `--placeholder-inner-corners` instead:
the terrain tiles correctly and is fully paintable, but concave corners render
square.

### Variety

To stop a large painted region repeating one tile, append more variant blocks of
5 columns to the right of the sheet and raise the variant count:

```bash
build/dev/bin/compose_blob47 compose build/grass_quadrants.png 3 build/grass_blob47
```

Cells left fully transparent in a variant inherit from variant 0, so varying
only the fill quadrant costs four sprites. The brush picks between variants
deterministically from each cell's coordinates, so repainting a region never
reshuffles it.

### Slopes

Slopes are not autotiled — no neighbour-mask scheme produces them. They are
modular units: one tile per slope `TileShape`, repeated to make a ramp of any
length. Drawing them directly (rather than composing from quadrants) is
deliberate: the diagonal surface band is the detail quadrant compositing handles
worst.

Draw each slope as a single tile that tiles diagonally, so a ramp of any length
is that one tile repeated. Two are enough to start: 45° up-to-the-right and 45°
up-to-the-left.

As with the quadrants, you draw them in the atlas and name their cells. The
`slopes` subcommand crops them into the canonical sheet, whose column order is a
`TileShape`-derived internal contract you never have to reproduce:

```bash
build/dev/bin/compose_blob47 slopes assets/source_art/pixel_32px.png 32 \
  build/grass_slopes.png \
  --tile kSlope45FloorTallRight=10,11 \
  --tile kSlope45FloorTallLeft=12,11
```

Shape names are the `TileShape` enumerators, so a typo fails loudly instead of
landing in the wrong column. `FloorTallRight` is the ramp you walk up going
right: "Floor"/"Ceiling" is the edge the solid mass hugs, and "Tall" names the
side that reaches full tile height. Unnamed shapes stay transparent and are skipped, so
gentle 2:1 and steep 1:2 variants can be added later without any code change.
Pass the sheet to `compose`:

```bash
build/dev/bin/compose_blob47 compose build/grass_quadrants.png 1 build/grass_blob47 \
  --slopes build/grass_slopes.png
```

Slope units are appended below the blob blocks, listed in the manifest with
their `TileShape`, and registered as **terrain members** on import. That last
part is what makes painted ground flow into a slope instead of drawing an edge
against it. Place them from the Tiles palette like any other tile.

### Regenerating the mask template

`generate_blob47_mask` emits a white-silhouette 47-tile atlas plus a CSV mapping
atlas index to neighbour mask. It is useful for verifying the brush with no
artwork at all:

```bash
build/dev/bin/generate_blob47_mask build/blob47_template
```


## Convert artwork to pixel-perfect dimensions

Center-crop an image to 16:9, reduce it to a 320x180 logical canvas, and
nearest-neighbor scale it to 1280x720:

```bash
build/tileset-venv/bin/python scripts/pixelize_image.py \
  input.png output.png \
  --logical-width 320 \
  --logical-height 180 \
  --scale 4
```

The logical dimensions define the pixel-art grid. The integer scale keeps every
logical pixel the same size in the output. Use nearest-neighbor texture sampling
when rendering the result.

## Fit generated tileset artwork

Create an isolated environment and install the image-processing dependencies:

```bash
python3 -m venv build/tileset-venv
build/tileset-venv/bin/python -m pip install -r scripts/requirements-tileset.txt
```

Fit generated artwork into an authoritative template. Tile density is explicit;
this example scales a 16-pixel template to 32-pixel tiles:

```bash
build/tileset-venv/bin/python scripts/fit_tileset.py \
  template.png generated.png output.png \
  --template-tile-size 16 \
  --output-tile-size 32 \
  --debug matches.png \
  --report matches.json
```

The output tile size must be an integer multiple of the template tile size. The
tool fails when automatic component matching exceeds its confidence limit. Use
the debug preview to inspect matches and pass `--mapping` with a corrected JSON
mapping when necessary.
