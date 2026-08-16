# Prop artwork from generated images

**Status: design, not implemented.** Nothing described here exists yet.

Terrain has a complete authoring story: a versioned recipe records the intent, a
renderer derives the artwork, and regeneration redraws every tile without moving
an ID. Nothing else does. A tree is a hand-cut `SpriteFrame` pointing into a
third-party sheet, and there is no tool between "an image exists" and "a blueprint
can be placed".

This is the design for taking a generated image — an AI render, a photo bash, a
painting — to a prop that looks like it belongs in the same game as the ground it
stands on.

## 1. The problem is style, not pixels

Downscaling is easy and mostly solved: `scripts/pixelize_image.py` already does the
correct two-step, a BOX downsample to a logical canvas followed by a NEAREST
integer upscale (`:32-40`).

What does not exist is any definition of what "our style" is. Outside terrain there
is no shared palette, no art-direction document, and no colour vocabulary at all.
Searching the repository for a quantiser — median cut, k-means, colour-distance
matching, palette extraction — finds nothing, in C++ or Python.

So an image reduced to 32px is *smaller*, not *on style*. It keeps every colour the
generator invented, which is exactly the tell: a prop rendered in colours no other
object in the scene uses reads as pasted on, however clean its pixels are.

## 2. The palette already exists, inside terrain

`TerrainMaterial` (`src/terrain/terrain_style.h:181-192`) is five packed RGB colours
— `surface`, `substrate`, `outline`, `accent_primary`, `accent_secondary` — plus
`hue_shift` and `contrast`. `BuildPalette` (`src/terrain/terrain_generator.cc:200`)
expands those into a full `kIndexCount` indexed palette through HSV ramps, with
named semantic roles: outline, surface high/mid/shade, contact shadow, interior,
pattern, decor, botanical, an accent ramp, wall.

That is a real, tuned, already-shipping palette. **A prop quantised to it shares
literal colours with the ground it stands on**, which is the strongest possible
version of "matches", and it costs no new asset type, no new format, and no
invented swatch file.

So the design's central choice: **a prop's style input is a reference to a terrain
material, not a free-floating palette.**

A consequence worth stating plainly: props are keyed to a material, so a cave prop
and a meadow prop are different assets even from the same source image. That is
correct — a mossy log does not belong in an ice cave — but it means the source
image is reusable and the derived artwork is not.

### What this costs in code

`BuildPalette` is file-local: it sits in `terrain_generator.cc`'s anonymous
namespace (lines 17-405), and the header exposes only `TerrainRenderer::Colorize`
as a private method. Nothing outside that translation unit can currently ask for a
material's resolved colours.

So step one of any implementation is a small, honest addition to `terrain_generator`:
a public function returning the resolved palette for a `TerrainGenConfig`. Not a
refactor — the function already exists and already does exactly this.

## 3. The pipeline

Source image (large, soft-shaded, antialiased, arbitrary palette, with a
background) to a shipped prop sheet:

1. **Isolate the subject.** `scripts/fit_tileset.py` already carries
   median-border-colour background removal (`:44-51`) and OpenCV connected
   components (`:53-55`). Reuse both rather than writing a third copy.
2. **Fit to a tile-multiple box.** Keep `pixelize_image.py`'s BOX-then-NEAREST
   discipline. Replace its `center_crop_to_aspect` (`:14-30`): a prop does not want
   a centre crop to a fixed aspect, it wants its alpha bounding box snapped out to
   a whole number of 32px tiles.
3. **Quantise** to the resolved terrain palette from §2. Nearest colour in a
   perceptually reasonable space. **Dithering off by default** — ordered dither
   fights readability at 32px, where every pixel is a deliberate mark.
4. **Clean up.** Hard-threshold alpha: pixel art has no partial alpha, and a soft
   generated edge survives downsampling as a halo of near-transparent fringe pixels
   that reads as blur. Drop orphan pixels.
5. **Pack and emit.** One prop sheet PNG plus a manifest of per-prop rects, then
   register texture, sprite and blueprint through `Api`.

