# Cave parallax content gate

**Status: generated input PNGs prepared; human import and visual review
pending.** The Milestone 1 authoring and diagnostic controls are implemented,
but this gate cannot be accepted from automated tests alone. The prepared
inputs live under
[`notes/cave-parallax-gate-inputs/`](../notes/cave-parallax-gate-inputs/).

This is the repeatable acceptance record for Milestone 1 of
[`environment-artwork-plan.md`](environment-artwork-plan.md). It proves the
imported workflow and supplies evidence for the processing defaults proposed in
Milestone 2.

## Fixed context

| Item | Value |
|---|---|
| Logical game viewport | 960 × 540 world units (`assets/config.json`) |
| Diagnostic zoom range | 0.5–2.0; provisional authoring range, not a runtime-camera contract |
| Terrain style | `lucinda_cave` terrain recipe and tileset |
| Existing prop | `Cave Crystal`, grounded and non-colliding |
| Theme order | Far Fill → Far Formations → Near Background |
| World order | Back Decor → Gameplay → Front Decor |
| Zone fades | Zero; rendering is deferred until Milestone 5 |

Use a short temporary level or a disposable copy of a level. Do not turn the
gate into the production first level: its purpose is to expose workflow,
coverage, and seam problems before background processing defaults are fixed.

## Input and observation record

Fill every blank during the review. Negative coverage margins are uncovered
world units. Edge deltas are measurements for seam review, not pass/fail
scores.

| Plane | Source PNG | Texture ID | Native size | Repeat X/Y | Scale | Offset | Edge observations | Worst coverage margins |
|---|---|---|---|---|---:|---|---|---|
| Far Fill | [`cave-far-fill.png`](../notes/cave-parallax-gate-inputs/cave-far-fill.png) | _pending_ | 960×540 | _pending_ | _pending_ | _pending_ | _pending_ | _pending_ |
| Far Formations | [`cave-far-formations.png`](../notes/cave-parallax-gate-inputs/cave-far-formations.png) | _pending_ | 960×540 | _pending_ | _pending_ | _pending_ | _pending_ | _pending_ |
| Near Background | [`cave-near-background.png`](../notes/cave-parallax-gate-inputs/cave-near-background.png) | _pending_ | 960×540 | _pending_ | _pending_ | _pending_ | _pending_ | _pending_ |

Also record:

- level ID and zone ID: _pending_;
- tested camera route: _pending_;
- visual result at zoom 0.5, 1.0, and 2.0: _pending_;
- whether the far/middle/near split adds useful depth without foreground
  parallax: _pending_;
- any player, terrain-edge, crystal, seam, or empty-camera-area readability
  problem: _pending_;
- processing defaults suggested by the evidence, if any: _pending_.

## Human workflow

1. Prepare three PNGs with one job each: an opaque Far Fill, a transparent or
   intentionally composited Far Formations layer, and a transparent Near
   Background layer. Keep gameplay silhouettes and foreground occluders out of
   all three.
2. In **Texture**, choose **New Texture**, browse to each PNG, give it an
   unambiguous cave/plane name, choose **Create**, and record the resulting ID.
   Reopen each texture once to confirm the managed copy resolves.
3. In **Parallax Theme**, choose **New Theme** and name it for the cave gate.
   Rename its first layer `Far Fill`, then add `Far Formations` and
   `Near Background` in that far-to-near order.
4. For each layer, use **Search Textures** and its live thumbnail to select the
   imported texture. Apply **Far**, **Middle**, or **Near Background**. Adjust
   scale and offset only in the layer inspector, where those values are owned.
5. Enable repetition only on an axis that will be reviewed. Choose
   **Analyze Repetition**, inspect the numeric opposing-edge facts, and inspect
   every available three-copy wrapped preview at nearest-neighbour display.
   Record visible seams even when the edge deltas are small.
6. Use **Move Farther** and **Move Nearer** to deliberately scramble and restore
   the order. Confirm that selection follows the moved layer, then save, close,
   and reopen the theme before continuing.
7. In **Level**, open the temporary level. Rename `Base` to `Gameplay` if
   needed; add non-colliding `Back Decor` behind it and `Front Decor` in front.
   Paint a small route with the `lucinda_cave` tileset and place at least one
   Cave Crystal on Gameplay or Back Decor according to the intended read.
8. Add a zone over the complete camera route, assign the saved cave theme, and
   leave both fades at zero. Save the level.
9. Return to **Parallax Theme**. Select the level/zone context. Preview the
   complete theme and each selected layer, scrub Travel X and Travel Y from 0
   to 1, and inspect zoom 0.5, 1.0, and 2.0. Record every negative coverage
   margin and resolve it with deliberate scale, offset, source size, or repeat
   changes.
10. In **Level**, inspect the same route with parallax behind the cave terrain
    and crystal. Toggle world and parallax layers independently. Check that the
    Near Background remains behind gameplay and that no background plane reads
    as collision or foreground occlusion.
11. Save both assets, close both editors, reopen them, and repeat the route.
    Compare layer order, texture IDs, factors, offsets, scale, repeat flags, and
    the rendered composition with the values recorded above.

## Acceptance

The gate passes only when all statements are true:

- three hand-authored PNGs can be imported and selected through live catalog
  thumbnails;
- reorder, selection, save, close, and reopen preserve the identical authored
  composition;
- hard theme, level, and catalog validation passes with zero fades;
- every enabled repeat axis has been visually reviewed in a wrapped preview;
- every non-repeating axis covers the recorded route throughout the diagnostic
  zoom range, or an explicit accepted exception is recorded;
- the complete Level preview includes cave terrain and Cave Crystal and remains
  readable;
- the three background planes create useful depth without foreground parallax;
- observations above are complete enough to justify, or reject, Milestone 2
  processing defaults.
