# Closing the headless loop

Implementation plan for completing the headless asset pipeline. Extends
[`headless-curation.md`](headless-curation.md); it does not supersede anything.

The headless curation architecture is settled and correct: `AssetWorkspace`
(`src/api/asset_workspace.h`) is the same composition root the editor uses,
with a `HeadlessTextureStore` in place of SDL, and `curate_assets` publishes
atomic review bundles through the `CurationReviewer` registry
(`src/curation/registry.h`). Do not redesign any of that.

**Status: the production generate → review → commit → re-review loop is
complete.** Source redraw, complete environment builds, and concurrent-agent
catalog locking were added during the production Catacombs pass. The only
unchecked item below is an optional project skill wrapper; the documented CLIs
remain the supported automation contract and do not depend on that wrapper.

## Design decisions

Settled — raise a flag before deviating.

**D1 — Relocate the generation stack out of `src/editor/`.** Everything in
`src/editor/image_generation/` except `image_generation_lifecycle_panel` is
already UI-free (service, engine, OpenAI/Codex clients, transports, prompts).
Move it to `src/generation/`; the panel stays in the editor and depends on the
new location. Why: a headless generate command must not depend on the editor
layer. Record the new boundary in `docs/architecture.md`.

**D2 — Bundle-asset commits are recipe-only, through the existing
transaction.** The registry's `CommitCandidate` seam currently refuses props.
Keep refusing raw bundle JSON. Instead, accept a *recipe* candidate and run the
compensated regeneration transaction — `Api::RegenerateGeneratedProp` /
`Api::RegenerateGeneratedParallaxArtwork`, both already on the Api surface.
Why: the bundle can never disagree internally because it is always produced by
its own transaction. This is the "recipe-only mutation path" already named in
`docs/roadmap.md`.

**D3 — Process-per-operation CLIs; no daemon, no MCP server.** Workspace load
is cheap at this catalog size, and separate atomic commands are the easiest
interface for an agent to call. Generation output publishes through the same
staging-directory + `manifest.json` pattern as reviews: extract the atomic
publisher from `src/curation/review.cc` into a shared helper rather than
duplicating it. Why: one publication contract for every bundle an agent
consumes. Revisit only if process startup measurably becomes the bottleneck.

**D4 (resolved) — Generated images commit as new-asset creation.** Surfaced
during implementation: the regeneration transactions intentionally reject a
changed retained source ID or digest (`src/artwork/regenerate_prop_asset.cc`),
so newly generated pixels can never flow through regeneration. Resolution,
preserving D2: retain the reviewed source through the compensated retention
boundary, then commit through the existing compensated
`Api::CreateGeneratedProp` / `Api::CreateGeneratedParallaxArtwork` creation
transaction. Existing-asset recipe commits remain settings-only regeneration.
This is parity, not a compromise — the editor holds the same invariant.
Rebinding an existing asset to a new source stays unsupported everywhere; if
ever needed, it is a new, separately reviewed capability, not a loosening of
the regeneration checks.

## Phase 1 — Close the commit loop for generated assets

After this phase an agent can commit a prop or parallax-artwork recipe
headlessly, with evidence published before and after.

- [x] **Prop recipe commit.** Implement `PropReviewer::ReviewCandidate` /
      `CommitCandidate`: accept a prop-recipe JSON candidate, run the prop
      pipeline (`src/artwork/prop_artwork_pipeline.h` →
      `PreparedPropRegeneration`), commit via `Api::RegenerateGeneratedProp`.
- [x] **Failure-path tests for the commit gate.** Every refusal the commit can
      return gets a test that produces it: candidate/selected ID mismatch,
      recipe digest mismatch, schema-invalid candidate, transaction failure
      after review publication.
- [x] **New `parallax-artwork` reviewer kind.** Recipe review + regeneration
      commit via `Api::RegenerateGeneratedParallaxArtwork`. Only
      `parallax-theme` and `prop` are registered today.
