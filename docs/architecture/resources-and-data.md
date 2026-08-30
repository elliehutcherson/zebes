# Resources and data architecture

## Definitions versus runtime resources

Definitions under `src/objects/` are strict serializable values. They hold IDs,
metadata, geometry, and authored behavior—not SDL objects, background tasks,
manager pointers, or mutable runtime state.

Every serialized field is required. Writers emit every field; readers treat a
missing, unknown, or malformed field as corruption. Format changes require an
explicit migration and tests that load every shipped definition.

Managers under `src/resources/` own one definition kind and its filesystem
lifecycle. Cross-kind graph loading belongs to coordinators such as
`AssetWorkspace` and `LevelAssetLoader`, not to manager constructors.

## Texture ownership

`Texture` is metadata and managed pixel identity. `TextureResourceStore` owns
native resources and returns opaque `TextureHandle` values. A handle carries no
portable native meaning; only its owning store may create, resolve, or destroy
it.

Callers retain stable resource IDs or copied immutable snapshots. They do not
keep manager-owned definition pointers or handles across catalogue refreshes or
store destruction.

Transient unsaved editor previews may own native preview resources directly,
but they are destroyed by the preview owner and never enter managed definitions.

## Asset workspace profiles

`AssetWorkspace` composes managers, API access, and the selected load profile:

- editor: mutable catalogues and authoring resources;
- runtime: explicit read-only complete referenced graph;
- headless: normal definitions with `HeadlessTextureStore` and no SDL.

A profile loads only what its tenant needs. Runtime boot may perform synchronous
I/O before the loop; running engines do not block on filesystem work.

## Retained source and recipes

`SourceArtwork` stores lossless authoring input and immutable provenance:
origin, provider/model/request metadata when generated, dimensions, encoded and
RGBA digests, and source path. Reference inputs are generation evidence, not
runtime asset dependencies unless a versioned definition explicitly owns them.

Recipes are strict, versioned reconstruction contracts. They record retained
source IDs/digests, all deterministic processing inputs, owned output IDs, and
expected output digests. A recipe owns only its declared graph; it does not
silently overwrite unrelated Blueprint states, colliders, or level placements.

## Transactions

Preparation is pure over copied values. Commit then:

1. validates current source and target snapshots;
2. preflights every ID, path, definition, reference, and pixel payload;
3. creates or updates dependencies before dependants;
4. publishes the recipe and external binding last;
5. compensates completed writes in reverse order on failure;
6. reports both the primary and rollback failure if compensation fails.

Regeneration preserves stable output IDs and refuses stale optimistic snapshots.
Deletion scans all external references and removes only the complete owned graph.
Files are never deleted directly around manager/reference rules.

## API boundary

`Api` coordinates operations that span managers. It exposes Zebes-owned values,
status results, stable IDs, and opaque handles. It does not leak provider JSON,
filesystem implementation details, SDL types, or editor models.

## Provider-neutral generation

`ImageGenerationSpec` owns prompts, candidates, aspect, transparency preference,
and ordered typed image references. References are copied/moved `RgbaImage`
values with Zebes roles; paths and native handles stop at ingestion/adapters.

OpenAI and Codex adapters validate capabilities before work, preserve reference
order, and return stable provenance. Generated animation is deprecated, but the
ordered-reference boundary remains supported for prop/parallax generation,
redraw, and headless authoring.
