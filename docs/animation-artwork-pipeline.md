# Animation artwork pipeline plan

**Status: planned as the required follow-on to the Milestone 3 live animation
gate and before the Milestone 4 runtime thread split.**

## Goal and sequencing

Build a repeatable authoring path that can turn imported or remotely generated
frame-set source artwork into validated Zebes Texture, Sprite, and Blueprint
state bindings. The output must be reviewable, regenerable from retained source,
and safe to commit without leaving a partial asset graph.

This work follows the M3 live proof directly. M3 should first establish stable
semantic Blueprint state keys, a game-owned player state-selection policy, and
visible playback using a deliberately small asset in the real Texture/Sprite
format. That separates engine correctness from image-generation quality. The
proof is not the production animation workflow, and M4 does not begin until the
pipeline feasibility and first production clip gates below pass.

Animation is not an extension that runs the static Prop pipeline once per
frame. Independent image requests do not preserve identity, proportions,
camera, palette, contact point, or loop continuity. A generated candidate is a
coherent frame set produced in one request or from one retained source sheet;
all frames then pass through one shared deterministic processing run.

## Scope

The first pipeline supports short, looping 2D entity clips such as player idle
and run. Imported and manually drawn sheets are first-class inputs and use the
same processing, review, recipe, and commit path as generated sheets. Remote
generation is an optional source provider, not an invariant of the pipeline.

The first version deliberately excludes:

- independent per-frame generation;
- automatic in-betweening, skeletal animation, or video extraction;
- inferring collision geometry from alpha;
- automatically changing a Blueprint state's collider or placement mode;
- a generic workflow graph; and
- shared cross-entity atlas packing.

One Texture per clip is the initial packaging boundary. Each Texture contains a
deterministic strip or grid for one Sprite. A Blueprint may reference several
such Sprites through its semantic states. This costs a small number of texture
switches but avoids shared-atlas slot ownership, compaction, and unrelated clip
churn before runtime measurements justify those systems.

## Contracts to establish first

### Stable runtime semantics

Blueprint state display names and vector indices are authoring presentation,
not gameplay identifiers. Add a serialized, stable state key and migrate every
shipped Blueprint definition. The game layer owns the meaning of keys such as
`idle`, `run`, and `airborne`; engine playback only resolves a key and changes
the selected state. Migration and loader tests must cover every shipped
definition.

The player state policy should be deterministic from fixed-tick runtime data.
For the first gate, horizontal speed and grounded/airborne state are sufficient.
State changes reset playback only when the semantic key changes. Facing policy
must also be explicit: prefer runtime horizontal mirroring if the renderer can
support it cleanly; otherwise left- and right-facing clips are separate recipe
outputs. Do not ask generation to make both directions until this choice is
settled.

### Frame-set geometry

Every clip declares:

- a fixed logical canvas and frame count;
- an entity origin and grounded/contact line shared by every frame;
- ordered frame durations in fixed simulation ticks;
- source-cell layout or explicit source rectangles;
- intended playback behavior (`loop` or `hold-last`); and
- limits for source dimensions, output dimensions, pixels, and bytes.

Processing may translate isolated subjects inside their fixed frame canvases to
align an authored origin. It must not resize each pose independently to make
bounding boxes match; that creates visible scale breathing. Sprite frame offsets
remain relative to the same entity origin, while colliders remain authored
Blueprint behavior independent of visible pixels.

### Retained source and recipe

Reuse `SourceArtwork` and the provider-neutral generation service for lossless
source retention and provenance. Extend the source contract only if a coherent
frame set cannot be represented safely as one retained sheet; do not introduce
parallel provenance or credential handling.

Add a strict, versioned `AnimationArtworkRecipe` only after the feasibility gate
settles the source shape. The expected recipe records:

- retained source-artwork IDs and their accepted digests;
- the target Blueprint ID and stable state-key bindings;
- per-clip cell layout, frame count, timing, canvas, origin, and playback mode;
- shared isolation, scale, palette, alpha, and cleanup settings;
- produced Texture and Sprite IDs plus final pixel and definition digests; and
- the deterministic pipeline implementation version.

The recipe may update only the Sprite binding for an explicitly named Blueprint
state. Collider ID, placement mode, unrelated states, and authored level
placements remain outside its ownership.

## Feasibility gate

Do this before adding managers, editor panels, or persistence APIs.

1. Define one small player reference, one idle loop, and one locomotion loop
   with fixed canvas, origin, pose brief, and frame-count limits.
2. Produce bounded candidates outside the asset root. Compare a coherent-sheet
   generated candidate with an imported or manually corrected baseline; do not
   issue separate requests for individual frames.
3. Run a disposable platform-neutral processor that extracts cells and
   publishes native frames, an enlarged contact sheet, loop order, origin and
   contact-line overlays, and adjacent-frame difference evidence.
4. Review identity, silhouette scale, palette, registration, clipping, foot
   slide, and loop closure. A processor can reject structural errors but must
   not silently repair a different costume, limb count, or pose.
5. Put the best processed candidate through the existing Sprite definition and
   running Catacombs playback path.

