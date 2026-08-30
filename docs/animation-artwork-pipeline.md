# Animation artwork pipeline plan

**Status: active next work. The Milestone 3 live animation gate was accepted on
2026-08-29; this pipeline is required before the Milestone 4 runtime thread
split.**

## Goal and sequencing

Build a repeatable authoring path that can turn imported or remotely generated
frame-set source artwork into validated Zebes Texture, Sprite, and Blueprint
state bindings. The output must be reviewable, regenerable from retained source,
and safe to commit without leaving a partial asset graph.

M3 established stable semantic Blueprint state keys, a game-owned player
state-selection policy, explicit loop-or-hold Sprite playback, and visible
playback using a deliberately small asset in the real Texture/Sprite format.
That separates engine correctness from image-generation quality. The accepted
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
not gameplay identifiers. M3 added serialized, stable state keys and migrated
every shipped Blueprint definition. The game layer owns the meaning of
`idle-*`, `run-*`, and `airborne-*`; engine playback resolves the boot-checked
state handle and changes the selected state. Migration and loader tests cover
every shipped definition.

The player state policy should be deterministic from fixed-tick runtime data.
For the first gate, horizontal speed and grounded/airborne state are sufficient.
State changes reset playback only when the semantic key changes. M3 settled on
separate left- and right-facing Sprite bindings because runtime mirroring is not
part of the renderer contract. Prove one direction during feasibility, then
produce or derive the paired direction through the same deterministic pipeline;
do not expand the renderer merely to make the feasibility spike pass.

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

## Bounded generation-feasibility gate

Do this before adding managers, editor panels, persistence APIs, or reference
support to another provider adapter. The gate answers two separate questions:

1. Can a small deterministic processor turn a coherent source sheet into a
   registered, palette-stable native clip and an honest review bundle?
2. Can one reference-conditioned generation request produce a recognizable
   character following a complete pose sequence closely enough to survive that
   processor without per-frame repair?

The first question must pass with an imported or deliberately simple manually
authored baseline even if generation fails. A provider failure must not be
misdiagnosed as a pipeline failure, and a good processor must not be used to
hide a bad generated performance.

### Fixed experiment contract

Lock this contract before the first quality-bearing remote request. The local
manual/synthetic dry run may expose a contradiction and change it once; after
generation starts, changing geometry, pose order, palette, or acceptance rules
starts a new explicitly identified run rather than moving the goalposts.

| Concern | Gate value |
|---|---|
| Character | One small, original, license-clean player design. Existing Samus proof pixels are not a generation reference or production character. |
| View | Right-facing orthographic side view only. Left-facing output remains a later deterministic or separately generated production concern. |
| Native frame | 48 × 48 RGBA pixels, rendered at 2× in the current world scale. |
| Registration | Entity origin `(24, 44)` in native pixels; grounded contact line `y = 44`; the existing 32 × 64 world-unit collider is unchanged. |
| Idle | Four frames in a 2 × 2 coherent sheet, 15 fixed ticks per frame, looping. |
| Locomotion | Ten frames in a 5 × 2 coherent sheet, 4 fixed ticks per frame, looping. The pose progression and 40-tick cadence follow the shipped right-running Samus example, while the generated character and pixels remain original. |
| Palette | One gate-local character palette snapshot, at most 12 opaque colours plus transparent. It is chosen once from the accepted reference and checked against the Catacombs background; it is not inferred independently per frame. |
| Source bounds | RGBA PNG, at most 2048 pixels on either axis, 16 million decoded pixels, and 64 MiB encoded. Cells are equal, explicit, and unlabelled. |
| Allowed transforms | Exact cell extraction, declared matte isolation, one shared crop/scale, fixed-origin placement, one shared palette quantization, binary-alpha cleanup, and deterministic packing. |
| Forbidden repair | Per-frame rescaling, warping, repainting, limb or costume repair, pose reordering, or a different registration rule for whichever frame looks wrong. |

Samus is motion and cadence evidence only: the shipped right-facing run supplies
the ten-frame, five-phase-per-side rhythm and 4-tick timing, not pixels,
costume, palette, or character identity for generation.

The 48-pixel canvas is a feasibility value, not a new engine constraint. It is
large enough to cover the current proof's roughly 44-pixel native height with
registration padding, while keeping the gate cheap. The accepted gate records
whether this value should become the first recipe default.

