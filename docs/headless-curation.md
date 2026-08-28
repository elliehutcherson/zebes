# Headless asset curation

`generate_assets`, `stage_asset_creation`, `stage_asset_redraw`,
`build_terrain`, `build_environment`, and `curate_assets` are the first-party,
windowless asset loop. The commands load
the same complete `AssetWorkspace` as the interactive editor,
but supply a headless texture-resource adapter instead of SDL. There is no
second JSON loader, no ImGui click automation, and no alternate interpretation
of asset references.

`curate_assets` registers these visual kinds:

- `level` renders persisted integrated parallax, tile, and entity composition
  over deterministic 0.5x, 1x, and 2x routes, with contact sheets, isolated
  depth passes, a layout map, and measured coverage/distribution evidence;
- `parallax-artwork` renders native, enlarged, configured repetition, and
  lateral-edge evidence and supports creation, source-redraw, and
  recipe-regeneration candidates;
- `parallax-theme` produces complete-theme route samples, isolated layers,
  repeat-seam frames, coverage findings, and measured wrap separation;
- `prop` resolves the recipe-owned Texture/Sprite/Blueprint graph and renders
  native, pixel-detail, and logical placement-context frames;
- `sprite` renders native and enlarged frames plus an animation strip;
- `terrain` renders its recipe/tileset/texture graph, slope matrix, atlas, and
  owned frames;
- `tileset` renders its atlas, native tile frames, and placement context.

## Running a review

Build the commands:

```bash
cmake --build build/dev --target generate_assets stage_asset_creation stage_asset_redraw \
  build_terrain build_environment curate_assets
```

List the registered kinds:

```bash
build/dev/bin/curate_assets --list_kinds
```

Review an existing asset. `--output` must name a directory that does not yet
exist; the command never replaces an earlier review.

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=level \
  --id=9e20ee58-f4d2-4931-b74b-5555d4b35c00 \
  --output=/tmp/catacombs-level-review
```

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=parallax-theme \
  --id=93fc14e1-265b-420d-afe6-81706f0f08c5 \
  --output=/tmp/cave-theme-review
```

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --id=62ab6a6f-e007-4a53-9ff1-a41da985557c \
  --output=/tmp/cave-crystal-review
```

To review a proposed parallax definition without changing the project, pass a
complete schema-current definition through `--candidate`:

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=parallax-theme \
  --id=93fc14e1-265b-420d-afe6-81706f0f08c5 \
  --candidate=/tmp/cave-theme-candidate.json \
  --output=/tmp/cave-theme-candidate-review
```

Add `--commit` only after accepting that review. The selected ID and candidate
ID must match. Review artifacts publish before persistence, and a successful
commit publishes a second review at `<output>-committed`. A failed persistence
operation therefore leaves the pre-commit evidence intact.

## Full generate, review, commit, re-review loop

Generation creates a new asset. `--recipe_id` or `--recipe_name` selects one
existing recipe whose
terrain link, resolved style, and deterministic pipeline settings are copied as
a template; it does not authorize rebinding that existing asset to new pixels.
Prop generation derives its composition aspect from the template's tile canvas
by default; a 1-by-2 recipe asks the provider for a portrait source rather than
an unrelated square. New creations may override that composition explicitly
with paired `--prop_canvas_tiles_wide` and `--prop_canvas_tiles_high` flags and
may select `--prop_attachment=grounded`, `ceiling`, or `free`. Free placement
also requires paired `--prop_free_anchor_x` and `--prop_free_anchor_y` pixel
coordinates inside the final canvas. The anchor becomes the Blueprint origin,
and the generated Sprite offset is its exact negation. The override changes
only the candidate's validated composition and provider aspect; terrain
palette, isolation, cleanup, and other deterministic settings still come from
the template. `stage_asset_creation` accepts the same flags so an externally
generated source follows the identical candidate contract. Partial dimensions,
non-positive dimensions, incomplete or out-of-canvas free anchors, free anchors
on another attachment mode, and prop flags on non-prop generation fail before
provider work.
When a parallax template requires a transparent overlay but the selected
provider cannot emit transparent pixels, generation names the recipe's exact
solid-matte RGBA value in the provider instructions. The template must use
solid-matte extraction; generation fails before the provider call when it has
no deterministic way to recover transparency. Providers must not substitute a
different chroma-key color.
`--provider=fake` is deterministic and requires no credentials, so this is also
the clean-checkout acceptance path.

