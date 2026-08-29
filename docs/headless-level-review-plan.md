# Headless integrated level review

Implementation plan for reviewing a complete persisted level without opening
the editor. This extends [`headless-curation.md`](headless-curation.md) and the
Catacombs production work in
[`environment-artwork-plan.md`](environment-artwork-plan.md); it does not
replace either document.

**Status: implemented through Phase 3, including focused entity iteration;
0.5x coverage and two distributed prop passes accepted.** The
registered reviewer, deterministic route planner, integrated raster path,
contact sheets, isolated passes, layout map, and manifest evidence are
available. The first persisted Catacombs review established the baseline for
the vertical-coverage pass. The 2026-08-28 validation published 210 samples
over six zoom-specific content tracks as 343 PNG artifacts in a 40 MB bundle.
It reported the Far and Near formation coverage gaps without misclassifying
the tile-filled Gameplay layer as empty. Streamed atomic publication preserves
all 343 artifact IDs, metadata records, and RGBA digests while keeping the
measured peak resident set at 182,964,224 bytes (about 174.5 MiB), rather than
retaining the bundle's 694 MiB of decoded RGBA payloads at once.

The follow-up pass added alternating Far lower-foundation and Near
lower-wall/rubble companions, rebuilt the environment byte-stably, and
published the same 343-artifact review with no objective findings. The complete
0.5x route now reaches the viewport bottom in both finite formation layers;
representative 1x and 2x frames preserve the accepted upper composition. A
single lower companion per layer was rejected during visual review because its
stamp cadence was obvious, so each layer uses two distinct variations.

The later scale-aware prop passes use an exact 32×64 Mouse Player Placeholder
as their size reference. The second pass replaces two of four repeated floor
scatters with low Fallen Votive Tablets and one of three foreground shrouds with
an asymmetric Tattered Valance. Entity count, layer count, and collision remain
unchanged. Its deterministic rebuild and final 343-artifact review again report
no objective findings. Two wall-plaque drafts were rejected because their pale
central shapes read as an interactable at gameplay scale; neither is shipped.

## Outcome

Add `level` to the existing curation registry:

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=level \
  --id=9e20ee58-f4d2-4931-b74b-5555d4b35c00 \
  --output=/tmp/catacombs-level-review