The processing canvas is 48 × 48, with the grounded contact boundary at
`y = 44`; rows 44–47 are transparent guard rows. The packed clip deliberately
omits those guard rows: every Sprite frame uses the same 48 × 44 source
rectangle, `render_w = 96`, `render_h = 88`, and offsets `(-48, -88)`. Thus its
render bounds end at the entity origin, while any non-transparent pixel in rows
44–47 is a hard validation failure.

### Reference and pose strategy

A profile alone helps identity but does not specify a gait, and a wireframe
helps pose but does not specify costume, proportions, or palette. Use both in a
single locked conditioning kit:

1. **Identity board.** Create or import one turnaround with front, right-side,
   and back views, a dominant right-profile silhouette, a proportion grid,
   fixed colour swatches, and two or three unmistakable design landmarks. Keep
   it orthographic and unposed; a face close-up has little value at native size.
2. **Pose guides.** Author simple joint-and-torso guides on the exact 2 × 2 and
   5 × 2 cell layouts. Mark the shared origin and contact line, planted feet,
   hip height, facing direction, and frame order. These are key-pose constraints,
   not claimed automatic in-betweens. For the right-facing locomotion guide, use
   this ten-pose sequence:

   1. Right-foot contact/strike; left leg trails.
   2. Right compression/down pose; rear heel lifts.
   3. Right passing pose; the legs pass beneath the hips.
   4. Right drive/up pose; the rear leg extends as the forward leg swings.
   5. Right pre-contact reach; the forward foot approaches the contact line.
   6. Left-foot contact/strike; the alternating phase begins.
   7. Left compression/down pose; the opposite rear heel lifts.
   8. Left passing pose; the legs pass beneath the hips.
   9. Left drive/up pose; the rear leg extends as the forward leg swings.
   10. Left pre-contact reach; the forward foot approaches the contact line and
       closes the loop into frame 0.
3. **Conditioning boards.** Compose the identity board and the clip-specific
   pose grid into one image per clip. At the time of this 2026-08-29 run,
   `ImageGenerationSpec` permitted one provider-neutral `reference_image`;
   composing the evidence kept the experiment within that then-current
   boundary. `OpenAiImageClient` supported the field and `CodexImageClient` did
   not. Later ordered-reference infrastructure belongs to the separately
   reviewed pose-conditioned experiment and does not alter this run's evidence.
4. **Coherent request.** Request the entire idle or locomotion sheet in one
   operation, with one flat contrasting matte and no labels, borders, scenery,
   effects, or extra views. Never generate or revise individual frames.

Generate at clean high resolution and let the deterministic processor create
the native pixel treatment. Asking the model to solve exact low-resolution
pixel placement and temporal coherence at the same time makes the experiment
harder to interpret.

### Generation budget and controls

- If no suitable original identity board exists, allow one preliminary request
  to create it, then accept and lock that board before clip generation.
- Request one candidate at a time. Allow at most two coherent-sheet attempts
  for idle and two for locomotion: four quality-bearing clip requests and five
  total requests including optional identity-board creation.
- Attempt two may change one recorded hypothesis in the prompt or conditioning
  board. It may not splice, redraw, or independently regenerate a failed frame.
- Use one provider/model/version for both clips when its capabilities allow it,
  and record submitted and revised prompts, reference and output RGBA digests,
  dimensions, and timestamps. An exact retry after a transport failure does
  not become a visual candidate.
- Prepare one imported/manual baseline for each clip before remote generation.
  The baseline may be graphically simple: it controls the processor, timing,
  and runtime route rather than competing as final art.
- Keep every input and result outside `assets/` during the gate. A timestamped
  `build/animation-feasibility/<run-id>/` directory is the working run; selected
  research inputs may be copied to `notes/` after review, but the normative
  contract and conclusion stay in this document.

### Disposable processor implementation

Build only the narrow platform-neutral seam needed to make the comparison
repeatable:

- `src/artwork/animation_artwork_feasibility.{h,cc}` owns typed in-memory gate
  contracts, cell extraction, shared transforms, packing, and diagnostics over
  copied `RgbaImage` values. It has no filesystem, SDL, API, resource-manager,
  generation-client, or JSON dependency. The deliberately explicit
  `Feasibility` name prevents this experiment from silently becoming the
  production recipe contract.
- `scripts/animation_artwork_spike.cc` owns flags, bounded PNG reads and writes,
  fixed idle/run presets, the timestamped output directory, provenance input,
  and the review manifest. It refuses an existing output directory rather than
  mixing runs.
- `tests/artwork/animation_artwork_feasibility_test.cc` uses small synthetic
  sheets to cover exact layout and count, dimension and byte-safe arithmetic,
  clipped and empty cells, shared rather than per-frame scale, origin/contact
  placement, palette reuse, deterministic packing, and byte-stable diagnostics.
  No remote generation belongs in this test.

