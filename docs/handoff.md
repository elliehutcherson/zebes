# Handoff: procedural terrain

State of the terrain work as of 2026-08-13, and the things it uncovered but did
not fix. Development is on `main`; 511 C++ tests and 7 Python tests pass through
`scripts/build_and_test.sh`.

---

## Tomorrow's starting point

Phase 3 is implemented and polished in the working tree, but the combined
Phase 2/3 work is not committed yet. Start by reopening **Autumn Forest** in the
Terrain tab and confirming the wall treatment on the same dark-material scene
used today. Wall darkness no longer subtracts value until it clips to RGB
black: it now approaches the authored outline colour asymptotically, and the
preset was retuned from `1.8` to `1.2`. Focused tests pin both endpoints—zero
matches the substrate and the maximum stays coloured—and a generated visual
matrix was inspected for the 32px Autumn and 16px Chunky presets across the
preview scene and all twenty slope shapes.

The generated `lucinda_cave` texture, tileset definition and recipe are
untracked user work. Preserve them. Decide separately whether they are a test
asset worth committing; do not let a terrain-code commit absorb or delete them
by accident.

Recommended order tomorrow:

1. Perform the quick in-editor Autumn comparison above.
2. Review `git diff` and commit the Phase 2/3 generator, editor, tests and docs.
3. Decide whether the `lucinda_cave` assets belong in their own commit.
4. Begin Phase 4 design for slope-connectivity variants only after the current
   atlas contract and selection policy are written down; it is a topology and
   manifest change, not just another renderer pass.

Phase 3's deliberate limitations remain: edge motifs inherit the material's
surface palette rather than owning a separate tint, and short/dry grass plus
snow favour upward-facing edges while moss may continue onto walls. Neither
limitation should be removed by overloading the existing controls; a future
edge palette or facing-policy control should be explicit recipe state.

---

## What was built

A terrain can now be authored procedurally, from inside the editor, and comes
out the far end as a saved tileset a level can paint with.

### The generator

`src/terrain/terrain_generator.{h,cc}` renders a whole blob-47 atlas: all 47
masks for every phase, plus one unit per slope `TileShape`. It rests on two
ideas, both worth knowing before changing it:

- **The surface is a distance band, not an edge.** Every tile is rendered inside
  a 3x3 block of its own neighbours, and depth into the solid is measured with a
  distance transform. A flat top, a 45-degree hypotenuse and a concave notch all
  get a correct band from identical code, which is why slopes cost almost
  nothing here.
- **Band width is modulated by an exactly periodic field**
  (`src/terrain/terrain_field.{h,cc}`). Two adjacent tiles sample the same phase
  and therefore agree along their shared border for free. There is no seam
  bookkeeping anywhere, and `TheBandIsContinuousAcrossATileSeam` in
  `tests/terrain/terrain_generator_test.cc` is what holds that property down.

It was ported from a Python prototype rather than kept as a script, because the
tuning UI had to be in-process: a Python engine behind an ImGui front-end would
put a venv and a process spawn between every slider drag. The prototype is not
in the tree.

### The editor

`src/editor/terrain_editor/` is a tab in the house 3-column shape: controls at
0.2, a `Canvas` viewport at 0.6, output at 0.2. **Create** writes the artwork,
registers its texture and saves a finished tileset, so nothing needs to exist
first and nothing is left unsaved after. Both ways of making a terrain live
there — generate one, or import a `compose_blob47` manifest — because both end
at the same `BuildTerrainCandidate`.

### Scalable generated materials

The follow-up appearance work separates `TerrainMaterial` (artistic intent),
`TerrainPixelProfile` (chunky 16px, balanced 32px, or detailed 64px policy), and
`ResolvedTerrainStyle` (validated concrete measurements). The editor now ships
complete **Cozy Meadow**, **Autumn Forest**, and **Chunky Grass 16** presets
alongside the original materials, and exposes surface texture, three
independently configurable interior layers, palette and pixel-profile controls.

Surface coverage is facing-aware. Top, side and underside depths are authored
independently and blended from the distance-field normal, so diagonal slopes
inherit the same policy without shape-specific branches. An optional wall
layer begins behind the contact shadow and has its own depth and darkness; it
is suppressed on upward-facing ground.

Edge details are a fourth, independent surface concept. Short grass, dry grass,
moss fringe and snow lip use authored one-dimensional pixel profiles which the
renderer carries along the local edge tangent and extends inward along its
normal. Their atlas-global cell grid fits exactly inside the variant period, so
the rhythm does not reset at tile boundaries. Amount, length, clump size, lean
and highlight are authorable; compact 16px banks contain simpler gestures than
the richer 32px/64px banks. Details only recolour solid pixels behind the
surface band, so no family can change collision alpha.

