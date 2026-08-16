# Generated prop artwork pipeline

**Status: Milestone 0 implemented and ready for visual acceptance.** The shared
terrain palette, deterministic spike transforms, command-line runner, and
focused tests now exist. The spike produces three boulder treatments beside
real `lucinda_cave` terrain. A human visual go/no-go remains deliberately open;
resource ownership, editor workflow, and provider integration have not started.

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

The remaining gaps are equally concrete:

- There is no network client, image-generation provider abstraction, credential
  boundary, cancellation contract, or remote-request timeout policy.
- The terrain palette is now a shared platform-neutral boundary, and Milestone
  0 provides the minimum in-process transforms needed for visual evaluation.
  Those spike stages still need broader fixtures, diagnostics, content limits,
  and a versioned pipeline coordinator before they are an editor-ready library.
- `assets/source_art/` is a directory convention, not a managed authoring
  resource. Nothing records ownership or generation provenance for an image in
  it.
- There is no prop recipe, pipeline coordinator, generated-prop bundle commit,
  regeneration path, or editor tab.

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

Literal terrain colours are the strongest available starting hypothesis, not a
settled visual rule. A prop restricted to those colours may fit perfectly, or
terrain's palette may be too narrow for a larger freestanding object. That has
to be decided from rendered comparisons before it becomes recipe infrastructure.

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

Milestone 0 uses those semantic results to compare three explicit prop palette
policies against the same source and transform settings:

1. every opaque colour in the resolved terrain palette;
2. a semantic subset, initially outline, substrate, wall, botanical, and accent
   roles;
3. deterministic additional tonal ramps derived from those semantic colours.

The third policy tests whether props need to share terrain's style-generation
rules rather than its literal final swatches. Any added ramp has a named,
versionable algorithm and deterministic parameters; it is not a hand-edited
escape palette. The spike chooses the production default. Experimental policies
that do not prove useful should be removed rather than carried forever.

The accepted prop palette plus the tile size and raster policy form the resolved
artwork-style snapshot. Downstream quantization consumes that snapshot and does
not care which experimental policy produced it.

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
  transparency preference, optional style/reference image

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

Credentials are supplied to the adapter by editor configuration or the process
environment. They are never serialized, logged, copied into pipeline or local
background-work inputs, shown in status text, or included in source provenance.
The transport owns the credential for the minimum request lifetime. Logs may
contain a local request correlation ID, provider status code, and sanitized
error message.

A remote request must have connect and total timeouts, response byte and image
dimension limits, cancellation, and shutdown behavior. `BackgroundTask` alone
is not this abstraction: destroying its `std::future` can wait for the worker,
and it intentionally has no cancellation. It remains the right tool for bounded
local transforms. The provider adapter needs a cancellable transport request,
or a transport operation with a strict timeout plus a non-blocking request
owner.

Invalid response bytes, unsupported formats, oversized images, empty candidate
sets, authentication failures, rate limits, and cancellation are distinct
errors. The adapter decodes accepted bytes to `RgbaImage` through common image
I/O; no provider object crosses into the pipeline.

Generation should ask for a single isolated subject, a simple or transparent
background, the selected view and light direction, and the target style. Those
instructions improve the input, but deterministic processing and validation
remain authoritative. A good prompt is not an invariant.

## 5. Managed source artwork

Add an editor-only `SourceArtwork` resource under
`assets/definitions/source_artworks/`; store its lossless image under
`assets/source_art/props/` using an ID-backed filename. It is not a `Texture`, is
never loaded by the renderer resource store, and is not shipped as runtime art.

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

The accepted source is retained even though only the final texture ships. That
is what makes reprocessing possible without paying for another remote request
or hoping a changed model returns the same boulder. Intermediate stage images
are a session cache and need not be persisted.

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

The author chooses a world anchor, defaulting to bottom center for a grounded
prop. The preview draws the tile grid, ground line, subject bounds, and anchor.
Layout settings include canvas size, padding, subject scale, and anchor offset,
so no crop or pivot decision is hidden in generated pixels.

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
- anchor inside the canvas and a grounded opaque pixel within the configured
  tolerance when the prop is marked grounded;
- 1:1 sprite source-to-render dimensions.

Validation is a stage so final-only mode still reports the exact failed
invariant instead of emitting a malformed asset.

## 7. Preview and execution semantics

Preview policy changes what the editor stops on and retains, not how pixels are
produced:

- **Review each step** runs one stage, shows its output and diagnostics, and
  waits for Continue. Editing a stage invalidates that stage and every output
  after it, while earlier accepted outputs remain valid.
- **Finished only** runs the same stage list in one bounded worker operation and
  retains only the accepted source and final artifact. On failure it opens the
  failing stage with its input, attempted output when available, and diagnostic.

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

In review-each-step mode the same run pauses after each transform in step 2.
The final pixels and committed bundle are otherwise identical.

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

1. `PreparePropAsset` runs with copied source pixels, style snapshot, and recipe
   settings. It returns validated final pixels and definitions with no `Api`,
   filesystem, SDL, or manager access.
2. `Api::CreateGeneratedProp` preflights names, references, paths, and all
   definitions, then creates source artwork when needed, texture, sprite,
   blueprint, and recipe in dependency order.