```bash
build/dev/bin/generate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --recipe_id=62ab6a6f-e007-4a53-9ff1-a41da985557c \
  --name="Headless Cave Pod" \
  --prompt="one luminous organic cave pod" \
  --provider=fake \
  --output=/tmp/cave-pod-generated

CAVE_POD_ID="$(jq -r .asset_id /tmp/cave-pod-generated/manifest.json)"

build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --id="$CAVE_POD_ID" \
  --candidate=/tmp/cave-pod-generated/candidate.json \
  --output=/tmp/cave-pod-review

build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --id="$CAVE_POD_ID" \
  --candidate=/tmp/cave-pod-generated/candidate.json \
  --commit \
  --output=/tmp/cave-pod-commit
```

For example, a low three-tile floor scatter can retain the same Catacombs
palette and processing while requesting an honest wide source and output:

```bash
build/dev/bin/generate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --recipe_name="Catacombs Funeral Brazier" \
  --name="Catacombs Collapsed Offerings" \
  --prompt="one low connected scatter of broken offerings" \
  --provider=codex \
  --prop_canvas_tiles_wide=3 \
  --prop_canvas_tiles_high=1 \
  --prop_attachment=grounded \
  --output=/tmp/collapsed-offerings
```

A wall-positioned candidate uses the same managed path but authors its origin
explicitly. For a 64×64 output, the centered form is:

```bash
build/dev/bin/generate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --recipe_name="Catacombs Ceiling Chain Frieze" \
  --name="Catacombs Wall Accent" \
  --prompt="one inert asymmetrical patch of funerary wall masonry" \
  --provider=codex \
  --prop_canvas_tiles_wide=2 \
  --prop_canvas_tiles_high=2 \
  --prop_attachment=free \
  --prop_free_anchor_x=32 \
  --prop_free_anchor_y=32 \
  --output=/tmp/catacombs-wall-accent
```

Free placement controls geometry, not gameplay meaning. A decorative wall
accent still has no collider, and integrated review must reject silhouettes
that resemble a switch, pickup, door, or hazard.

The last command publishes `/tmp/cave-pod-commit/manifest.json` before the
mutation and `/tmp/cave-pod-commit-committed/manifest.json` from persisted
state afterward. Its commit first retains the reviewed processed source with
generation provenance, then invokes the compensated
`Api::CreateGeneratedProp` transaction. Parallax artwork uses the same sequence
with `--kind=parallax-artwork` and a parallax-artwork recipe template.

Artwork produced by an external generator enters that same creation path
through `stage_asset_creation`; do not copy it into `assets/source_art` or
manually assemble its Texture/Sprite/Blueprint graph:

```bash
build/dev/bin/stage_asset_creation \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --recipe_id=62ab6a6f-e007-4a53-9ff1-a41da985557c \
  --name="Catacombs Ossuary Reliquary" \
  --input=/tmp/ossuary-reliquary.png \
  --provider=imagegen \
  --model=builtin \
  --prompt="one narrow grounded ossuary reliquary" \
  --output=/tmp/ossuary-reliquary-generated
```

The command verifies and retains the imported RGBA pixels, records their exact
provenance, creates fresh output IDs, and publishes the same strict candidate
and manifest shape as `generate_assets`. Recipe-specific background isolation,
palette mapping, sizing, anchoring, review, and compensated commit remain owned
by `curate_assets`.

## Redrawing retained source without breaking references

Do not overwrite a retained PNG in `assets/source_art` directly. That bypasses
its content digest and leaves the derived runtime texture stale. Generate an
edit directly from the retained source by unique recipe name:

```bash
build/dev/bin/generate_assets \
  --asset_root="$PWD/assets" \
  --operation=redraw \
  --kind=parallax-artwork \
  --recipe_name="Catacombs Near Structural Shell" \
  --prompt="taper both lateral edges into transparent irregular silhouettes" \
  --provider=openai \
  --output=/tmp/shell-redraw
```

The provider must advertise reference-image editing. The OpenAI adapter sends
the retained pixels to the image-edit endpoint; the deterministic fake provider
supports the same contract for tests. Codex currently fails preflight for this
operation rather than guessing at an undocumented image-attachment protocol.

