# Headless asset curation

`generate_assets` and `curate_assets` are the first-party, windowless asset
loop. They load the same complete `AssetWorkspace` as the interactive editor,
but supply a headless texture-resource adapter instead of SDL. There is no
second JSON loader, no ImGui click automation, and no alternate interpretation
of asset references.

`curate_assets` registers these visual kinds:

- `parallax-artwork` renders native, enlarged, and configured repetition
  evidence and supports both creation and recipe-regeneration candidates;
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
cmake --build build/dev --target generate_assets curate_assets
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

Generation creates a new asset. `--recipe_id` names an existing recipe whose
terrain link, resolved style, and deterministic pipeline settings are copied as
a template; it does not authorize rebinding that existing asset to new pixels.
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

The last command publishes `/tmp/cave-pod-commit/manifest.json` before the
mutation and `/tmp/cave-pod-commit-committed/manifest.json` from persisted
state afterward. Its commit first retains the reviewed processed source with
generation provenance, then invokes the compensated
`Api::CreateGeneratedProp` transaction. Parallax artwork uses the same sequence
with `--kind=parallax-artwork` and a parallax-artwork recipe template.

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

## Adding another curated asset kind

The extension boundary is `CurationReviewer` in `src/curation/registry.h`:

1. Implement `kind()` and `Review()` for the domain. Resolve definitions and
   pixels through `Api`, and keep layout calculations platform-neutral.
2. Return `CurationReview` artifacts rendered with the shared RGBA compositor.
   Do not introduce SDL, ImGui, or hand-written catalog parsing.
3. Override `ReviewCandidate()` and `CommitCandidate()` only when the proposed
   document can be validated and persisted atomically at that boundary. Bundle
   assets call their existing compensated creation or regeneration transaction;
   they are never persisted as a loose collection of definitions.
4. Register the reviewer in `scripts/curate_assets.cc` and add focused tests for
   its domain invariants.

The generic registry, safety limits, atomic publication, manifest format, image
digests, and CLI behavior remain unchanged when a reviewer is added. Generation
is likewise extensible through a kind-owned prompt, strict candidate schema,
and existing domain preparation transaction rather than editor automation.
