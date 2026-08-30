# Generated prop artwork pipeline

**Status: Milestones 0-5 implemented; imported and generated source authoring
are both available.**
The boulder/`lucinda_cave` and tree/`Cozy Meadow` checks both support the visual
approach. The production policy is the complete resolved terrain palette.
Source hashing, input limits, a versioned coordinator, retained stage artifacts,
and typed stage diagnostics now form the Milestones 1-2 foundation. Strict
`SourceArtwork` and `PropRecipe` schemas, managers, editor ownership, reference
scans, pure `PreparedPropAsset` construction, and compensated bundle creation
are implemented. Bundle deletion and snapshot-guarded deterministic
regeneration are also implemented. The Prop Artwork tab now completes the
durable imported-source workflow with grounded, ceiling, and free/background
attachment modes. Uncommitted imports are session-owned and are discarded on
replacement, Clear, or normal shutdown. Milestone 5's generation service,
session-lifetime polling engine, OpenAI `gpt-image-2` and Codex App Server
adapters, composition-root owners, editor prompt/candidate controls, and
headless generation entry point are implemented; a generated candidate is
retained exactly as an imported PNG is. Only credential-backed live completion
and operational hardening checks remain.

This design covers a static world prop such as a boulder, tree, sign, or ruin.
The result is ordinary Zebes data: a texture, a one-frame sprite, and a blueprint
that can be placed in any world layer. AI generation is one possible source of
pixels, not a new runtime asset kind and not a dependency of the game.

## 1. What the codebase provides now

The first version of this document predated several boundaries that should now
be reused rather than rebuilt:

- `RgbaImage`, `ReadPng`, and `WritePng` provide a platform-neutral image value
  and PNG I/O.
- `PreviewTextureSink` and `SdlPreviewTexture` upload an in-memory image without
  pretending it is a managed texture. This is the correct boundary for every
  pipeline preview.
- `common/BackgroundTask` runs bounded, platform-neutral work and returns a
  typed `StatusOr` result. Terrain creation already demonstrates the required
  prepare-on-worker, commit-on-editor-thread split.
- `TextureManager` can create generated artwork, read it back, replace its
  pixels without changing its ID, and show non-durable pixels.
- Terrain recipes establish strict, versioned authoring records; regeneration
  establishes stable output IDs and stale-snapshot checks.
- Asset-reference scans and generated-terrain deletion establish that a
  multi-record build product is deleted as a bundle, or not at all.
- Editor models, status banners, `ConfirmPrompt`, deterministic asset catalogs,
  and the three-column Terrain Editor establish the interaction conventions.
- World layers and blueprint placement are complete. A prop needs no new level
  representation.

The remaining follow-up is equally concrete:

- Provider-neutral image-generation, credential, and HTTP contracts now exist,
  including fail-fast capability checks and non-blocking RAII cancellation. A
  libcurl multi transport provides bounded verified HTTPS and prompt
  cancellation. OpenAI and Codex adapters, editor selection/review, and the
  headless `generate_assets` path now consume those contracts. Remaining work
  is the opt-in OpenAI integration check, the final real Codex editor
  accept/discard/cancel/shutdown walk, retry/shutdown hardening, and Windows
  Codex process transport before Windows support is advertised.
- The terrain palette and deterministic transforms are shared platform-neutral
  boundaries. The versioned coordinator enforces source limits, records a
  canonical source digest, retains each preview artifact in review mode, and
  reports typed stage metrics. Stage-specific diagnostics can continue to
  expand without changing the editor boundary.
- Source artwork and prop recipes have strict, versioned managers and are part
  of reference scans. A prepared generated-prop bundle now preflights and
  creates its texture, sprite, blueprint, and recipe together with explicit
  compensation. Regeneration uses exact source, recipe, texture, pixel, and
  sprite snapshots; bundle deletion preflights external references and retains
  a recovery record until the runtime outputs are gone.
- The Prop Artwork tab imports or reuses retained sources, resolves a full
  terrain palette, processes on a bounded worker, previews every stage or only
  the finished result, and creates, regenerates, or deletes complete bundles.

The feature should fill those gaps. It should not embed Python in the editor,
shell out to a provider CLI, put provider JSON into domain objects, or add AI to
runtime rendering.

## 2. Ownership and dependency direction

The complete flow is:

```text
prompt / imported image
        |
        v
ImageGenerationClient (editor service; optional)
        |
        v
accepted SourceArtwork (authoring input, retained losslessly)
        |
        v
PropArtworkPipeline (pure, deterministic, platform-neutral)
        |
        v
PreparedPropAsset (pixels + ordinary Zebes definitions)
        |
        v
Api commit on editor thread
        |
        +--> Texture --> Sprite --> Blueprint
        `--> PropRecipe records the build and its stable IDs