- [x] **Post-commit re-review.** After a successful commit, publish a second
      bundle from persisted state, so on-disk evidence always matches what was
      committed.

Acceptance: `curate_assets --kind=prop --candidate=… --commit` succeeds against
a real recipe; a deliberately corrupted candidate is refused with the review
bundle still published.

## Phase 2 — Headless generation

After this phase the loop starts headlessly: an agent can produce a candidate
without opening the editor.

- [x] **Execute D1.** Move `src/editor/image_generation/` (minus the lifecycle
      panel) to `src/generation/`. CMake targets, includes, and
      `docs/architecture.md` updated. No behavior change; existing tests move
      with the code.
- [x] **`scripts/generate_assets.cc`.** Flags: `--operation`, `--kind`,
      `--recipe_id` or unique `--recipe_name` (or
      prompt inputs per kind), `--provider={openai,codex,fake}`, `--output`
      (must not exist; never replaced). Flow: build prompt →
      `ImageGenerationService` → `src/artwork/generated_artwork_postprocessor`
      → atomically publish candidate JSON + staged images + manifest via the
      shared publisher (D3).
- [x] **Fake-provider test coverage.** The
      `ImageGenerationService::Create(client)` seam already exists — use it to
      test the command end to end with no network and no credentials.
- [x] **Document the agent loop.** Extend `docs/headless-curation.md` with the
      full sequence, split by commit path per D4: generate → review candidate →
      commit (new asset: retain reviewed source + creation transaction;
      existing asset: settings-only recipe regeneration) → persisted re-review,
      with runnable command lines for each step.

Acceptance: the documented loop runs start to finish with `--provider=fake` in
a clean checkout, exercising both the new-asset creation path and the
settings-only recipe path.

## Phase 3 — Reviewer coverage

One reviewer per remaining visual asset kind. The registry makes each purely
additive — publication, manifests, and CLI behavior do not change.

| Kind      | Review artifacts                                  | Notes                                            |
| --------- | ------------------------------------------------- | ------------------------------------------------ |
| `tileset` | Atlas sheet, per-tile frames, in-context placement | Complete                                         |
| `terrain` | Slope-matrix render, generated-atlas frames        | Complete; shares `TerrainReviewScene`            |
| `sprite`  | Native + enlarged frames, animation strip          | Complete                                         |

Acceptance: each reviewer follows the checklist in `headless-curation.md`
("Adding another curated asset kind") — resolve through `Api`, shared RGBA
compositor, no SDL/ImGui, no hand-written catalog parsing — with focused tests
per domain invariant.

## Phase 4 — Agent ergonomics

Only if friction actually appears while running the loop. Do not build ahead of
need.

- [x] **Manifest path on stdout.** Print the published manifest path as the
      final stdout line on success so an agent chains commands without parsing
      logs.
- [ ] **Project skill for the loop.** A `.claude/skills/` entry wrapping
      generate → review → commit → re-review, so future sessions don't
      rediscover the CLI contract.

## Non-goals

- **No tolerant modes.** No `--force` on commit, no default-substitution on
  load. Git is the undo; strict parsing stays strict.
- **No reviewers for non-visual kinds.** Config, colliders, and similar are
  covered by API validation already.
- **No daemon or MCP server** (D3), and no second loader — everything resolves
  through `AssetWorkspace` + `Api`.
- **No auto-verdicts.** Findings stay evidence (`headless-curation.md`:
  "findings are evidence, not automatic taste decisions"). The manifest + PNGs
  must remain sufficient for a human or agent to decide.

## Verification

Per repo rules: run the narrowest test during implementation
(`scripts/test.sh <target>`), the complete affected executable before handoff,
and `git diff --check`. The D1 move touches a broadly consumed target — bound
the affected set with `scripts/test.sh --affected-target` and expect to justify
anything wider. `clang-format -i` + one `scripts/lint.sh` invocation over all
edited translation units.