Wall darkness uses a bounded blend toward the authored outline rather than an
open-ended shade ramp. The former ramp could drive Autumn Forest's already-dark
substrate to literal black at ordinary settings. The new mapping preserves its
warm hue across the full accepted range while keeping zero exactly equal to the
interior.

Recipe schema v3 stores the facing and edge-detail values. Loading v1 derives
facing from its old depth/underside-bias line and disables the new wall layer,
preserving legacy behavior without silently using new defaults. Loading v2
explicitly disables edge details. Saving either format upgrades it once to v3.

The ruffle field also uses neighbouring whole frequencies for its directional
terms. The earlier same-frequency construction was seamless but collapsed to a
single repeated sine along a flat top, creating a regular comb at one-tile
repeat periods. The revised field remains periodic while forming uneven top
clumps; the Autumn Forest preset uses a lower density and rounded shape to make
that behavior visible.

The interior is deliberately split into a continuous base treatment (flat,
mottle, soil clods or cobbles), a substrate pattern, and semantic details
(meadow, forest-floor, snow or crystal objects). Substrate choices now include
pebbles, flecks, crosses, diamonds and a weighted mixed-earth bank inspired by
`assets/source_art/pixel_32px.png`; amount, spacing and pattern-only contrast are
independent controls. The latter two layers have separate motif banks and
cached placements, and the editor hides controls which do not apply to the
selected families. Compact banks use deliberately smaller motifs for the 16px
profile instead of scaling down the richer artwork.

Surface and interior texture sample wrapping world-periodic fields. Substrate
and detail locations are generated independently once over the full variant
period and clipped into each tile, so neither moves merely because painting a
neighbour changes the tile's Blob47 mask. Semantic details also preserve
substrate motifs beneath them. All appearance passes stay inside the existing
alpha silhouette, preserving the shared artwork/collision boundary.

The first cleanup pass moved material/profile/preset validation into
`terrain_style`, moved typed and profile-specific motif artwork into
`terrain_motifs`, and made periodic grids, cellular interiors and both motif
placement layers renderer-owned reusable state. Discrete surface motifs now fit their
cell count to the full repeat, so indivisible combinations such as a 96px field
with roughly 5px scallops do not reset at the atlas wrap. The editor tracks
preset selection separately from a material name and displays **Custom** after
any manual visual edit while preserving the selected preset for seed-only
changes.

### Engine changes it required

- `src/objects/tile_shape_geometry.h` is now the single definition of every
  `TileShape` polygon, shared by the generator and the editor's collision
  overlay so drawn artwork and declared geometry cannot drift.
- `Terrain::variant_period` says how a terrain picks variants: `0` is
  weighted-random per cell, `P > 0` means the variants are `P x P` phases of one
  pattern and the cell at `(x, y)` must use phase `(y mod P) * P + (x mod P)`.
  It is **required** in every tileset definition — written always, read with
  `.at()` — so a definition that predates it fails loudly rather than being
  silently reinterpreted.
- `WritePng` (`src/common/image_io.h`) and
  `TextureManager::CreateTextureFromPixels` let the editor turn generated pixels
  into real, registered artwork.

### Bugs fixed on the way

- **Six of the eight steep-slope polygons in `tile_draw.h` were wrong.** Two
  were the exact complement of the solid region and three were byte-identical
  copies of floor shapes. Nothing had ever asserted a vertex;
  `tests/objects/tile_shape_geometry_test.cc` now does, including that every
  ceiling shape is the exact vertical mirror of its floor counterpart.
- `kSlope45BottomRight` was wound opposite to every other polygon, which
  `AddConvexPolyFilled` relies on. Found by the new winding test.
- The prose at `tileset.h:26` claimed the enumerator names mark the right-angle
  corner. They mark the end the wedge tapers to. The ASCII art beside it was
  always correct.
- A multi-tile ruffle period silently collapsed back to one tile, because
  rounding density to whole repeats per tile made every frequency a multiple of
  the tile count. `variant_period = 2` cost 4x the tiles and looked identical to
  1. Frequencies are now chosen coprime with the period.
- The interior mottle was a visible polka-dot lattice — a handful of pure
  sinusoids interfere into a grid. It needed real value noise, now
  `ValueNoiseField`.
- The preview's draft/settle heuristic asked `IsItemActive()`, which answers for
  the *last widget rendered*, not the slider being dragged. It asks
  `IsAnyItemActive()` now, and the policy lives in `TerrainEditorModel` where a
  test drives it.