To use pixels produced by an external bitmap editor or generator, import them
through the same candidate schema:

```bash
build/dev/bin/stage_asset_redraw \
  --asset_root="$PWD/assets" \
  --kind=parallax-artwork \
  --id=3e0c2e51-e4d5-49c4-b8fa-ab51d815372e \
  --input=/tmp/redrawn-shell.png \
  --provider=imagegen \
  --model=builtin \
  --prompt="taper both lateral edges into transparent gutters" \
  --output=/tmp/shell-redraw

build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=parallax-artwork \
  --id=3e0c2e51-e4d5-49c4-b8fa-ab51d815372e \
  --candidate=/tmp/shell-redraw/candidate.json \
  --output=/tmp/shell-redraw-review

build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=parallax-artwork \
  --id=3e0c2e51-e4d5-49c4-b8fa-ab51d815372e \
  --candidate=/tmp/shell-redraw/candidate.json \
  --commit \
  --output=/tmp/shell-redraw-commit
```

For a seam-aware redraw, provide the generator with the retained target first,
then the immediate left and right retained sources in composition order. Name
those roles and recipe names in the submitted prompt so the saved provenance
records which neighbours supplied continuity context. If one neighbour is the
wraparound element, say so explicitly. The prompt should also state the overlap
gutter from the environment specification and require irregular transparent
silhouettes instead of a straight canvas-edge cutoff.

Artwork and layout solve different parts of the seam. Stage and review the
neighbour-conditioned pixels through the commands above, preserve the existing
IDs at commit, then declare the gutter overlap in the environment specification.
Rebuild the environment and review the complete `parallax-theme`; an isolated
artwork repeat cannot demonstrate that adjacent, non-identical formations flow
together. `catacombs_processional.json`, for example, uses a 96-pixel overlap
between its 960-pixel Near canvases and carries the same overlap through the
wrap period.

Both routes snapshot the current retained-source and derived-texture digests in
the candidate. Review and commit reject it if another agent changes either
asset before promotion. The commit preserves the source, recipe, and texture
IDs. It advances retained source provenance and digest, rebuilds the runtime
texture with the existing pipeline settings, and compensates all earlier writes
if a later write fails.

Existing assets take a different path. A complete schema-current recipe
candidate may change only mutable recipe settings. The reviewer deterministically
rebuilds the output and commit calls `Api::RegenerateGeneratedProp` or
`Api::RegenerateGeneratedParallaxArtwork`; retained source identity and digest
must remain unchanged. This sequence is a credential-free smoke test of that
settings-only path. The first review calculates all derived fields; the second
command extracts that exact recipe for the guarded commit:

```bash
build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --id=62ab6a6f-e007-4a53-9ff1-a41da985557c \
  --candidate="$PWD/assets/definitions/prop_recipes/62ab6a6f-e007-4a53-9ff1-a41da985557c.json" \
  --output=/tmp/cave-crystal-settings-review

jq '.metadata.recipe' \
  /tmp/cave-crystal-settings-review/manifest.json \
  > /tmp/cave-crystal-settings-candidate.json

build/dev/bin/curate_assets \
  --asset_root="$PWD/assets" \
  --kind=prop \
  --id=62ab6a6f-e007-4a53-9ff1-a41da985557c \
  --candidate=/tmp/cave-crystal-settings-candidate.json \
  --commit \
  --output=/tmp/cave-crystal-settings-commit
```

When editing settings, first review the proposed recipe. The review manifest's
`metadata.recipe` is the exact deterministic result, including its derived
pixel digest. Promote that exact object to the final candidate before commit;
commit refuses stale digests, changed IDs, changed source identity, unknown
fields, and older schemas.

## Building deterministic terrain

Terrain definitions live under `assets/authoring/terrains/`. A versioned build
spec carries the complete generator configuration and one display name; the
top-level and material names must agree. The generic builder creates a managed
Texture/Tileset/TerrainRecipe bundle when that unique recipe name is absent and
regenerates the existing bundle in place on later runs:

```bash
build/dev/bin/build_terrain \
  --asset_root="$PWD/assets" \
  --spec="$PWD/assets/authoring/terrains/catacombs_masonry.json"
```