The gate passes when at least one short generated clip and the imported baseline
survive the same deterministic processing and produce acceptable live playback.
If generation repeatedly fails identity or temporal coherence, stop and retain
the imported/manual workflow as the production source path while reassessing
the generation technique. Provider success is not allowed to block the basic
animation authoring pipeline.

## Deterministic processing pipeline

After feasibility, implement the transforms under `src/artwork/` over copied
`RgbaImage` values. Like the Prop pipeline, stages are typed ordered operations,
not a generic graph, and have no API, filesystem, SDL, or resource-manager
access.

1. **Decode and validate.** Validate RGBA storage, dimensions, byte/pixel
   limits, expected sheet geometry, and exact frame count.
2. **Extract.** Slice only the declared cells or rectangles. Reject ambiguous
   gutters, missing cells, unexpected occupied cells, and clipped foreground.
3. **Isolate.** Prefer source alpha; otherwise remove only the declared or
   border-connected matte. Process all frames with the same policy and report
   per-frame confidence.
4. **Register.** Place each frame on the shared logical canvas against the
   authored origin/contact line. Report subject bounds and anchor drift; never
   infer or alter collision.
5. **Rasterize.** Apply one scale and pixel-block policy to the entire set in
   premultiplied-alpha space. Per-frame auto-fit is invalid.
6. **Quantize.** Use one resolved palette snapshot and deterministic tie-breaks
   for all frames so the clip does not shimmer from palette drift.
7. **Clean and validate.** Enforce binary alpha, clear RGB below transparent
   pixels, component and border rules, fixed canvas geometry, nonempty frames,
   stable origin, and configured drift bounds.
8. **Pack.** Emit a deterministic per-clip strip or grid and complete ordered
   `SpriteFrame` metadata. Re-running the same recipe and retained source must
   be byte-stable.

These are offline authoring costs. Runtime playback remains an index/cursor
update over active animated entities and does no generation, segmentation,
quantization, or whole-sheet processing.

## Review and persistence

Extend the existing headless curation boundary rather than introducing another
review executable. Animation evidence should include:

- every native frame and an enlarged contact sheet;
- the ordered animation strip already used by Sprite review;
- origin, contact-line, bounds, and clipping overlays;
- adjacent-frame comparisons, plus last-to-first closure evidence for looping
  clips and final-pose evidence for hold-last clips; and
- focused Catacombs playback at the supported zooms after persistence.

Preparation is pure and returns final pixels, Sprite definitions, Blueprint
binding changes, recipe data, diagnostics, and review artifacts. The API commit
then:

1. verifies retained source, target Blueprint, and output snapshots are still
   current;
2. preflights every ID, path, definition, and reference;
3. creates or updates Textures and Sprites before publishing the recipe and
   Blueprint bindings; and
4. compensates in reverse order on failure, reporting both primary and rollback
   failures.

Regeneration starts from retained source and preserves Texture, Sprite, recipe,
and Blueprint IDs. It updates only recipe-owned pixels, frame metadata, and
state Sprite bindings after optimistic snapshot checks. Deletion scans external
references and removes only a complete recipe-owned graph. Neither path
overwrites collider work or silently invokes a remote provider.

## Implementation milestones

1. **M3 semantic/live proof — implementation complete; live acceptance
   pending.** Stable Blueprint state keys, migration, the player policy, and a
   minimal real-format multi-frame proof are implemented. Accept the visible
   transitions in Catacombs.
2. **Generation feasibility.** Run the bounded generated-sheet/imported-sheet
   comparison and record the accepted source and frame geometry contract.
3. **Pure frame-set processing.** Implement extraction through deterministic
   packing with focused platform-neutral tests and a small command-line spike.
4. **Recipe and bundle lifecycle.** Add the strict recipe manager, catalog
   references, pure preparation, transactional create/regenerate/delete APIs,
   migrations, and failure-compensation tests.
5. **Headless curation.** Publish frame, alignment, loop, and focused in-level
   evidence; require byte-stable re-review after commit.
6. **Editor and provider flow.** Reuse the generation lifecycle controls and
   provider registry, add animation-specific prompt/reference/clip controls,
   and retain accepted candidates before processing or persistence.
7. **Production player set.** Generate or import, process, review, persist, and
   regenerate the initial player state set, then complete the live route gate.

Only after milestone 7 should the runtime plan proceed to M4. The finite cave
environment variation pass can continue independently when it does not modify
the player Blueprint or the same production level evidence.

## Verification boundaries

Platform-neutral tests must cover sheet bounds and frame count, common
registration and scale, shared palette quantization, alpha cleanup, deterministic
packing, timing metadata, recipe serialization, every shipped-definition
migration, stale-snapshot refusal, and compensation at every persistence step.
Provider adapters continue to be tested only at their existing request/event
boundary.

Before accepting the production pipeline, run the complete affected artwork,
resource, API, Sprite, Blueprint, curation, loaded-level, RuntimeWorld, player
simulation, and game-scene executables. The human gate is generate or import,
process, review, commit, byte-stable re-review, restart, and visibly exercise
idle, locomotion, direction, jump/fall, and landing transitions in Catacombs.