Steps 1-4 are a Python tool with no engine dependency, testable against
`assets/source_art/` alongside the existing `tests/pixelize_image_test.py`.
Quantisation against a known palette and alpha thresholding are both deterministic
and cheap to pin.

## 4. Where it plugs in

Nothing new is needed to *place* a prop. `SpriteFrame` already carries
`offset_x`/`offset_y` (`src/objects/sprite.h:19-20`), which is the hook for
anchoring art larger than its collider — exactly what the current tree uses
(-108, -188). A blueprint pairs the sprite with a collider, and the viewport
already places blueprints.

`Entity::sort_order` decides what a prop draws in front of within its world
layer. Ordered world layers now provide the broader depth boundary: put the
prop in a layer behind or in front of terrain as the scene requires. The prop
pipeline does not choose that layer; placement remains an authoring decision.

### One thing to fix in passing

The existing tree is a 114x94 source drawn at 216x188
(`assets/definitions/sprites/props-47594058-*.json`) — a **1.89x non-integer
scale**, in a codebase whose own documented convention is integer nearest-neighbour
upscaling (`scripts/README.md:224-240`). At that ratio some source pixels cover two
output pixels and some cover one, which is precisely the artefact pixel art exists
to avoid.

New props should be authored at 32px multiples and rendered at an integer scale,
and the generator should refuse anything else rather than emit art that is subtly
wrong in a way nobody will name.

## 5. The recipe

Terrain's recipe records complete authoring intent and re-derives artwork with IDs
preserved. A prop recipe is the same shape with one structural difference: **a
terrain recipe is reproducible from a seed, and a generated image is not.** The
source image is an input asset, not a parameter.

`assets/source_art/` is already exactly that. `scripts/README.md:3-14` defines it as
inputs read only by the tools and never given a texture definition — a split that
exists because a tileset once referenced a file silently replaced with unrelated
artwork. Generated images land there; the derived sheet lands in `assets/textures/`.

A `PropRecipe` therefore records: the source image path, the terrain material it
borrows its palette from, the fit box in tiles, the alpha threshold, and the
texture / sprite / blueprint IDs it produced. Regenerate re-derives artwork without
changing IDs, exactly as `TerrainRecipe` does — so retuning a material can restyle
every prop that borrowed from it.

Per `docs/style-guide.md:45-56`, adding it means a new definition directory, strict
required fields, and a `MIGRATIONS` entry — not a tolerant reader.

## 6. Sequence

Each step builds and tests on its own.

1. **Expose the resolved palette** from `terrain_generator`. Pure addition.
2. **The Python tool**, steps 1-4. No engine change; tested standalone.
3. **Sheet packing and the manifest**, mirroring `blob47_compose`'s split of
   artwork from the description of it.
4. **`PropRecipe`** and its manager, mirroring `TerrainRecipeManager`.
5. **An editor tab**, mirroring the Terrain Editor: controls left, preview centre,
   what-will-be-produced right.

Steps 1-3 deliver usable art with no format change at all. That is the honest
minimum, and it is worth landing before committing to 4 and 5.

## 7. Deliberately out of scope

- **Automatic depth assignment.** World layers now let a canopy sit in front of
  the player or a prop sit behind terrain, but generated artwork cannot infer
  the correct layer. `Entity::sort_order` still orders only within one layer.
- **Animated props.** A sprite is already a flipbook, so this is a packing question
  rather than a design one, but nothing here addresses per-frame consistency —
  quantising frames independently makes colours crawl between them. A shared
  palette decision across a frame set is the fix, and it is not designed here.
- **Colliders.** Generated art gives no collision geometry, and inferring one from
  an alpha silhouette would be the same class of guess the terrain phase deleted.
  Prop colliders stay authored in the Blueprint Editor.
- **Backgrounds.** Parallax layers take a whole texture and have their own scroll
  and repeat semantics. The same style-fit tool serves them, but the emit step is
  different, and zone seaming (`ParallaxZone::fade_length`, authored and still
  unimplemented) is the more pressing gap there.