---

## Not done

### Defaults still need production validation

The Terrain tab has now been exercised interactively while developing the
appearance controls. It still needs several complete levels built with saved
recipes before the defaults should be treated as production-proven taste.

Known rough edge to expect: opening the tab draws a draft instantly and then
settles at full quality, and that settling pass is roughly a one-second hitch at
32px and quality 4.

### Terrain features deliberately deferred

- **Slope connectivity variants (~40 tiles).** Slope-meets-slope currently looks
  identical to slope-meets-flat. Slope units are hand-placed and the brush is
  not involved, so this is more generated art plus a way to choose between
  variants. The manifest has no per-slope variant field, so it needs a schema
  change.
- **Half-blocks as terrain members.** `ParseManifestSlopes`
  (`src/terrain/terrain_detect.cc:53`) accepts only `TileShape` 6-25, so the
  four half-blocks cannot be imported as members. Widening the range is one line
  plus a test; nothing needs it yet.
- **Renaming the `kSlope45*` enumerators** so the names match the geometry.
  `kTileShapeIdentifiers` (`src/objects/tileset.h:70-97`) is a declared tool
  contract that asset pipelines parse off the command line, so correcting the
  prose was the right-cost fix. A rename means changing that contract
  deliberately.

### Editor UX problems, pre-existing

Found while critiquing the terrain flow. None are caused by this work; all of
them bite anyone authoring assets.

- **Destructive actions have no confirmation.** Deleting a tileset
  (`tileset_panel.cc:51`) or a terrain (`tileset_panel.cc:195`) destroys it on
  one click. Separately, the tileset list's `Edit` and `Delete`
  (`tileset_panel.cc:43, 51`) are enabled with nothing selected and guarded by a
  short-circuited `&& model.has_tileset_selection()`, so clicking either with no
  selection does nothing and says nothing. Disabling them would be truthful.
- **`Back` discards unsaved edits with no prompt.** `tileset_panel.cc:70-73`
  calls `CloseActiveTileset()` directly.
- **The Texture tab reports nothing.** Every failure path is `LOG(ERROR)` only
  (`texture_editor.cc:190, 196, 205, 220`), so an import that fails looks
  identical to one that worked unless you are watching a terminal. It is the
  only editor with no in-UI error surface; the tileset editor's dismissible
  banner is the pattern to copy.
- **A new tileset defaults to 16x16** (`tileset_editor_model.cc:41`) while all
  terrain art is 32x32, so a hand-built tileset silently slices the wrong grid
  until someone notices.
- **Create blocks for seconds with no progress indication** — the frame simply
  stops. It is a one-shot authoring action, so this is tolerable, but it reads
  as a hang.
- **Hand-authoring tiles is ~4 interactions each, across 3 columns**: Add in the
  navigator, click the atlas in the viewport, then name it and pick a shape in
  the inspector. There is no drag-select, no shift-click range, and no bulk add,
  which is why the import and generate paths exist at all. A rectangle-select in
  the atlas viewport would collapse most of it.
- **Selecting a tileset does not open it** — you select, then press Edit. No
  double-click handler.
- **Canvas panning is keyboard-only.** `HandleInput` (`canvas.cc:113-121`) reads
  WASD and the arrow keys; there is no middle-mouse drag, despite a comment in
  `blueprint_editor.cc` implying otherwise. Every viewport in the editor
  inherits this.

### Stale documentation

- `src/objects/tileset.h:189-190` says "TileChunk integer values are indices
  into the tileset's tile table". They are tile **IDs** — `viewport_scene.cc`
  resolves them through a lookup keyed on `tile.id`, and rendering fails with
  "level references unknown tile ID" when one is missing. The comment predates
  the change and has been wrong for a while.

---

## Where things are

| Path | What |
|---|---|
| `src/terrain/terrain_generator.{h,cc}` | The rasterizer. No SDL, no ImGui, by rule |
| `src/terrain/terrain_field.{h,cc}` | Distance transform and the two periodic fields |
| `src/terrain/terrain_detect.{h,cc}` | `BuildTerrainCandidate` — where both authoring routes converge |
| `src/objects/tile_shape_geometry.h` | Every `TileShape` polygon, shared with the overlay |
| `src/editor/terrain_editor/` | The tab: shell, model, controls panel, output panel, creation |
| `src/editor/preview_texture_sink.h` | The seam that keeps the panels testable without a window |
| `tests/terrain/`, `tests/editor/terrain_*` | Generator properties, creation, model, panels |
| `scripts/README.md` | The authoring narrative, both routes |