```

The resulting atomic bundle must be sufficient for a human or agent to review
the integrated composition without SDL, ImGui, accessibility permissions, or
screen capture. It contains complete route frames, contact sheets, selected
depth-isolation frames, a layout map, and measured findings in `manifest.json`.

The reviewer produces no movie. PNGs and the manifest are the complete review
contract: they are lossless, directly inspectable, frame-addressable,
content-digested, and covered by the existing atomic publication boundary.

## Baseline production problem

`parallax-theme` review proves theme composition but cannot show the theme with
world-layer tiles and entities. The live Level Editor can show the integrated
scene, but it is not a reliable headless review boundary and cannot produce
repeatable evidence.

Catacombs Processional makes the gap concrete. The logical game view is
960x540, so a 0.5x camera exposes 1920x1080 world pixels. The current Far and
Near formation artwork covers only the upper 540-world-pixel band. The complete
theme therefore exposes a hard horizontal change to the repeated Far Fill in
the lower portion of the frame. The current route also places all three props
inside the first 2,048 pixels of a 16,384-pixel level, and its two
Catacombs-specific props share a tall pedestal silhouette.

The accepted lower-companion pass resolves the formation coverage problem. The
sparse prop distribution and silhouette variety remain open content work.

The integrated reviewer must make those facts visible before more content is
committed. It must not convert aesthetic observations into automatic pass/fail
decisions.

## Design decisions

Settled for the initial implementation. Raise a design issue before deviating.

**D1 - Add a read-only `level` curation kind.** Implement `LevelReviewer` under
`src/curation/`, register it in `scripts/curate_assets.cc`, and retain the
generic CLI, review schema, pixel budget, and atomic publisher. `level` resolves
one persisted `Level` by stable ID through `Api`. It does not parse an
environment-build specification and does not support candidate review or
commit. Why: review must cover the same persisted graph the editor and runtime
load, while `build_environment` remains the separate deterministic authoring
step.

**D2 - Reuse production scene semantics, not editor presentation.** The
reviewer resolves resources through `Api` and reuses the platform-neutral
composition boundaries:

- `ResolveParallaxEnvironment` for active zones and fade weights;
- `CalculateParallaxLayout` for camera-relative formation geometry;
- `ComposeLevelTileRenderBatch` for tile culling and atlas source rectangles;
- `ComposeEntityRenderItems` for sprite bounds and stable draw ordering; and
- `Camera::WorldToScreen` plus the shared nearest-neighbour RGBA compositor for
  raster output.

Render order is primary parallax theme, secondary fade theme at its resolved
weight, then world layers from back to front, with tiles before entities inside
each world layer. The normal frames contain no editor grid, selection tint,
gizmos, or placement preview. Why: the artifact should represent authored
content, not the state of an editor tab.

**D3 - Use deterministic authored-content tracks.** The reviewer does not
pretend to infer player navigation from solid tiles. For each zone it chooses
the longest axis as the travel direction. On the secondary axis it collects
persisted tile centers, active entity anchors, and spawn, then greedily groups
the probes into the minimum number of viewport-sized tracks that covers them.
A zone with no probes receives one geometric-center track. Camera centers are
clamped to level bounds at every zoom and every track includes both travel
endpoints.

The first track is sampled at no more than half of the visible world extent,
giving adjacent frames at least 50 percent overlap for seam review. Additional
tracks cover different floors or prop bands and use edge-to-edge samples; they
do not duplicate the first track's horizontal seam evidence. Track and sample
order is stable by zone, zoom, secondary coordinate, and travel coordinate.

If the logical viewport is larger than the level at a requested zoom, route
planning fails with the minimum viable zoom instead of centering an undefined
out-of-world view.

Review zooms are the authoring minimum, 1x when it lies in range, and the
authoring maximum: currently 0.5x, 1x, and 2x. Zone fade boundaries and their
midpoints receive explicit key frames even when the normal spacing would miss
them. A preflight calculation refuses a route that would exceed the existing
review pixel budget rather than silently dropping coverage.

This route is a composition-review path, not a gameplay traversal contract. If
a later vertical or branching level needs a deliberately authored path, add an
explicit review-request document to the curation command. Do not put review-only
metadata into `Level` until a runtime or authoring owner also needs it.

**D4 - PNG and JSON are the complete output.** Do not broaden
`CurationArtifact` beyond PNG and do not add a video encoder. Overlapping route
frames and contact sheets provide sequence context while preserving direct,
frame-accurate inspection. Why: video adds a codec and extraction step without
improving the production decisions this reviewer needs to support.

**D5 - Findings report measurements, not taste.** Missing or malformed required
resources are hard errors. Geometric coverage gaps and sampled camera centers
with no resolved environment are warnings. Entity counts, occupied route span,
largest empty horizontal interval, layer alpha bounds, and per-frame visible
entity counts are informational evidence. The reviewer never declares a level
beautiful, sufficiently varied, or production-ready.

**D6 - Stream large evidence sets into atomic staging.** The generic registry
publishes through `CurationReviewer::PublishReview`. Its default implementation
retains the existing in-memory `Review()` contract for small asset reviews.
`LevelReviewer` overrides publication and emits each validated artifact through
the shared streaming sink, which encodes the PNG and records its manifest entry
inside the private atomic staging directory before the RGBA buffer is released.
The manifest is written last and the staging directory is renamed only after
the complete review validates. Contact sheets are accumulated one route at a
time, so they do not require retaining their native source frames. Why: atomic
publication is a correctness boundary, but it does not require hundreds of
decoded images to remain resident simultaneously.

## Review bundle

Use stable artifact IDs and paths so a consumer can compare two reviews without
parsing descriptions. The exact frame count follows the route calculation.

```text
manifest.json
layout-map.png
contact-sheets/complete-z050-zone-000-track-00.png
contact-sheets/complete-z100-zone-000-track-00.png
contact-sheets/complete-z200-zone-000-track-00.png
frames/complete/z050/zone-000-track-00/frame-0000.png
frames/complete/z100/zone-000-track-00/frame-0000.png
frames/complete/z200/zone-000-track-00/frame-0000.png
passes/z050/zone-000-track-00/frame-0000/parallax.png
passes/z050/zone-000-track-00/frame-0000/world-layer-0.png
```

### Complete route frames

Render every sampled camera on every review zoom at the configured logical game
view. Artifact metadata records:

- camera center, zoom, and visible world rectangle;
- route, zone, and sample IDs plus normalized route progress;
- resolved primary and optional secondary theme IDs and fade weight;
- visible world-layer IDs, tile count, and entity IDs; and
- the exact back-to-front render sequence.

When a camera center has no active environment, render an obvious checkerboard
behind world content and emit a warning. Do not silently substitute an arbitrary
theme or an opaque black background.

### Contact sheets

Produce one bounded-width sheet per zoom and authored-content track, with frames
in travel order. Artifact metadata records the ordered frame IDs and grid
coordinates, avoiding a font or text-rendering dependency in the raster path.
Per-track sheets stay inspectable when a high zoom needs several vertical or
horizontal bands. Individual frames retain native logical-game-view resolution
for detailed inspection.

### Isolated passes

At the start, middle, and end of each route, plus explicit fade samples, render:

- all parallax content without world layers;
- each authored parallax layer in isolation;
- each world layer in isolation; and
- the complete composition.

Isolation uses transparency or a checkerboard where appropriate so alpha gaps
remain visible. Do not multiply the complete route by every isolated pass; key
samples provide depth diagnosis without exhausting memory or obscuring the main
route evidence.

### Layout map

Render a schematic, bounded-size overview of the complete level. It shows level
and zone bounds, spawn position, world-layer entity anchors, entity sprite
bounds, and the camera-center routes. It is not a beauty render. Its purpose is
to expose spatial distribution problems, including the current concentration
of all props near the beginning of Catacombs Processional.

### Manifest findings and metrics

Reuse existing parallax camera-coverage diagnostics for every theme used by the
level. Add level-owned evidence:

- entity counts and occupied X/Y span per world layer;
- largest horizontal interval between entity anchors, including world edges;
- empty world layers and route frames with no visible authored entities;
- alpha bounds and empty edge bands for isolated parallax layers; and
- route frames whose center resolves no parallax environment.

Alpha and distribution metrics remain informational. Geometry that cannot
cover the declared camera route is a warning. Broken references, invalid source
rectangles, invalid camera geometry, and unreadable pixels stop the review.

## Production review workflow

The command gathers evidence; the reviewer decides whether the authored level
is sufficient. Review the bundle in this order:

1. Scan each zoom's contact sheet for exposed background transitions, abrupt
   visual cadence, repeated landmarks, empty stretches, and inconsistent scale.
2. Compare adjacent native frames wherever a contact-sheet cell looks
   questionable. The 50-percent route overlap distinguishes a real seam or
   coverage gap from a crop at the edge of one frame.
3. Inspect matched isolated passes to identify whether Far, Near, Back Decor,
   Gameplay, or Front Decor owns the problem.
4. Use the layout map and entity-span metrics to evaluate distribution, then
   return to complete frames to judge silhouette variety, depth placement,
   occlusion, negative space, and gameplay readability.
5. Report production blockers separately from optional polish, with concrete
   repair choices and their tradeoffs.

Sufficiency is deliberately not reduced to an entity count or alpha percentage.
A sparse route may be intentional, and several props can still read as one
repeated stamp. The combined spatial evidence and rendered frames provide the
context for that judgment.

## Implementation phases

### Phase 1 - Pure route and render planning

- [x] Add a platform-neutral route planner with stable route/sample IDs,
      authored-content tracks, world-bound clamping, fade key samples, and
      preflight pixel accounting.
- [x] Add a level render-plan type that records the resolved environment and
      ordered world-layer passes for one camera.
- [x] Test horizontal and vertical zones, small worlds, shared boundaries,
      0.5x/1x/2x transforms, stable deduplication, and pixel-budget refusal.

Acceptance: a Catacombs-sized fixture produces deterministic camera centers at
all three zooms, includes both route endpoints, and never exposes space outside
the level merely because a zoom changed. A world smaller than the requested
viewport fails before raster allocation.

### Phase 2 - Integrated RGBA rendering

- [x] Implement `LevelReviewer::Review` and strict resource resolution through
      `Api`.
- [x] Rasterize parallax themes and two-theme fades using the existing layout
      and weight semantics.
- [x] Rasterize tile batches from the level's tileset atlas.
- [x] Rasterize active entities using the first sprite frame and the same
      bounds/order used by Level Editor.
- [x] Preserve world-layer ordering and tile-before-entity ordering.
- [x] Generate transparent isolated passes and opaque complete frames.

Acceptance: a focused fixture with deliberately overlapping solid-colour
textures proves the exact draw order by pixel assertion. A missing theme,
tileset, sprite texture, malformed frame, or unknown tile fails with a message
that names the level and resource.

### Phase 3 - Evidence, diagnostics, and CLI registration

- [x] Produce complete route frames, bounded contact sheets, isolated key
      passes, and the layout map.
- [x] Add camera, environment, draw-order, and visibility metadata to every
      artifact.
- [x] Add coverage, alpha-bound, and entity-distribution evidence to the
      manifest.
- [x] Register `level`, document the runnable command in
      `headless-curation.md`, and update the architecture boundary if
      implementation extracts a new shared scene-composition type.
- [x] Stream level artifacts through atomic staging and verify that the
      resulting manifest and per-ID RGBA digests match the in-memory contract.
- [x] Add a focused-entity mode that plans one clamped camera at 0.5×, 1×, and
      2×, annotates the selected sprite bounds and origin, retains isolated
      passes, and leaves the full route as the production acceptance gate.
- [x] Report workspace-load and review/publication wall time outside the
      deterministic manifest.

Acceptance: two reviews of unchanged persisted assets have the same manifest
and decoded-RGBA digests. `curate_assets --list_kinds` includes `level`, and
Catacombs Processional publishes a complete bundle without SDL or ImGui.
Two focused reviews of the same entity also publish byte-identical manifests
and PNGs while reducing the production bundle from 343 artifacts to 31 for the
current three-layer Catacombs composition.

Remaining speed work keeps the same quality boundary:

- [x] Prepare a generated Prop candidate through the shared deterministic prop
      pipeline, substitute its texture/Sprite for the selected entity in a
      copied level, and stream integrated focused evidence without registering
      handles or persisting the asset graph. Candidate preparation, validation,
      and commit now have one shared library boundary used by both Prop and
      Level reviewers.
- [x] Add an explicit `referenced-level` workspace profile for exploratory
      review. Profiling isolated retained Source Artwork validation at 15.1
      seconds of the roughly 16-second complete workspace load. The new
      read-only profile keeps every renderable level catalog loaded but leaves
      unrelated authoring catalogs empty. On the production Catacombs focus it
      reduced workspace loading from 15.4 seconds to 1.5 seconds and total
      publication from 26.3 seconds to 12.6 seconds. Complete and profiled
      bundles were byte-identical; the default final gate remains complete.
- [x] Add a reference-validated quarantine command for rejected generated
      graphs. `quarantine_assets` supports terrain, Prop, and Parallax Artwork;
      it refuses external references, atomically publishes the complete graph
      plus manifest to a recoverable directory, and only then invokes the
      existing checked bundle deletion while the asset-root write lock is held.

## Catacombs 0.5x content pass

Run this only after Phase 3 publishes the baseline evidence.

Steps 1-6 are complete for two distributed content passes. Additional wall
silhouette and terrain-material variants remain normal content polish.

1. Review the persisted Catacombs level and retain the bundle outside the asset
   tree as the before-state.
2. Measure the lower vertical extent required by every supported camera center.
   Do not assume another 540-pixel strip is sufficient; size and position the
   lower artwork from the measured 0.5x route and include a controlled overlap
   with the accepted upper formations.
3. Generate and curate one Far lower-foundation companion and one Near
   lower-wall/rubble companion through the managed creation workflow. Keep them
   separate from the accepted upper silhouettes. Add a second variation only
   when the contact sheet exposes an objectionable repeat cadence.
4. Add the accepted companions to the environment specification by recipe name,
   rebuild the environment twice, and verify the persisted output is
   byte-stable.
5. Re-run integrated reviews at 0.5x, 1x, and 2x. Correct vertical seams,
   unintended scaling, and newly exposed repetition without weakening the
   supported zoom range.
6. After vertical coverage passes, generate low/wide floor debris, hanging or
   wall accents, and foreground framing. Distribute them deliberately over the
   complete 16,384-pixel route instead of adding another tall pedestal near the
   spawn.

The first Step 6 result uses a static Mouse Player Placeholder with an exact
32×64 collider as the scale reference, plus four low/wide floor scatters, three
ceiling friezes, and three sparse foreground shrouds. The second result keeps
those counts but establishes floor and foreground A/B silhouettes: Fallen
Votive Tablets replace the repeated floor scatter at x=6,848 and x=14,912, and
a Tattered Valance replaces the shroud at x=10,048. The persisted layer
evidence reports nine Back Decor entities spanning x=640–14,912, one Gameplay
player placeholder at x=256, and four Front Decor entities spanning
x=1,472–14,272. The final deterministic rebuild and 343-artifact review have no
objective findings. Production visual review intentionally rejected brighter
hanging artwork that resembled a pickup, grounded ribs that resembled a
collider-free hazard, opaque or invalid larger drapery candidates, and two
free-positioned plaques that resembled interactables.

The terrain follow-up gives this level an independent Catacombs Masonry
Texture/Tileset/TerrainRecipe bundle and leaves the shared `lucinda_cave`
bundle untouched. The first candidate removed the bright crystal cadence but
was rejected because its solid regions read as a flat editor mask at 0.5×. The
accepted candidate adds restrained three-phase cobble relief and enough rim
contrast to preserve collision readability without competing with the detailed
background. Its terrain review contains 425 artifacts and the rebuilt level
review contains 343; both report no objective findings. A second terrain and
environment build preserved every managed ID and produced byte-identical
outputs. A normalized comparison of the rebuilt level against the prior
tileset proves that dimensions, spawn, zones, entities, tile occupancy, and
collision shapes are unchanged.

The subsequent wall-treatment pass also demonstrates why integrated review is
a production gate rather than a presentation aid. Two relief candidates were
rejected as floating slabs. Narrow damp-course and leached-mortar assets passed
their isolated prop reviews and looked unobtrusive in the 0.5× and 1× contact
sheets, but the 2× route exposed a coordinate-space error: as the wall
parallaxed beneath the world-space Back Decor entities, a stain that aligned to
dark mortar in one frame crossed a pale arch in another and resembled a thin
ledge. The four trial placements and both managed asset graphs were removed;
the shipped level remains at 14 entities with unchanged collision. Surface-bound
weathering must be authored into the parallax plane it decorates.

Prefer companion lower artwork over scaling the current upper formations:
scaling would change their accepted landmark size and horizontal rhythm.
Vertically repeating arches, pillars, or ceiling silhouettes is also rejected
because it creates implausible stacked architecture. Redrawing every existing
formation to a taller canvas remains an option if companion seams cannot be
made coherent, but it is the higher-cost fallback because it reopens already
accepted upper content.

Content acceptance:

- the complete 0.5x route has no hard exposed transition caused by insufficient
  Far or Near formation height;
- 1x and 2x preserve the accepted upper composition and horizontal seams;
- isolated passes show each depth band contributing useful depth without
  obscuring gameplay silhouettes;
- contact sheets do not reveal an obvious lower-band stamp cadence; and
- the layout map and complete frames show multiple prop silhouettes distributed
  beyond the opening segment.

## Tests and verification

Expected focused coverage:

- `tests/curation/level_review_route_test.cc` for pure route rules;
- `tests/curation/level_reviewer_test.cc` for resource failures, draw order,
  multi-track contact/layout evidence, pixel-budget refusal, artifacts,
  findings, deterministic output, and streamed publication;
- `tests/curation/review_test.cc` for streamed validation, atomic cleanup, and
  default registry publication; and
- existing viewport/parallax tests when a shared pure composition boundary is
  changed.

During implementation, run the narrowest failing case. Before handoff, run the
complete new test executables and any affected existing executable, format all
edited C++ files, pass all edited translation units to one scoped lint command,
and run `git diff --check`. Use `scripts/test.sh --affected-target` if extracting
a shared scene type introduces several known consumers; a full local build is
not required unless the affected set can no longer be bounded confidently.

## Non-goals

- No editor automation, virtual display, screenshots, or ImGui serialization.
- No second asset loader and no direct catalog JSON parsing.
- No gameplay simulation or claim that the inferred centerline is a player
  navigation path.
- No persisted level candidate mutation or level commit through the curation
  registry. A focused review may substitute prepared Prop pixels in a copied
  `Level` solely to publish transient evidence.
- No video output or encoder dependency.
- No automatic aesthetic verdict or weakening of the 0.5x requirement to make
  the current content pass.