The spike hard-rejects malformed RGBA storage, unexpected dimensions or cell
count, ambiguous matte removal, empty or border-clipped foreground, a shared
transform that cannot fit the fixed canvas, and planted poses that miss the
contact line. Bounds, occupied pixels, palette use, silhouette height, anchor
translation, adjacent-frame change, and last-to-first change are reported for
review. Do not invent content thresholds after seeing the candidates: exact
duplicates and structural violations may fail automatically, while identity,
anatomy, pose quality, foot slide, and loop continuity remain visible review
judgements.

For every candidate publish:

- original sheet and exact extracted cells;
- isolated pre-registration cells so shared processing cannot hide drift;
- every native frame and an 8× nearest-neighbour contact sheet;
- origin, contact-line, foreground-bounds, and clipping overlays;
- adjacent-frame and last-to-first difference images with descriptive metrics;
- the packed clip texture and complete ordered `SpriteFrame` preview metadata;
  and
- a manifest containing the locked contract, provenance, digests, diagnostics,
  and hard validation results.

Run the same accepted input twice into distinct empty directories and compare
each manifest's `deterministic_payload` subtree and image digests. Timestamps
and run IDs remain operational fields outside that subtree rather than being
normalized after writing.

### Review and live proof

Review manual and generated candidates under anonymous run labels before
looking at provider or attempt names. A generated clip receives no partial
credit: all of these observations must pass without forbidden repair.

- The head, torso, costume landmarks, palette roles, limb count, and apparent
  proportions read as the same character in every frame and across both clips.
- Poses follow the declared order and facing; no frame is a near-duplicate used
  to evade a difficult phase.
- Camera, silhouette scale, and origin are stable; planted feet stay on the
  contact line without visible sliding, and intended lifted feet remain clear.
- Nothing is clipped, detached, or added, and transparent/matte cleanup does
  not erase enclosed character detail.
- Last-to-first playback reads as a continuation rather than a snap or held
  duplicate. Difference images are evidence, not a substitute for playback.
- Native, 2×, 4×, and Catacombs views keep the player readable against terrain
  without changing collision behavior.

For the live proof, stage the packed Texture, Sprite definition, and one
`idle-right` or `run-right` binding at a time in a disposable copied asset root
under `build/`. Reuse the existing Player Animation Proof Blueprint and collider
only in that copy. The staging tool must assert that collider ID, placement
mode, unrelated state bindings, and the checked-in `assets/` tree are unchanged.
Exercise idle, run, direction changes through the existing proof states, landing,
and collision in the running Catacombs path even though only the right-facing
candidate is under review.

### Decision and stop rules

| Evidence | Conclusion and next action |
|---|---|
| Either manual baseline fails structural processing or live playback | The experiment is invalid. Fix the gate contract or spike before spending generation attempts. |
| Both manual baselines pass; generated locomotion fails after its two attempts | Coherent generated motion is not feasible with this technique. Continue the production pipeline with imported/manual sources and stop provider-specific animation work. |
| Generated locomotion passes, but idle or cross-clip identity fails | Record single-clip feasibility only. Continue the import/manual pipeline; do not graduate generation to the production workflow yet. |
| Both generated clips and both baselines pass deterministic, blinded, and live review | Accept reference-conditioned coherent-sheet generation as a feasible optional source path, record the winning contract and digests here, and begin pure production frame-set processing. |

### Gate run record: 2026-08-29

The disposable implementation and bounded image requests have now exercised
this contract. Evidence remains under the ignored
`build/animation-feasibility/` tree; these digests make the result identifiable
without turning disposable images into checked-in production assets.