Regeneration reuses the editor's production terrain transaction. It preserves
the recipe, tileset, texture, terrain, and tile IDs; tile-size or repeat-period
changes fail because they alter atlas topology. Unknown fields, duplicate
recipe names, mismatched spec/material names, missing ownership links, and
ambiguously renamed bundles fail before pixel replacement. The production
Catacombs spec is loaded by a parser test and rebuilds byte-identically.

## Building a complete environment

Environment definitions live under `assets/authoring/environments/`. They refer
to artwork recipes, tilesets, terrains, themes, world layers, Blueprints, and
Blueprint states by unique names; no catalog GUID is authored by hand. Array
order supplies local element, layer, and zone IDs, while entity placements carry
explicit stable local IDs. The generic builder creates manager-owned resource
IDs on the first run and preserves them on later runs:

```bash
build/dev/bin/build_environment \
  --asset_root="$PWD/assets" \
  --spec="$PWD/assets/authoring/environments/catacombs_processional.json"
```

The versioned schema describes the parallax composition, world dimensions,
zones, placed entities, and ordered solid/empty terrain rectangles. Entity
placement materializes the named Blueprint state into the same persistent
Sprite and Collider references as interactive Level Editor placement. Unknown
or ambiguous resource and state names fail the build. The Catacombs
Processional spec replaces the retired Catacombs-specific C++ authoring program
and is also loaded by a shipped parser test.

## Concurrent agents and catalog snapshots

Every `AssetWorkspace` participates in an asset-root advisory lock. Readers
take a shared lock only while loading the complete catalog, then release it and
work concurrently from an in-memory snapshot. Writers take an exclusive lock
before loading and retain it through their full transaction. A contending
writer waits up to 30 seconds, then fails with the owning process ID and root;
it never commits a stale catalog snapshot. Candidate and review output
directories use create-only atomic publication, so agents must give each run a
distinct output directory.

## Review bundle contract

Every successful review is one newly published directory containing PNGs and
`manifest.json`. Publication happens through a sibling staging directory;
`manifest.json` becomes visible only with the complete image set. The manifest
records:

- schema version, asset kind, stable ID, and display name;
- the exact reviewed definition, recipe, or generated-asset creation envelope;
- domain context such as game-view dimensions, level/zone identity, placement
  mode, or authoring zoom range;
- every artifact's purpose, dimensions, decoded-RGBA SHA-256, and render
  metadata;
- measured informational findings and warnings.

Findings are evidence, not automatic taste decisions. A geometric gap is a
warning; exact edge contact cannot prove that transparent pixels form a good
visual seam. Final content promotion still requires visual acceptance.

For `level`, review the zoom contact sheets first, then open questionable
native frames and their matched isolated passes. The layout map and entity-span
metrics expose distribution; the rendered evidence remains authoritative for
silhouette variety, repetition cadence, occlusion, negative space, and gameplay
readability. The command does not emit video or make an aesthetic verdict.

## Adding another curated asset kind

The extension boundary is `CurationReviewer` in `src/curation/registry.h`:

1. Implement `kind()` and `Review()` for the domain. Resolve definitions and
   pixels through `Api`, and keep layout calculations platform-neutral.
2. Return `CurationReview` artifacts rendered with the shared RGBA compositor.
   Do not introduce SDL, ImGui, or hand-written catalog parsing.
3. Override `PublishReview()` when a production review can contain enough
   decoded images to make in-memory publication material. Emit through the
   shared artifact sink so validation, pixel limits, digests, manifest shape,
   failure cleanup, and the final atomic rename remain generic. The level
   reviewer uses this path and accumulates only one route contact sheet.
4. Override `ReviewCandidate()` and `CommitCandidate()` only when the proposed
   document can be validated and persisted atomically at that boundary. Bundle
   assets call their existing compensated creation or regeneration transaction;
   they are never persisted as a loose collection of definitions.
5. Register the reviewer in `scripts/curate_assets.cc` and add focused tests for
   its domain invariants.

The generic registry, safety limits, atomic publication, manifest format, image
digests, and CLI behavior remain unchanged when a reviewer is added. Generation
is likewise extensible through a kind-owned prompt, strict candidate schema,
and existing domain preparation transaction rather than editor automation.