```

Only the first edge is remote and nondeterministic. Everything after accepting
a source image must be deterministic from stored pixels plus a versioned recipe.
The UI depends on these services; neither the image pipeline nor its persisted
types depend on ImGui, SDL, a provider SDK, or `Api`.

This distinction is load-bearing. A prompt and model name do not reproduce an
AI image: models change, safety systems change, providers disappear, and many
requests have no usable seed. The accepted source pixels are the authority.
Provider request data is provenance.

## 3. The shared style contract

Literal terrain colours are the accepted initial style rule. They survived two
contrasting rendered checks: a rock whose material family resembles its cave,
and a tree whose foliage and bark must separate inside a bright meadow palette.

The palette is no longer just the five fields in `TerrainMaterial`.
`BuildPalette` also uses pattern contrast, wall darkness, compact-palette policy,
semantic ramps, and accent-gradient rules. Exposing only `TerrainMaterial` would
let terrain and prop experiments disagree before the comparison even begins.

Extract palette construction from `terrain_generator.cc` into a small
platform-neutral `terrain_palette` build unit used by both terrain and prop
artwork. Its public result should use Zebes-owned types and carry:

- the complete ordered semantic swatches, including alpha;
- stable semantic roles such as outline, surface, substrate, contact, decor,
  botanical, accent, and wall;
- the deduplicated opaque colours available to a nearest-colour quantizer.

Callers ask for this result from a validated `TerrainGenConfig`; no caller
reimplements terrain palette construction. Terrain generation must keep using
the extracted function, and its existing pixel tests must remain unchanged.
That is how the extraction proves it did not subtly restyle shipped terrain.

Milestone 0 compared three explicit prop palette policies against the same
source and transform settings:

1. every opaque colour in the resolved terrain palette;
2. a semantic subset, initially outline, substrate, wall, botanical, and accent
   roles;
3. deterministic additional tonal ramps derived from those semantic colours.

The complete resolved palette was accepted. It retained the boulder's planes,
and the second tree/meadow run confirmed that one literal palette can separate
foliage and trunk while staying inside terrain colours. The semantic subset was
too flat and derived ramps did not improve the result enough to justify another
style contract, so both experimental policies were removed from the production
library.

The resolved terrain palette plus the tile size and raster policy form the
artwork-style snapshot. Downstream quantization consumes that snapshot.

The Prop Editor initially selects a `TerrainRecipe` as its style source. A prop
recipe stores both that recipe ID and a snapshot of the resolved artwork style:

- the ID is a strong authoring reference while attached, allowing the editor to
  offer **Refresh from terrain** and allowing deletion checks to name the prop;
- the snapshot is the input to regeneration, so editing a terrain does not
  silently restyle every prop the next time it is opened;
- **Detach style** clears the reference but keeps the snapshot when independent
  ownership is wanted.

If several non-terrain pipelines eventually need independently editable styles,
that usage is evidence for an `ArtworkStyle` resource. Creating one now would
force terrain recipes through a format migration before a second consumer has
proved what the shared object must contain. The snapshot boundary permits that
future extraction without making it a prerequisite.

## 4. The provider boundary

The editor-facing interface is provider-neutral. In conceptual terms it needs:

```text
ImageGenerationSpec
  prompt, optional negative prompt, requested candidates, target aspect,
  transparency preference, ordered owned reference images with semantic roles

ImageGenerationClient
  Capabilities()
  Start(spec) -> request handle

request handle
  Poll() -> pending | candidates | failure
  Cancel()