| Input | Result | Source RGBA digest | Packed RGBA digest |
|---|---|---|---|
| Manual ten-frame locomotion | Structural pass; independent `deterministic_payload` rerun matched exactly. | `f2fef6e3ae458fed09df764c621515b04fc073aee290e6c2600ff8cad799127a` | `4e1eb46bcc0783e49471559b803f4396b3d90aa047064985ff472fdcb49c9964` |
| Manual four-frame idle | Structural pass; independent `deterministic_payload` rerun matched exactly. | `3ec2b6319992b8a58f9c45edf3b288106e341ad91d83f8b3d1ba1ef028411e95` | `09b8fa3b55656dda715b4507b226e43b8050c1cfe5ab947bbc22d51e820f1c55` |
| Generated locomotion attempt 2 | Automated structural pass and independent deterministic rerun match, but live Catacombs playback is a hard failure. The frames do not form fluid motion or read as a person running; the coherent-sheet request did not follow the pose guide closely enough. | `3d6b494788f5fc99fd4eb192df6d76046f4de1ce56369c09786da8b705aa24e3` | `14866473aceb6759d47842003a06c935af0750fef504cdb56c1f357c6463d947` |
| Generated idle attempt 2 | Automated pass but visible-review failure: column-dependent registration produces an approximately three-native-pixel left/right oscillation, and adjacent changes cover roughly the whole character rather than a subtle breathing motion. | `ced67b64d4b9b14c3eb674b13278edbbef6ab37ea12bc5234497d38ae4632edd` | `5fe1c59237d1dc6c6e3bba5364b1db47f4e705d6a066d0efe4ec339e854eceec` |

The request count reached the locked maximum: one identity-board request, two
locomotion requests, and two idle requests. Locomotion attempt 1 and idle
attempt 1 had no valid equal-square-cell extraction and failed before
processing. Attempt 2 used canvas-matched pose boards; locomotion then admitted
one shared `256 × 256` extraction with a 28-pixel row gap, while idle required a
shared `400 × 400` extraction but failed visible registration and motion review.
No frame was regenerated or repaired independently.

Live review on 2026-08-29 overturned the still-frame impression of locomotion
attempt 2. Structural checks, per-frame inspection, and difference images did
not establish temporal coherence: at speed the sequence is visibly discontinuous
and does not communicate a run. This is the decisive gate observation, not a
polish issue and not something the deterministic processor may repair.

The full generated-source gate is a hard fail. Coherent whole-sheet generation
is not a feasible animation source with this technique. Keep imported/manual
sheets as the production path, do not add provider-specific animation transport
to this locked run, and do not spend further requests on it.

The separately budgeted
[pose-conditioned experiment](history/animation-pose-conditioned-experiment.md)
also failed. One composite identity board and three separated identity views
both produced dimensionally different characters, and the opposing guides did
not produce unambiguous pose phases. That complete experiment is deprecated.
No parametric-guide, sequential-conditioning, generated-sheet, independent-frame
batch, or additional provider call is active roadmap work.

Remote provider success is not a prerequisite for animation authoring. The
production path is imported or manually authored sheets through source-neutral
processing, review, recipe, and transactional persistence. Generated animation
research cannot be used to delay or redefine that path.

### Historical generation-gate order and verification

1. Lock the original identity board, character palette, idle guide, locomotion
   guide, exact prompts, and gate constants.
2. Add the pure feasibility processor, spike CLI, synthetic tests, and artifact
   manifest. Run both manual baselines and settle the contract once.
3. Run the bounded coherent-sheet attempts and process each without tuning the
   processor to a candidate.
4. Complete anonymous artifact review, deterministic rerun, and disposable
   Catacombs playback.
5. Record the result matrix, accepted digests, actual generation count, and any
   contract amendment in this section. Delete or explicitly promote the
   feasibility code when milestone 3 begins; do not leave two processors.

During implementation, format the edited C++ files and run the focused
`animation_artwork_feasibility_test`, then the complete affected artwork test
executable and `git diff --check`. The gate does not justify the broad suite,
resource migration checks, or UI tests because it changes no serialized or SDL
contract.

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

1. **M3 semantic/live proof — complete.** Stable Blueprint state keys,
   migration, the player policy, explicit playback modes, and a minimal
   real-format multi-frame proof passed their automated and live Catacombs
   gates on 2026-08-29.
2. **Generation feasibility — complete and closed.** Manual/imported sheets
   passed deterministic processing and live staging. Coherent generation and
   pose-conditioned independent frames failed their visual gates; generated
   animation is deprecated.
3. **Pure frame-set processing — next.** Promote the accepted source-neutral
   extraction, registration, shared palette, validation, and deterministic
   packing behavior with focused platform-neutral tests.
4. **Recipe and bundle lifecycle.** Add the strict recipe manager, catalog
   references, pure preparation, transactional create/regenerate/delete APIs,
   migrations, and failure-compensation tests.
5. **Headless curation.** Publish frame, alignment, loop, and focused in-level
   evidence; require byte-stable re-review after commit.
6. **Editor import flow.** Reuse retained-source lifecycle controls and add
   animation-specific imported-sheet, clip, timing, origin, and state-binding
   controls. Do not add remote animation generation.
7. **Production player set.** Import or manually author, process, review,
   persist, and regenerate the initial player state set, then complete the live
   route gate.

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
