# Environment artwork and parallax plan

**Status:** Engineering Milestones 0–3 and zone fades are accepted. The
Catacombs production baseline has finite parallax coverage through 0.5×,
independent masonry, distributed player-scaled decor, initial floor/foreground
variants, and a distinct middle ceiling frieze. Remaining work is one bounded
content-variation pass, not an engine prerequisite.

The completed implementation narrative is preserved in
[`history/environment-artwork-plan-through-2026-08-30.md`](history/environment-artwork-plan-through-2026-08-30.md).

## Scope and layer model

Environment authoring keeps two systems distinct:

- **Parallax themes** are camera-relative, repeated layers stored far to near.
  Use them for cave plates, distant formations, and near framing that should
  move at a parallax factor.
- **World layers** are level-relative sparse tiles/entities stored back to front.
  Use them for background decor, gameplay, and foreground props that occupy
  world coordinates.

The standard world stack is Back Decor, Gameplay, and Front Decor. Depth is a
level-authoring decision; generation and recipes do not assign layers or sort
orders.

## Resource contracts

- `ParallaxTheme` is a standalone managed resource. Levels reference themes only
  through `ParallaxZone::theme_id`.
- `Texture` is runtime pixel content; `Sprite` selects entity frames;
  `Blueprint` binds placeable states; `SourceArtwork` retains authoring input and
  provenance.
- `ParallaxArtworkRecipe` deterministically owns one retained source and one
  output Texture. It does not own a theme or level.
- `PropRecipe` owns a retained source plus Texture/Sprite/Blueprint bundle. It
  does not choose world depth or collision automatically.

Managers own definitions and file lifetimes. Editors and scenes carry stable IDs
or copied immutable snapshots, never manager pointers across refreshes.

## Source and deterministic processing

Imported and generated parallax sources share one bounded ingestion and
retention path. The production pipeline is an ordered typed C++ composition:

1. validate decoded dimensions, encoded bytes, pixels, and digest;
2. frame/crop to the explicit target aspect;
3. rasterize with nearest-neighbor policy where pixel structure requires it;
4. quantize against the resolved terrain/theme palette when configured;
5. isolate transparent overlays from declared alpha or matte policy;
6. apply edge/cleanup rules and validate output geometry;
7. emit final pixels, diagnostics, and byte-stable digests.

Stages operate on copied `RgbaImage` values and have no manager, editor, SDL, or
provider dependency. Preview never mutates managed resources.

## Bundle lifecycle

Preparation is pure. Commit validates retained source, recipe, output IDs,
references, current optimistic snapshots, and pixels before writing. Outputs
publish before recipes or external bindings. Failure compensates completed
writes in reverse order.

Regeneration preserves stable IDs and refuses stale source/recipe/texture
snapshots. Deletion scans external references and removes only the complete
recipe-owned graph. Never delete files around manager/reference rules.

## Theme and level editor contracts

The Theme Editor owns one theme draft, a far-to-near layer rail, ordered artwork
elements, searchable stable-ID texture selection, and complete/selected preview
modes. Saving a theme never saves a level.

The Level Editor owns level and zone drafts. Zone creation validates bounds,
fade geometry, and a selected existing theme before publishing. Assigning a
theme stores only its ID; editing/saving the theme is a separate transaction.

Viewport composition resolves immutable theme snapshots and every unique
texture once per frame. Editor-only grids, outlines, ghosts, and selection do
not enter runtime or headless scene values.

## Parallax composition and fades

Themes render far to near before world layers. Each layer composes its authored
elements in order and repeats the resulting cell according to scroll/repeat
rules. Distinctive formations use deliberately authored finite companions rather
than obvious repetition.

Zone activation and fades are platform-neutral level-domain rules:

- half-open containment chooses the active zone;
- later authored overlap priority is absolute;
- only supported adjacent zones may form a seam;
- primary/secondary ordering is stable across travel direction;
- fade widths are finite, non-negative, and bounded by zone geometry;
- missing themes/textures/handles fail the frame.

The accepted implementation and visual evidence are in
[`history/zone-fades.md`](history/zone-fades.md).

## Catacombs production baseline

Accepted content includes:

- complete finite formations through the 0.5× viewport extent;
- independent Catacombs terrain/masonry;
- player-scaled props distributed beyond the opening route;
- floor and foreground A/B silhouette variation;
- a distinct middle ceiling frieze;
- complete streamed route review with no objective findings.

World-space stains do not belong on parallax masonry. Aging must be baked into
parallax artwork or use the same scroll factor.

## Remaining bounded pass

1. Inspect focused 0.5×, 1×, and 2× evidence for actual repetition.
2. Add one floor-scatter silhouette and one foreground-shroud silhouette only
   where that evidence shows a repeated shape.
3. Preserve entity density, layer, sort order, and collider counts.
4. Review each candidate transiently before persistence.
5. Rebuild the environment twice and compare deterministic digests.
6. Run the complete Catacombs route review once after persistence.

Further prop, wall, or terrain variants are optional polish and do not block the
runtime track.

## Verification

Platform-neutral tests cover resource validation, recipe serialization,
processing, stale snapshots, compensation, zone geometry/fades, composition,
and headless publication. UI tests cover draft/intent behavior. Human review is
required for seams, depth, repetition, occlusion, negative space, and gameplay
readability.
