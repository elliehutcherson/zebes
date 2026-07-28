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
PNG as a texture, create a tileset pointing at it, then use **Import** in the
Tileset Editor's Terrains section with the manifest path. That creates all 47
tiles and the terrain in one step.

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
  --tile kSlope45BottomLeft=10,11 \
  --tile kSlope45BottomRight=12,11
```

Shape names are the `TileShape` enumerators, so a typo fails loudly instead of
landing in the wrong column. Unnamed shapes stay transparent and are skipped, so
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