3. A failure rolls back in reverse order. Errors report both the primary failure
   and any failed compensation; they never claim success with a partial bundle.
4. The recipe becomes visible only after every output exists.

The current managers do not provide a general cross-file transaction. Do not
hide that behind a class named `Transaction`. Either add staging/commit support
with well-defined cache behavior or use explicit compensating operations as
terrain does and test every failure point. The same helper should be suitable
for generated terrain once proven, rather than becoming prop-only transaction
machinery.

Regeneration starts from the stored `SourceArtwork`, never from a new provider
call. It preserves texture, sprite, blueprint, and recipe IDs. It may replace
texture pixels and update the recipe-owned sprite frame after comparing the live
recipe, sprite, source digest, and prior pixel digest with the exact snapshots
used by the worker. A mismatch refuses the commit as stale.

The blueprint is an output binding but is not overwritten during regeneration:
the author may have added collider states in the Blueprint Editor. Regeneration
updates only recipe-owned artwork and frame geometry. **Generate new source** is
a separate operation that creates a new candidate and requires confirmation
before replacing the retained source; **Save As** creates a fresh bundle and
fresh IDs.

`Api::DeleteGeneratedProp` deletes the bundle as one operation. It first scans
the output members for external referrers: placed entities naming the blueprint,
other blueprints reusing the sprite, and other sprites reusing the texture. Any
such use blocks deletion and names the referrer. On success it removes recipe,
blueprint, sprite, and texture in dependency order. It removes the source only
when no other recipe references it; a shared source remains and does not block
deleting this prop.

## 10. Editor shape

Add a Prop Artwork tab with the established three-column layout:

- **Input:** import or generate, provider status, prompt, candidate selection,
  selected terrain recipe, and refresh/detach style actions.
- **Preview:** stage selector in review mode; checkerboard, tile grid, ground
  line, anchor, fit control, and final in-context view.
- **Output:** canvas and cleanup settings, review/finished-only policy, Create,
  Open, Save As, Regenerate, and bundle Delete.

The model owns the draft, stage states, accepted source, configuration snapshots,
revision, errors, and pending-work metadata. Panels render and report intents.
The containing editor starts provider/local work and commits through `Api`.
Neither model nor panels own native textures or provider SDK objects.

Controls are disabled only where mutation would invalidate pending ownership.
Remote generation can be cancelled. Bounded local work may finish and be
discarded by revision. Every failure appears in the dismissible status banner;
low-confidence isolation is a review state, not a log warning.

After creation the blueprint appears in the existing Level Editor palette. To
place a boulder behind ground or actors, put it in an earlier world layer. Its
`sort_order` only orders it among entities in that same layer. This pipeline does
not infer a world layer.

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
- focused UI tests cover stage navigation, final-only behavior, pending-state
  controls, candidate choice, and errors without an SDL window.

Provider adapters may have opt-in integration tests gated by credentials. They
are never part of the ordinary test suite or required to build the editor.

## 12. Implementation sequence

Each milestone leaves a useful, tested boundary and avoids committing the UI to
unfinished provider behavior.

0. **Visual feasibility spike (implemented; visual decision pending).** Build
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
1. **Shared palette and image primitives.** Keep the accepted spike code: extract
   the terrain palette without changing terrain output; add safe in-memory
   decode and content digests.
2. **Deterministic artwork library.** Implement typed stages, diagnostics,
   final validation, and focused fixtures beyond the spike's minimum path. No
   editor or provider dependency.
3. **Authoring resources and lifecycle.** Add `SourceArtwork`, `PropRecipe`,
   strict managers, migrations, asset references, prepared output, bundle
   commit/regeneration/deletion, and ID-backed generated paths.
4. **Editor workflow from imported sources.** Add the model, per-stage and
   final-only preview policies, in-context preview, background processing, and
   finished texture/sprite/blueprint creation. This proves the entire durable
   pipeline without a network dependency.
5. **Generation service and first adapter.** Add cancellable provider requests,
   credential/configuration plumbing, candidate processing, provenance, limits,
   and opt-in integration tests. Imported and generated sources converge at the
   same acceptance boundary.
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

scripts/prop_artwork_spike.cc

tests/terrain/terrain_palette_test.cc
tests/artwork/prop_artwork_pipeline_test.cc
```

The executable accepts an imported image and existing terrain recipe and writes
comparison images outside the authored asset tree. `scripts/` is the established
home for repository command-line tools; adding a new top-level `tools/` tree
would create a second convention for the same responsibility.

The implemented spike writes every intermediate stage, each finished palette
variant, a native in-context comparison, and a nearest-neighbor 4x comparison.
Its initial boulder run establishes that the pipeline mechanics and all three
palette policies work. The full-palette and derived-ramp variants retain more
rock-plane detail; the semantic subset is visibly flatter. This observation is
not the milestone decision: the author must still decide whether at least one
variant belongs in Zebes before Milestones 1-6 proceed unchanged.

## 13. Deliberately out of scope

- **Automatic colliders.** Alpha is artwork, not collision intent. Author them
  in the Blueprint Editor.
- **Automatic layer or sort assignment.** Depth is a level-authoring decision.
- **Animation.** Frame consistency, shared quantization, and packing need a
  frame-set design rather than repeated static-prop runs.
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
