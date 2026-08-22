# Cave parallax gate inputs

These three generated PNGs are import inputs for the human workflow in
[`docs/cave-parallax-content-gate.md`](../../docs/cave-parallax-content-gate.md).
They are deliberately outside `assets/`: Texture Editor copies an accepted
input into `assets/textures/` and creates its managed definition.

## Files and roles

| File | Suggested texture name | Role | Alpha | Palette colors |
|---|---|---|---|---:|
| `cave-far-fill.png` | `Cave Gate — Far Fill` | Quiet distant wall plate | Opaque | 6 |
| `cave-far-formations.png` | `Cave Gate — Far Formations` | Distant silhouettes and arches | Binary transparency | 13 |
| `cave-near-background.png` | `Cave Gate — Near Background` | Large behind-gameplay framing forms | Binary transparency | 12 |

Every file is 960×540. The images were normalized to a hard two-pixel raster
grid and remapped without dithering to colors already present in
`assets/textures/lucinda_cave.png`.

## Starter theme settings

Add the layers in the table order above. Apply the matching Far, Middle, and
Near Background presets, start with scale `2.0`, offset `(0, 0)`, and both
repeat axes disabled. The scale is only a useful first inspection value: at
zoom 0.5, a 960×540 source needs scale 2.0 to equal the 1920×1080 visible world
area before camera travel is considered.

Use the Theme Editor diagnostics before enabling repetition. These images were
not declared seamless. Toggle one repeat axis at a time, inspect the three-copy
wrapped preview, and record visible seams and coverage margins in the gate.

## Generation record

The built-in image generator used the existing `lucinda_cave` atlas and Cave
Crystal sprite as style-and-palette references. The three final content prompts
were:

1. **Far Fill:** a wide opaque, quiet distant cavern wall plate with broad
   low-contrast shadow basins, no crystals, no platform-like ledges, and no
   gameplay focal point.
2. **Far Formations:** a wide overlay of distant columns, arches, stalactites,
   and sparse mineral flecks with generous open space and no continuous floor.
3. **Near Background:** a wide overlay of large edge-anchored cave ribs,
   columns, a broken arch, and muted embedded crystals, with an open gameplay
   center and no foreground occlusion.

The built-in generator did not emit genuine transparency for the overlays.
They were therefore regenerated on a contrasting magenta matte, the matte was
removed deterministically, transparent pixels were normalized, and the result
was validated as `PaletteAlpha` PNG data. This follows the flat-matte fallback
already specified in the environment artwork plan.