```

The first adapter may target one provider, but provider names, model names,
request IDs, revised prompts, and response metadata stop at this boundary. A
stable subset is copied into source provenance after a candidate is accepted.
Provider-specific tuning is represented by a versioned adapter configuration,
not an untyped JSON bag in `PropRecipe`.

Raw credentials are supplied by a `CredentialSource`; the first implementation
reads a named process-environment variable. Project/editor configuration may
store a non-secret credential reference, but never the value. Credentials are
never serialized, logged, copied into pipeline or local background-work inputs,
shown in status text, or included in source provenance. The transport owns the
move-only credential for the minimum request lifetime. Logs may contain a local
request correlation ID, provider status code, and sanitized error message.

A remote request must have connect and total timeouts, response byte and image
dimension limits, cancellation, and shutdown behavior. `BackgroundTask` alone
is not this abstraction: destroying its `std::future` can wait for the worker,
and it intentionally has no cancellation. It remains the right tool for bounded
local transforms. The provider adapter needs a cancellable transport request,
or a transport operation with a strict timeout plus a non-blocking request
owner.

**Who polls.** `ImageGenerationEngine` owns every in-flight request and runs on
its own thread under `EngineRunner`, created once at editor startup and
destroyed at shutdown. The editor submits a spec, receives an id, and drains
finished requests from its existing frame poll; it never blocks on the engine.
Results must reach the editor thread regardless, because accepting a candidate
commits resource-manager and GPU state, so the engine is a place to put
retries and rate limiting rather than a way to avoid that handoff.

The engine polls rather than waking on a socket, because the transport exposes
no descriptor its caller can wait on. `HttpOperation::SuggestedPollDelay`
carries libcurl's own timer up to the engine, which sleeps until the soonest
deadline across its requests and indefinitely when none are in flight. The
alternative — `curl_multi_socket_action` with dynamically registered sockets —
was rejected: `NotificationSet` seals its source list before a worker starts
precisely so the list is never mutated while a thread is armed, and breaking
that invariant buys a fraction of a second on requests that take tens.

Shutdown does not drain. Stopping the runner abandons an unfinished request,
and destroying the engine cancels it without joining remote work.

Invalid response bytes, unsupported formats, oversized images, empty candidate
sets, authentication failures, rate limits, and cancellation are distinct
errors. The adapter decodes accepted bytes to `RgbaImage` through common image
I/O; no provider object crosses into the pipeline.

Generation should ask for a single isolated subject, a simple or transparent
background, the selected view and light direction, and the target style. Those
instructions improve the input, but deterministic processing and validation
remain authoritative. A good prompt is not an invariant.

**The first adapter targets OpenAI `gpt-image-2`.** Its generations endpoint is
one synchronous POST returning base64 PNG candidates, so an operation wraps a
single transport request with no second state machine. `n` bounds candidates at
ten. It rejects transparent backgrounds, unlike `gpt-image-1`, so the adapter
reports `supports_transparency = false` and the capability check refuses such a
spec before any request is sent; isolation removes the background instead,
which is the path imported sources already take. Reverting to `gpt-image-1` for
real alpha is a change to `OpenAiImageConfig`, not to the adapter.

## 5. Managed source artwork

Add an editor-only `SourceArtwork` resource under
`assets/definitions/source_artworks/`; store its lossless image under
`assets/source_art/` using an ID-backed filename. It is not a `Texture`, is
never loaded by the renderer resource store, and is not shipped as runtime art.

Implemented: `SourceArtworkManager` constructs the ID-backed path rather than
accepting one from a caller, writes lossless PNG pixels plus the strict
definition, validates canonical decoded-pixel SHA-256 on every load/read, and
enforces the same source limits as the deterministic coordinator. Imported and
generated provenance are tagged alternatives; nullable generated fields are
written explicitly. API deletion is blocked while any prop recipe references
the source.

Its strict, versioned definition contains:

- stable ID, display name, and source-art path;
- a tagged provenance value: `imported` or `generated`;
- for imported art, the original filename and import time;
- for generated art, the provider and model identifiers, submitted prompt,
  optional provider-revised prompt, provider request ID when available, and
  generation time;
- decoded width, height, and a content digest.

Every field is written. Inapplicable values use the tagged alternative or an
explicit null; readers do not guess defaults. The source manager validates that
the path stays under `source_art`, the image decodes, dimensions and byte count
are within authoring limits, and the digest matches. Use SHA-256 over a canonical
width/height header followed by decoded RGBA bytes, so encoder metadata does not
change identity.

The editor build may copy this authoring data into its working asset tree.
Runtime packaging must exclude `source_art` and editor-only source/recipe
definitions; retaining an input does not make it a shipped texture.

The accepted source is retained once a prop bundle references it even though
only the final texture ships. That is what makes reprocessing possible without
paying for another remote request or hoping a changed model returns the same
boulder. A newly imported source remains owned by the current editor draft until
bundle creation succeeds. Replacing or clearing that draft, or normally closing
the editor, removes the unreferenced definition and PNG. Selecting an existing
retained source never transfers ownership to the session. Intermediate stage
images are a session cache and need not be persisted.

Source artwork participates in reference scans. Deletion is refused while a
prop recipe references it. A future recipe may share one source across several
style variants, so ownership cannot be inferred from filename or directory.

## 6. The deterministic pipeline

Implement the transforms in a new platform-neutral `src/artwork/` subsystem
over `RgbaImage`. The existing Python tools are useful prototypes and offline
utilities, but making the interactive editor depend on a Python executable,
virtual environment, OpenCV, and SciPy would create a second deployed runtime.
Shared C++ transforms also let editor, tests, and future command-line front ends
execute identical code.

The pipeline is an explicit ordered composition of typed stages, not a generic
node-graph framework. Each stage accepts an immutable artifact and its typed
configuration, validates its preconditions, and returns a new artifact plus
diagnostics. A stage never mutates managed assets.

### 6.1 Decode and validate

Validate positive dimensions, exact RGBA byte count, configured pixel and byte
limits, and non-empty image content. Normalize orientation and colour encoding
at decode time. The in-memory contract is sRGB RGBA8.

### 6.2 Isolate subject

Prefer meaningful source alpha. Otherwise estimate the border colour and remove
only background connected to the image border; a global colour deletion can
erase a similarly coloured part of the boulder. Connected components identify
the likely subject and report confidence.

Fail rather than guess when there is no foreground, several similarly large
subjects, the subject touches every edge, or confidence falls below the
configured threshold. The preview shows the mask over a checkerboard. Manual
mask painting is a later tool, but the stage boundary leaves room for it without
changing downstream transforms.

### 6.3 Compose and anchor

Fit the isolated bounding box into an authored canvas measured in terrain tiles.
The canvas is a whole number of selected-style tiles and has transparent
padding. Preserve aspect ratio; never center-crop the subject to force an
aspect.

The author chooses a persisted attachment mode. Grounded props derive the
bottom-center subject contact; ceiling props derive the top-center contact; a
free/background prop stores an explicit anchor in final-texture pixel
coordinates. Free subjects are centered in the canvas independently of their
anchor, so moving the entity origin never silently moves its pixels. The
preview draws the tile grid, subject, and anchor.

### 6.4 Rasterize to the pixel policy

Downsample in premultiplied-alpha space with an area/BOX filter. If the resolved
style calls for pixels larger than one output pixel, reduce to the logical grid
and expand by an integer nearest-neighbor scale. Non-integer scale is invalid.
The finished texture is already at its render size; `SpriteFrame::render_w` and
`render_h` equal its source dimensions.

This replaces the existing tree's 114x94-to-216x188, 1.89x render scale. Unequal
pixel sizes are not an allowed output of the pipeline.

### 6.5 Quantize

Map every sufficiently opaque pixel to the deduplicated prop palette in the
resolved artwork-style snapshot using a documented perceptual colour distance.
Use a fixed colour space and tie-break by palette order so results are
deterministic on every platform.

Dithering is off by default. At prop scale it often replaces intentional clusters
with noise. If later added, it is an explicit enum and a recipe field, not a
boolean whose algorithm can change silently.

### 6.6 Edge treatment

Optionally replace pixels on the inside of the alpha boundary with the resolved
outline role and width. Keeping the outline inside preserves the authored
silhouette and canvas fit. This stage must not invent alpha outside the subject.
It is separately previewable because an object with deliberate thin features
may need it disabled or reduced.

### 6.7 Cleanup and validate

Hard-threshold alpha, clear RGB under transparent pixels, and remove only
components below an explicit area limit. Report every removed component. A
second substantial component is an error rather than debris to discard.

Final validation requires:

- valid RGBA storage and configured size limits;
- a non-empty opaque subject with no partial alpha;
- RGB colours drawn only from the selected prop palette;
- whole-tile canvas dimensions;
- anchor inside the canvas and an opaque contact pixel within the configured
  tolerance for grounded and ceiling props; free/background anchors require no
  inferred contact pixel;
- 1:1 sprite source-to-render dimensions.

Validation is a stage so final-only mode still reports the exact failed
invariant instead of emitting a malformed asset.

## 7. Preview and execution semantics

Preview policy changes what the editor presents and retains, not how pixels are
produced:

- **Review each step** runs the deterministic coordinator once on a bounded
  worker, retains every stage image, and opens at isolation. Previous/Next
  navigates source, isolation, composition, rasterization, quantization, edge
  treatment, cleanup, and the in-context result. Editing an input invalidates
  the prepared result and requires an explicit reprocess.
- **Finished only** runs the same stage list in one bounded worker operation and
  retains only the accepted source, final artifact, diagnostics, and compact
  in-context preview. On failure it reports the stage's typed error without
  publishing any output.

If generation returns several candidates, finished-only mode processes each
candidate and shows the finished variants; it does not silently choose a raw
candidate. Selecting a finished variant determines which source image is
retained.

The draft model records a monotonically increasing revision. Each worker result
carries the revision and input digest it processed. A result for a superseded
revision is discarded rather than replacing a newer preview. Local processing
uses `BackgroundTask`; resource-manager calls, source persistence, GPU upload,
and final commit stay on the editor thread.

Stage previews are ordinary `RgbaImage` values uploaded through
`PreviewTextureSink`. The final view also composites the prop over a small scene
rendered from the selected terrain style, with the tile grid and anchor visible.
That in-context composition is preview-only and is never baked into the prop.
The author can left-drag the prop around this scene. Grounded and ceiling props
follow the nearest valid terrain surface; free/background props follow both
pointer axes. The gesture changes only transient preview placement—level
placement remains a Level Editor concern, and neither the recipe nor prepared
artwork is mutated.

Canvas-backed movement shares a platform-neutral pointer-drag controller with
Level Editor entities. It preserves the original grab offset, while each caller
continues to own picking, constraints, and durable state.

### Example: a background boulder in finished-only mode

1. The author selects a terrain recipe, enters a boulder prompt, and requests
   candidates. The provider adapter adds the style brief and starts a
   cancellable remote request.
2. Each returned image is decoded and passed through isolation, tile fit,
   rasterization, palette mapping, edge treatment, cleanup, and validation on a
   worker. The UI shows only the finished in-context variants.
3. The author selects one result and adjusts its canvas or anchor if needed.
   Doing so re-runs the deterministic stages; it does not call the provider.
4. Create retains the selected raw image as `SourceArtwork`, then commits the
   final texture, one-frame sprite, collider-free blueprint, and `PropRecipe`.
5. The existing Level Editor places that blueprint in a world layer behind the
   ground layer. No AI or prop-pipeline object is needed to render the level.

In review-each-step mode the same run retains and exposes every transform from
step 2. The final pixels and committed bundle are otherwise identical.

## 8. Recipe and output bundle

`PropRecipe` is a strict, versioned editor resource under
`assets/definitions/prop_recipes/`. It records the complete intent required to
reproduce the final pixels from the retained source:

- ID and display name;
- `source_artwork_id`;
- nullable attached terrain-recipe ID plus the resolved style snapshot;
- typed isolation, layout, raster, quantization, edge, and cleanup settings;
- texture, sprite, and blueprint IDs produced by the build;
- expected generated `SpriteFrame` and final pixel digest;
- pipeline implementation version.

Implemented: the initial schema writes every style role and every current
pipeline setting. Tile size and pixel-block policy live only in the resolved
style snapshot, while canvas dimensions live only in composition settings; the
raster and cleanup stages receive derived values so a persisted recipe cannot
contain contradictory copies. Managers preserve stable pointers on save, and
API creation/save preflight every referenced source and output definition.

The implementation version is not permission to carry every old algorithm
forever. A deliberate migration either preserves old output by translating its
settings or marks the recipe as requiring explicit reprocessing. Unknown future
versions and missing fields fail.

One generated prop initially emits one texture, not a shared prop sheet. The
sprite frame already is the manifest for one image, while incremental sheet
packing introduces slot ownership, fragmentation, compaction, and source-rect
stability before runtime measurements show that texture switching is a problem.
Keep packaging behind `PreparePropAsset` so a later atlas resource can change
the commit strategy without changing isolation or style processing.

Generated texture and source filenames should be ID-backed, with display names
kept in definitions. Reusing `CreateTextureFromPixels` unchanged would make a
rename/path collision policy part of the feature because that method currently
uses `<name>.png`.

The one-frame sprite uses the whole image, renders 1:1, and derives offsets from
the anchor so entity position is the authored contact point. The blueprint has
one default state referencing that sprite and an empty collider ID. A decorative
background boulder needs no collider; a collidable prop can be completed in the
Blueprint Editor without the artwork pipeline guessing geometry.

## 9. Commit, regeneration, and deletion

Follow terrain's prepare/commit shape but make the bundle boundary explicit:

1. Implemented: `PreparePropAsset` runs with copied source pixels, style
   snapshot, caller-allocated stable IDs, and recipe settings. It returns every
   stage preview plus validated final pixels and complete definitions with no
   `Api`, filesystem, SDL, or manager access.
2. Implemented: `Api::CreateGeneratedProp` confirms that the accepted source
   snapshot is still current, preflights names, IDs, references, and exact
   output paths, then creates texture, sprite, blueprint, and recipe in
   dependency order. Source acceptance is deliberately a preceding operation so
   processing and preview use the managed source boundary. The current editor
   session owns that record until output creation succeeds; cancellation or
   failure keeps it available only for the rest of the draft rather than
   publishing unfinished work indefinitely.
3. Implemented: a failure rolls back in reverse order. Errors report both the
   primary failure and any failed compensation; they never claim success with a
   partial bundle.
4. Implemented: the recipe becomes visible only after every output exists.

Generated texture pixels use `textures/props/<texture-id>.png`; display names
remain in definitions. Texture, sprite, and blueprint definitions publish from
sibling temporary files, so failed writes do not expose truncated JSON.

The current managers do not provide a general cross-file transaction. Do not
hide that behind a class named `Transaction`. Either add staging/commit support
with well-defined cache behavior or use explicit compensating operations as
terrain does and test every failure point. The same helper should be suitable
for generated terrain once proven, rather than becoming prop-only transaction
machinery.

Implemented regeneration starts from the stored `SourceArtwork`, never from a
new provider call. `PreparePropRegeneration` is a pure worker boundary that
retains every preview and preserves texture, sprite, blueprint, and recipe IDs.
`Api::RegenerateGeneratedProp` may replace texture pixels and update the
recipe-owned sprite frame only after comparing the live source metadata,
recipe, texture definition and pixels, and sprite with the exact snapshots used
by the worker. A mismatch refuses every write as stale. A commit failure uses
explicit compensation to restore the prior recipe and sprite definitions.

The blueprint is an output binding but is not overwritten during regeneration:
the author may have added collider states in the Blueprint Editor. Regeneration
updates only recipe-owned artwork and frame geometry. **Generate new source** is
a separate operation that creates a new candidate and requires confirmation
before replacing the retained source; **Save As** creates a fresh bundle and
fresh IDs.

`Api::DeleteGeneratedProp` deletes the bundle as one operation. It first scans
the output members for external referrers: placed entities naming the blueprint,
other blueprints reusing the sprite, and other sprites reusing the texture. Any
such use blocks deletion and names the referrer. On success it removes blueprint,
sprite, and texture in dependency order, then removes the recipe. Keeping the
recipe until the outputs are gone preserves the IDs needed to retry after an I/O
failure; already-missing outputs are tolerated. It removes the source last and
only when no other recipe references it; a shared source remains and does not
block deleting this prop.

## 10. Editor shape

Implemented: the Prop Artwork tab uses the established three-column layout:

- **Input:** import, reuse, or explicitly delete retained source art; attach,
  refresh, or detach a terrain recipe's resolved style; choose grounded,
  ceiling, or free/background attachment; and tune isolation, canvas, raster,
  edge, and cleanup
  settings; and describe, generate, cancel, review, and accept a generated
  source. The generation section is one control at a time: a running request
  shows only Cancel, and a finished one replaces the prompt with candidate
  navigation, Accept, and Discard.
- **Preview:** stage selector in review mode; checkerboard, tile grid, rulers,
  anchor, automatic fit, diagnostics, and final in-context view. A candidate
  under review takes the preview over every stage until it is accepted or
  discarded, and stage stepping waits, because the buttons deciding its fate
  are in the input column beside it. It uses the
  shared editor `Canvas`, so wheel zoom, keyboard/middle-drag panning, ruler
  behavior, zoom limits, and Fit framing stay consistent with the Terrain,
  Tileset, Blueprint, and Level editors. Context composition expands around the
  complete prop texture with one tile of margin; a tall prop is never clipped
  merely because the terrain preview has too little space above its ground.
- **Output:** recipe selection, review/finished-only policy, Process, Create,
  Open, Save As, Apply Regeneration, confirmed bundle Delete, and confirmed
  Clear workspace. Clear closes a saved recipe without deleting its bundle and
  discards only session-owned, uncommitted source artwork.

The model owns the draft, stage states, accepted source, configuration snapshots,
revision, errors, and pending-work metadata. Panels render and report intents.
The containing editor starts provider/local work and commits through `Api`.
Neither model nor panels own native textures or provider SDK objects.

Controls are disabled while bounded local work owns their snapshots. A result
for a superseded revision is discarded. Every failure appears in dismissible
status text. Normal shutdown discards a session-owned import; recovery of
leftovers after a process crash remains Milestone 6. Remote cancellation is a
Milestone 5 provider responsibility.

After creation the blueprint appears in the Level Editor's searchable
thumbnail palette. To place a boulder behind ground or actors, put it in an
earlier world layer. Its
`sort_order` only orders it among entities in that same layer. This pipeline does
not infer a world layer.

### Attachment semantics

Implemented attachment modes are:

- **grounded:** bottom-center subject contact with contact-tolerance
  validation;
- **ceiling:** top-center subject contact with the same contact-tolerance
  validation and context placement;
- **free/background:** an explicit final-texture pixel anchor inside the canvas,
  with no inferred contact edge or contact-pixel validation.

Attachment mode belongs to the deterministic composition settings persisted by
`PropRecipe`; the free anchor is the tagged alternative's required payload.
Recipe schema 2 and pipeline version 2 introduced the contract. The migration
maps version-1 recipes to grounded, renames grounded tolerance to contact
tolerance, and preserves their composition and frame geometry. Attachment is
not collider intent and does not infer a world layer. Changing it invalidates
downstream previews and regeneration snapshots like every other recipe input.

## 11. Verification boundaries

Tests should pin behavior where it is platform-neutral:

- palette extraction returns the same terrain colours and leaves existing
  generated-terrain pixels unchanged;
- isolation handles meaningful alpha, border-connected backgrounds, uncertain
  multiple subjects, edge contact, and empty images;
- premultiplied resampling produces no transparent RGB fringe, pixel expansion
  is integer, quantization is deterministic, alpha is binary, and validation
  rejects every stated invariant;
- pipeline invalidation discards downstream artifacts and stale worker results;
- review-each-step and finished-only produce byte-identical final images;
- a fake provider covers capabilities, candidates, timeout, cancellation,
  malformed bytes, and sanitized failures without network access;
- source and prop recipe readers are strict, migrations are idempotent, and the
  shipped-assets test loads every new definition;
- bundle tests inject failure after each create/update/delete and verify either
  a complete product or an accurately reported partial-compensation failure;
- regeneration preserves IDs, refuses stale sprite/style/source snapshots, and
  does not overwrite blueprint collider edits;
- reference scans block source, style, and bundle deletion with useful names;
- an accepted candidate is retained with field-for-field generated provenance,
  and one the model refuses is removed again rather than left as an orphan;
- focused UI tests cover stage navigation, final-only behavior, pending-state
  controls, candidate choice, and errors without an SDL window.

Provider adapters may have opt-in integration tests gated by credentials. They
are never part of the ordinary test suite or required to build the editor.

## 12. Implementation sequence

Each milestone leaves a useful, tested boundary and avoids committing the UI to
unfinished provider behavior.

0. **Visual feasibility spike (accepted).** Build
   only the shared palette extraction,
   deterministic transforms, and `scripts/prop_artwork_spike.cc`. Feed it one
   externally generated boulder plus a real terrain recipe such as
   `lucinda_cave`. Emit an in-context comparison of the full palette, semantic
   subset, and deterministic-ramp policies beside the terrain scene produced by
   the existing renderer. The go/no-go question is whether at least one result
   looks like production-quality Zebes art beside that terrain. Passing technical
   invariants is necessary to inspect the result but is not the success
   criterion. If the answer is no, iterate on transforms and palette policy
   without building resources, lifecycle, UI, or provider integration.
1. **Shared palette and image primitives (implemented).** Keep the accepted
   spike code: extract the terrain palette without changing terrain output; add
   safe in-memory decode and content digests. The palette and digests landed
   here; in-memory decode was recorded as done but was not, and arrived with
   Milestone 5 as `DecodeImage` when the adapter needed it.
2. **Deterministic artwork library (foundation implemented; diagnostics still expand).** Implement typed stages, diagnostics,
   final validation, and focused fixtures beyond the spike's minimum path. No
   editor or provider dependency.
3. **Authoring resources and lifecycle (implemented).** `SourceArtwork`,
   `PropRecipe`, strict initial schemas, managers, asset references, editor
   ownership, ID-backed source/output paths, deterministic prepared output, and
   compensated bundle creation, regeneration, and bundle deletion are
   implemented. No migration is needed until an initial schema has shipped and
   later changes.
4. **Editor workflow from imported sources (implemented).** The platform-neutral
   model, per-stage and final-only preview policies, real-terrain in-context
   preview, background import/processing, finished bundle creation,
   regeneration, Save As, and reference-safe deletion prove the entire durable
   pipeline without a network dependency. Shared `Canvas` navigation gives
   previews rulers, pan, zoom, and Fit, and context bounds retain the complete
   texture. Context props can be dragged for preview-only placement using the
   same pointer-offset gesture controller as Level Editor entities.
4a. **Attachment modes (implemented).** Grounded and ceiling contacts, explicit
   free/background anchors, strict persistence and migration, matching context
   previews, sprite offsets, regeneration, and editor controls share one
   deterministic contract.
4b. **Developer feedback loop (implemented).** The first implementation slice
   batches the complete reverse-dependency set into one CMake build and runs
   each selected binary exactly once. Successful configure, build, test, and
   scoped-lint output is concise; a failure prints the complete captured output
   and preserves the failing status. Scoped clang-tidy processes at most two
   translation units concurrently. Focused script tests cover selection
   handoff, one-build/one-run behavior, bounded lint concurrency, and configure,
   build, test, and lint failures.

   Measurements on the same warm x86_64 macOS checkout, using CMake 4.4.0, GNU
   Make 3.81, and Apple clang 17, are:

   | Case | Before | Batched/two-worker | Change |
   |---|---:|---:|---:|
   | warm `--affected-target prop_artwork`, 28 executables | 87.59s | 73.24s | -16% |
   | same sweep after touching `prop_artwork_pipeline.cc` | 97.83s | 97.06s | -1% |
   | strict clang-tidy, same 18 translation units | 84s | 51.36s | -39% |

   The earlier 5m44 affected sweep remains a real Milestone 4a observation, but
   it was not a controlled warm comparison. The new source-touch result showed
   that compilation/linking, rather than shell orchestration, dominated once a
   common source changed; even the warm batched build spent 65s inside GNU Make.

   An isolated Ninja 1.13 benchmark selected the same 28 executables. After
   adopting it, the real `scripts/test.sh --affected-target prop_artwork`
   command took 6.32s warm and 22.23s for the same source-touch case: reductions
   of 91% and 77% from the batched Makefile results. Unbounded Ninja parallelism
   caused several five-second GoogleTest discovery timeouts during link-heavy
   builds, while two workers completed reliably and were only about one second
   slower than four in the isolated source-touch comparison. Focused work
   therefore retains two jobs in the `dev` and `ui` build presets.

   The comprehensive wrapper now uses separate `dev-full` and `ui-full` build
   presets with eight workers. A global two-worker Ninja link pool also bounds
   each test binary's post-link GoogleTest discovery, preserving the constraint
   established above without throttling independent compilation. A clean
   UI-enabled build completed all 1,081 actions in 5m49.7s on the reference
   16-core macOS machine, averaging 699% CPU, without a discovery timeout.
   Release builds retain native parallelism because they have no test discovery
   hooks.

   The full gate also derives its test manifest from CTest and invokes each
   registered executable once. On the same checkout, the 96 headless
   executables ran in about 12s, and a warm complete UI-enabled wrapper run took
   about 37s including configure, build checks, 99 C++ executables, and 79
   Python tests. The prior case-by-case CTest C++ path alone took 62–75s to
   launch 1,003 test processes. Case filters and focused work retain CTest's
   case-level behavior.

   The remaining optimizations were measured separately and rejected for the
   local default. Apple ld's debug-speed flags (`-O0`, `-no_deduplicate`, and
   `-random_uuid`) made the source-touch cycle slightly slower at 23.41s and did
   not reduce the 576 MB test-binary footprint. For the real prop-pipeline
   compile, ccache reduced a 2.30s miss to a 0.03s hit; unchanged, touched, and
   edit-then-reverted content all hit, and the two cached objects used 1.5 MB.
   That can remove only about 2.27s, or 10%, from the 22.23s source-touch loop
   because linking remains dominant. CI retains its existing compiler cache,
   while local presets explicitly disable SDL's implicit ccache discovery so a
   machine with ccache installed does not silently get a partial cache policy.
5. **Generation service and provider adapters (implemented; live checks remain).**
   The first slice adds
   provider-neutral generation specifications, capabilities, candidates, and
   stable provenance; an environment-backed move-only credential boundary; and
   bounded HTTPS request/response types. Generation and transport handles poll
   without blocking, cancel unfinished operations on destruction, validate
   input before invoking an adapter, and reject malformed completion values.
   Focused fakes cover capability refusal, pending/completed/cancelled lifetime,
   missing credentials, HTTPS/time limits, and malformed provider results.

   The libcurl transport is implemented with asynchronous DNS, verified HTTPS,
   redirects disabled, bounded response headers and body, and immediate
   cancellation without a worker join.

   The polling owner and the first adapter are now implemented too.
   `ImageGenerationEngine` is a session-lifetime `Engine` that owns in-flight
   requests, takes specs and cancellations over a notifying MPSC queue, and
   returns one event per submitted request over a second queue the editor
   drains without blocking. Delivery is infallible by construction: `Submit`
   reserves one of eight outstanding slots before queueing and `NextEvent`
   releases it, so the event queue cannot overflow and the engine needs no
   retry-delivery path. Over the bound, `Submit` refuses with
   `ResourceExhausted` rather than queueing behind it. A rejected spec or a
   failed `Start` becomes that request's event rather than a `Run` failure,
   because returning it from `Run` would end the runner and take every other
   in-flight request with it.

   `OpenAiImageClient` targets `gpt-image-2` through a typed
   `OpenAiImageConfig`. It loads its credential per request, so a missing key
   is one request's `Unauthenticated` rather than a startup failure, and sends
   it only as a sensitive header. Provider status codes map to distinct
   statuses — 400 invalid, 401 unauthenticated, 403 permission denied, 404 not
   found, 429 resource exhausted, 5xx unavailable — and malformed bodies,
   undecodable base64, and empty candidate sets are `DataLoss` and `NotFound`.
   Common image I/O gained `DecodeImage`, which reads dimensions from the
   header before decoding so an oversized image is refused without allocating
   its surface; milestone 1 had recorded this as implemented when it was not.

   The editor flow is implemented too. `ImageGenerationService` assembles the
   transport, credential source, adapter, engine, runner, and thread in the one
   order that keeps each alive for its borrower, and `EditorUi` owns one for
   the process; `PropArtworkEditor` receives only the engine. The input column
   gained a prompt, a candidate count bounded by the adapter's reported
   ceiling, Generate, Cancel, candidate navigation, Accept, and Discard. At
   most one generation is in flight, because a second would produce two
   candidate sets with no way to say which the preview is showing.

   Accepting a candidate retains it through `Api::CreateSourceArtwork` with
   `GeneratedArtworkProvenance` and only then points the model at it, which is
   the same order an imported PNG takes; a retention that fails anywhere
   removes the source it created. Cancelling does not retire the request: the
   engine still owes exactly one event, so the id stays pending until it
   arrives, and dropping it early would let that event be mistaken for a later
   request's.

   Next: the credential-gated OpenAI integration test, which is the only thing
   that will exercise the curl negative-timeout branch, plus the final Codex
   editor accept/discard/cancel/shutdown walk recorded in
   `codex-image-generation.md`.
6. **Operational hardening.** Exercise shutdown, retries that are safe to
   retry, provider error UX, crash leftovers in staging, and the complete editor
   walk before considering another provider or atlas packing.

Milestone 0's implementation surface is intentionally narrow and uses existing
repository boundaries:

```text
src/terrain/terrain_palette.{h,cc}

