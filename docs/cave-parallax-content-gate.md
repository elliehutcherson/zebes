# Cave parallax content gate

**Status: accepted with one explicit follow-up: composed Near Formation layers
are Milestone 1.6.**
The three textures were imported, theme composition was assembled, and X/Y
repeat rendering remained filled and stable while scrubbing. Repetition was
accepted for low-salience Far Fill and Far Formations but rejected as the
long-term construction for distinctive Near Formations. The attempt exposed
that Level Editor made zone-owned theme assignment undiscoverable until a
zero-area level was repaired and a zone was selected. The editor now exposes
setup readiness and transactional theme assignment directly, with permanent
field labels and an always-visible Level Contents hierarchy. The prepared
inputs live under
[`notes/cave-parallax-gate-inputs/`](../notes/cave-parallax-gate-inputs/).

This is the accepted review record for Milestone 1 of
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

Negative coverage margins are uncovered world units. Edge deltas are
measurements for seam review, not pass/fail scores. Exact diagnostic numbers
that were not persisted are called out rather than reconstructed.

| Plane | Source PNG | Texture ID | Native size | Repeat X/Y | Scale | Offset | Edge observations | Worst coverage margins |
|---|---|---|---|---|---:|---|---|---|
| Far Fill | [`cave-far-fill.png`](../notes/cave-parallax-gate-inputs/cave-far-fill.png) | `2fd319f2-f8ac-487f-9a28-a8c987c148a8` | 960×540 | X/Y | 1.0 | 0, 0 | Filled and visually acceptable while scrubbing; repetition is low-salience | None on repeated axes |
| Far Formations | [`cave-far-formations.png`](../notes/cave-parallax-gate-inputs/cave-far-formations.png) | `88f60ea9-c1aa-4688-9943-687bb0b45879` | 960×540 | X/Y | 1.0 | 0, 0 | Filled and acceptable while scrubbing; may later use a longer composed repeat cell | None on repeated axes |
| Near Background | [`cave-near-background.png`](../notes/cave-parallax-gate-inputs/cave-near-background.png) | `d034ba7c-4951-456c-98d9-6aefc3e00578` | 960×540 | X only | 1.0 | 0, 0 | Coverage remained filled, but the repeated landmark composition is apparent | No visible Y gap on the reviewed route; numeric margin was not persisted |

Also record:

- level ID and zone ID: level `3edc8568-0c40-4276-9d48-352bebb62362`,
  zone `0`;
- tested camera route: the authored cave route within the full-level
  `65536×1024` zone; exact preview endpoints were not persisted;
- visual result at zoom 0.5, 1.0, and 2.0: coverage and scrubbing accepted by
  human review; distinctive Near repetition remains visually apparent;
- whether the far/middle/near split adds useful depth without foreground
  parallax: yes;
- any player, terrain-edge, crystal, seam, or empty-camera-area readability
  problem: none blocking the gate; repeating the Near landmark plate is the
  recorded composition problem;
- processing defaults suggested by the evidence: retain seamless repetition
  for low-salience far assets; preserve transparent Near outputs as reusable
  elements and compose several variants rather than forcing each output to
  tile alone.

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
   `Near Background` in that far-to-near order. Confirm the left hierarchy
   shows the same order; the first layer is drawn farthest back.
4. For each layer, use **Search Textures** and its live thumbnail to select the
   imported texture. Apply **Far**, **Middle**, or **Near Background**. Adjust
   scale and offset only in the layer inspector, where those values are owned.
5. Open the bottom **Diagnostics** drawer. Enable repetition only on an axis
   that will be reviewed. Choose **Analyze Repetition**, inspect the numeric
   opposing-edge facts, and inspect every available three-copy wrapped
   preview. Record visible seams even when the edge deltas are small.
6. Use **Move Farther** and **Move Nearer** to deliberately scramble and restore
   the order. Confirm that selection follows the moved layer, then save, close,
   and reopen the theme before continuing.
7. In **Level**, open the temporary level or choose **New Level**. Select
   **Level Settings** under **Level Contents**; name the draft, choose the
   `lucinda_cave` tileset, enter
   positive tile-aligned world dimensions and an in-bounds spawn, then choose
   **Frame World**. The tile-grid summary must show whole columns and rows. For
   a new draft, choose **Create Level** only after the setup blockers clear;
   **Review N issues** returns to the complete blocker list.
8. Rename `Base` to `Gameplay` if needed; add non-colliding `Back Decor` behind
   it and `Front Decor` in front. Paint a small route and place at least one
   Cave Crystal on Gameplay or Back Decor according to the intended read.
9. Under **Parallax Zones**, choose **Add Parallax Zone…**. In the creation
   inspector, search for and select the saved Cave Theme, enter bounds covering
   the complete camera route, and choose **Create Zone**. Confirm the committed
   row shows both zone and theme names, the viewport frames it, and **Parallax
   View** reads **Selected Zone**. Leave both fades at zero and save the level.
10. Return to **Parallax Theme**. In the center **Game View Preview**, select the
   level/zone camera context. Preview the complete theme and each selected
   layer, scrub Travel X and Travel Y from 0 to 1, and inspect zoom 0.5, 1.0,
   and 2.0. The viewport always represents the configured 960×540 logical game
   view even when the editor panel is resized. In the bottom drawer, record
   every negative coverage margin and resolve it with deliberate scale,
   offset, source size, or repeat changes. Zone bounds are authored in world
   space; the editor automatically limits them to camera centers reachable
   inside the level at each zoom.
11. In **Level**, inspect the same route with parallax behind the cave terrain
    and crystal. Toggle world and parallax layers independently. Check that the
    Near Background remains behind gameplay and that no background plane reads
    as collision or foreground occlusion.
12. Save both assets, close both editors, reopen them, and repeat the route.
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