src/artwork/
  isolate_subject.{h,cc}
  compose_prop.{h,cc}
  rasterize_prop.{h,cc}
  quantize_prop.{h,cc}
  edge_treatment.{h,cc}
  cleanup_prop.{h,cc}
  prop_artwork_pipeline.{h,cc}

src/common/image_digest.{h,cc}

scripts/prop_artwork_spike.cc

tests/terrain/terrain_palette_test.cc
tests/artwork/prop_artwork_pipeline_test.cc
```

The executable accepts an imported image and either an existing terrain recipe
or an explicit built-in preset. It writes comparison images outside the authored
asset tree. `scripts/` is the established home for repository command-line
tools; adding a new top-level `tools/` tree would create a second convention for
the same responsibility.

The initial boulder run compared all three candidate policies. After accepting
the full palette, the spike converged on the production coordinator and now
writes every intermediate stage plus native and nearest-neighbor 4x context
views. A second 3x3 tree run against `Cozy Meadow` increased confidence and
exposed enclosed backdrop pockets inside the canopy. Isolation now clears only
enclosed pixels that closely match the estimated background while retaining the
broader border-connected rule for subject safety.

## 13. Deliberately out of scope

- **Automatic colliders.** Alpha is artwork, not collision intent. Author them
  in the Blueprint Editor.
- **Automatic layer or sort assignment.** Depth is a level-authoring decision.
- **Animation.** Frame consistency, shared quantization, and packing need the
  separate frame-set workflow in
  [`animation-artwork-pipeline.md`](animation-artwork-pipeline.md), not repeated
  static-prop runs.
- **Parallax backgrounds.** They use whole textures with scroll, repeat, and
  zone semantics, not anchored world entities. They may reuse image transforms
  later but need a different output builder.
- **A generic workflow graph.** The useful reusable boundary is a sequence of
  typed image stages and output builders. Arbitrary graphs, plugin discovery,
  and serialized node parameters would add a framework before a second workflow
  requires one.
- **Shared prop atlases.** Measure runtime need first; if required, design stable
  slot ownership and explicit compaction without changing sprite IDs.
- **Silent AI regeneration.** A remote call always creates a new source
  candidate. It never masquerades as deterministic recipe regeneration.
