# Pose-conditioned animation generation experiment

Status: production reference support and the disposable pilot/batch runner were
implemented and fake-provider verified on 2026-08-30. The exact input kit is
locked and the four-tick guide-only baseline passed deterministic processing
and live review. The complete provider-native two-frame pilot was rejected for
visible identity drift, so no candidate batch is authorized. Four provider
turns were attempted across the three separately authorized pilot runs.

This plan starts a new experiment after the coherent-sheet generation gate
recorded in [the animation artwork pipeline](animation-artwork-pipeline.md)
failed live playback. It does not revise that result. The new hypothesis is
that one generated image per pose, conditioned by both character-identity and
pose references, can improve pose obedience enough to form a readable run
cycle without unacceptable identity drift.

The supplied 12-pose run sheet is one animation unit. This experiment uses all
12 poses in their existing order. It must not create a ten-frame derivative by
dropping two frames; doing so changes the motion rather than merely changing
the sample count. The existing ten-frame Samus sequence remains an optional
control, not a source from which to alter the supplied cycle.

## Decisions

- Use the complete 12-frame, 6 x 2 pose sequence.
- Productionize ordered, semantically labelled image-generation references.
- Migrate every current single-reference consumer to the same ordered contract;
  headless redraw becomes the first `edit-source` caller.
- Exercise those references through the shared generation service and the
  headless authoring path, not through provider-specific experiment code.
- Run a two-frame, non-candidate plumbing pilot before spending the complete
  12-frame candidate batch.
- Keep 12-frame batch orchestration and animation feasibility processing
  explicitly experimental until the visual gate passes.
- Treat all 12 generated frames as one candidate. A partial batch is not an
  animation candidate, and failed individual frames are not selectively
  regenerated or repaired.
- Judge the result in live playback. Structural processing and attractive
  still frames cannot establish temporal coherence.

In this document, **headless runtime** means the headless authoring runtime
around `ImageGenerationService` and `generate_assets`. Remote generation does
not belong in the gameplay runtime under `src/game/`.

## Scope boundary

There are two implementation layers with different lifetimes:

1. **Production reference-image support.** The provider-neutral request
   contract, validation, provider adapters, provenance, and headless ingestion
   are reusable generation infrastructure and receive production-quality tests
   and failure handling.
2. **Disposable animation experiment.** A thin headless orchestrator submits
   the 12 requests, assembles their unmodified results, invokes the existing
   feasibility processor, and publishes review evidence. It does not introduce
   an animation editor, manager, serialized recipe, or `generate_assets`
   animation kind.

This split lets the experiment use the real production generation boundary
without promoting the current feasibility processor or batch workflow into the
asset model prematurely.

## Production reference-image contract

`ImageGenerationSpec` currently carries one unlabelled
`optional<RgbaImage> reference_image`, and capabilities expose one boolean.
Replace that shape with an ordered collection of Zebes-owned values:

```cpp
enum class ImageGenerationReferenceRole : uint8_t {
  kEditSource,
  kSubjectIdentity,
  kPose,
};

struct ImageGenerationReference {
  ImageGenerationReferenceRole role;
  RgbaImage image;
};

struct ImageGenerationSpec {
  // Existing prompt and output fields omitted.
  std::vector<ImageGenerationReference> references;
};
```

The initial role vocabulary stays deliberately small:

- `edit-source` identifies the base image whose subject or composition should
  be modified rather than replaced;
- `subject-identity` defines appearance, proportions, costume, palette, and
  identifying landmarks;
- `pose` defines body geometry, facing, limb placement, and ground contact, but
  not character appearance.

Vector order is stable and provider-visible. The core contract permits
repeated identity or pose references when a future caller defines their
meaning; this animation workflow requires exactly one of each. An edit request
may have at most one `edit-source` reference and it must be reference 0. Each
request composes a deterministic indexed legend through the
shared prompt composer, so adapters cannot invent divergent role semantics.
`ComposeImageGenerationPrompt` owns the complete flattened form and uses a
shared turn-body composer for the role legend plus subject. OpenAI sends the
flattened form. Codex keeps artwork instructions in `thread/start` and sends
the same shared turn body in `turn/start`, avoiding both duplicated
instructions and duplicated legend logic. The flattened text orders general
instructions, the role legend, and the subject request, for example:

```text
Reference 1 (subject identity): preserve this character's identity and design.
Reference 2 (pose): use only its pose, facing, limb geometry, and ground contact.
```

The role is a Zebes semantic, not a claim that a provider enforces a native
role field. The current OpenAI Image API accepts multiple edit inputs, but its
documented inputs are ordered images rather than character- or pose-specific
fields. See the
[official image-generation guide](https://developers.openai.com/api/docs/guides/image-generation).

### Consumer migration inventory

Changing the request contract is intentionally a migration, not an additive
field left beside the old optional image. The implementation must update all
of these consumers together:

- `image_generation.{h,cc}` replaces the field and capability boolean,
  validates ordered references and limits, and composes the indexed legend;
- `headless_asset_generation.cc` maps the existing managed parallax redraw
  source to the sole `edit-source` reference and keeps its redraw-specific
  preservation instructions;
- `openai_image_client.cc` replaces its single-image edit branch with ordered
  multipart reference serialization;
- `fake_image_generation_client.cc` adopts deterministic multi-reference
  behavior;
- `codex_image_client.cc` and `codex_app_server_protocol.cc` begin advertising
  and transporting ordered local references;
- `generate_assets.cc` and the headless request models accept resolved
  reference inputs for existing create and redraw operations; and
- the focused generation-contract, OpenAI, Codex protocol/client, fake/service,
  and headless asset-generation tests migrate with the field rather than
  retaining compatibility helpers.

Update `architecture.md`, `codex-image-generation.md`, and the historical
animation gate wording where they describe the old single-reference
capability. Historical run records must say that the old limit applied at the
time of that run rather than rewriting its evidence.

### Validation and capabilities

- Replace `supports_reference_image` with `maximum_reference_images`; zero
  means unsupported.
- Bound each decoded image and the aggregate reference pixels/bytes held by one
  request before an adapter starts.
- Reject unknown roles, invalid RGBA storage, too many references, duplicate
  or non-leading `edit-source` references, and aggregate-size overflow.
- Preserve ownership as copied or moved `RgbaImage` values. Filesystem paths,
  texture handles, and borrowed resource pointers do not cross into the
  asynchronous generation contract.
- Preserve reference role, order, dimensions, and RGBA digest in the request
  snapshot and candidate manifests.

### Provider adapters

The engine, service, and request lifecycle already carry
`ImageGenerationSpec` without needing a new concurrency or ownership model.
Only adapter serialization changes.

**OpenAI adapter**

- Encode references as ordered repeated multipart image inputs.
- Use deterministic index-and-role filenames in the multipart request.
- Use only `ComposeImageGenerationPrompt` for subject instructions and the
  indexed role legend.
- Test exact ordering, field names, encoded images, count limits, and
  cancellation behavior.

Any request carrying a reference uses the edits endpoint. A concrete failure
risk is that its edit bias returns a lightly modified copy of the first input,
which in the primary request shape is the identity board, rather than a new
side-view character in the requested pose. The role legend explicitly says
that the identity board supplies appearance only and that the output must not
be a turnaround, contact sheet, or lightly modified reference board. The
non-candidate pilot below is the hard check; the full batch must not start if
either pilot output exhibits this failure.

**Codex adapter**

- Create one operation-owned subdirectory under the client's existing private
  working directory, track it in `OperationState`, and encode each in-memory
  reference there.
- Supply the files as ordered `localImage` inputs to `turn/start`, followed by
  the existing text and image-generation skill inputs.
- Keep paths and provider protocol types inside the adapter.
- Remove operation-owned reference files on success, provider failure,
  timeout, cancellation, session failure, and destruction.
- Test input ordering, private-path confinement, and every cleanup path.

**Fake adapter**

- Accept the same typed references so headless and engine tests exercise the
  production contract without credentials or network access.
- Preserve the existing redraw behavior when the sole reference is
  `edit-source`, and produce a deterministic result that makes reference order
  and role metadata observable for multi-reference tests.

### Headless authoring

Add a reference-manifest input to the headless authoring command rather than a
fragile collection of role-and-path flag strings. Each entry names exactly one
source: a path relative to the manifest or a managed `SourceArtwork` ID. A
minimal manifest is:

```json
{
  "schema_version": 1,
  "references": [
    {"role": "subject-identity", "source_artwork_id": "character-board"},
    {"role": "pose", "path": "pose-00.png"}
  ]
}
```

One headless reference resolver accepts the parsed source descriptor, resolves
and confines relative paths or loads managed pixels through `Api`, performs
bounded decoding, computes the exact digest, verifies managed source metadata,
and returns the provider-neutral image plus artifact provenance before remote
work starts. The existing redraw
path constructs an `edit-source` descriptor for its managed source and uses
this resolver too; it must not retain a parallel direct-loading path.
Published candidate bundles copy the exact resolved reference PNGs and record
their source kind, role, order, dimensions, and digest. This makes a headless
request reviewable without depending on the caller's original filesystem
layout and prevents filesystem and managed references from becoming two
silent ingestion systems.

`generate_assets` should accept the same reference manifest for its existing
supported asset kinds. The animation experiment uses the same loader and
`ImageGenerationService` from a thin script-side orchestrator; it does not add
`kind=animation` before an animation recipe and candidate transaction exist.

This pass keeps the exact references and their provenance in the immutable
candidate bundle but does not add managed reference-artwork IDs to accepted
asset definitions. Reference inputs are generation evidence, not runtime asset
dependencies. Persisting managed reference ownership after acceptance remains
a separate serialized-provenance decision requiring a migration, dependency
rules, deletion semantics, and tests that load every shipped definition. It
must not arrive as an unversioned optional field merely to support this
experiment.

## Implementation order

1. **Migrate the provider-neutral contract.** Add the three roles, ordered
   references, limits, shared prompt/turn-body composition, and focused
   contract tests. Migrate the fake provider in the same slice so engine and
   service tests remain deterministic.
2. **Complete both production adapters.** Add ordered OpenAI multipart inputs
   and Codex operation-local `localImage` inputs with cleanup tests. Do not
   start real-provider work while either adapter or capability test is failing.
3. **Unify headless ingestion.** Add the path-or-managed-source resolver,
   reference manifest, candidate artifacts, and CLI wiring. Migrate parallax
   redraw to `edit-source` through the same resolver and run its complete
   affected test executable.
4. **Generalize only the feasibility conveniences.** Add the script-local run
   manifest, 12-frame processing/staging coverage, and thin pose-batch
   orchestrator without adding production animation persistence.
5. **Run the visual gates in order.** Stage the guide-only baseline and lock
   timing/provider/model, then run the two pilot requests. Submit the 12-frame
   candidate batch only after both pilots pass.

## Twelve-frame experiment

### Locked input kit

The ignored working kit is
`build/animation-feasibility/pose-conditioned-v1/`. The uploaded 1264 x 580
source is retained verbatim and as a canonical PNG with the same decoded RGBA
digest, `c9d98ac5c0ebb30a3a48cd2f8aeba7112b842f1e49afb9b1d73e0949dc5f1939`.
Its row-major layout is six 210 x 290 cells per row, with no gaps and four
unused white pixels at the right edge. All 12 extracted cells have distinct
recorded digests and passed the fake-provider pilot ingestion path without
clipping.

The identity board is 1610 x 977 with RGBA digest
`92c20ab5e01fd21163e9e59c9e0d4898a98f506dbd45bbe6a06746ee45550e48`.
The real request uses the `codex` adapter, model `codex-imagegen`, 1024 x 1024
square output, and no transparency preference. Frames 0 and 6 are the locked
opposing contact poses. The run manifest currently selects four fixed ticks per
frame; the guide-only baseline must accept that playback before the pilot. A
timing amendment before any provider request relocks the manifests and their
digests rather than changing the approved request in place.

### Guide and pilot records

The supplied drawings did not share a source-cell ground line. The guide-only
baseline therefore records one vertical registration per drawing in
`guide-baseline/registration.json`, aligning their visible bottoms without
scaling, warping, repainting, or reordering. This is authoring normalization of
the pose guide, not permission to translate generated candidate frames.

Both independent processor runs passed structural validation with identical
`deterministic_payload` data and image digests. The registered source RGBA
digest is
`7fcd08e8d1b3e8d5d096905382a68f5304bfd810676275c25baa49b7b0ae8328`.
Disposable Catacombs playback passed human review at four fixed ticks per
frame, so the timing contract is accepted.

`pilot-01` then started frame 0 through Codex and stopped on the first failure,
as required. The turn ended as failed before an image, provider request ID, or
revised prompt was returned. Atomic non-candidate evidence is retained under
`build/animation-feasibility/pose-conditioned-v1/pilot-01/`; frame 6 was never
submitted and no retry was attempted. The configured `codex-imagegen` selector
does not appear in the current Codex model catalogue, while earlier successful
image generation used a normal Codex model plus the imagegen skill. The model
selector is therefore the leading cause, but this run did not retain the App
Server's structured turn error and cannot establish it conclusively.

The protocol now retains the turn error message and optional additional detail
for future failure evidence.

After separate authorization, `pilot-02-sol` changed only the model selector to
the catalogued `gpt-5.6-sol`. Frame 0 returned one fresh 1254 x 1254 image with
the locked character identity and a recognizable version of the requested
contact pose. The runner retained it unchanged, then rejected the request
because the locked expected output was 1024 x 1024. Frame 6 was not submitted.
Evidence remains under `pilot-02-sol/`; its raw RGBA digest is
`b6c9d88d1d282bc22537bece5233f094549cc5dc83c464f6e7c1bad01d4bfbcc`.

After separate authorization, `pilot-03-sol-native` locked the observed
provider-native 1254 x 1254 canvas and generated fresh frames 0 and 6. Both
requests completed with stable provenance and broadly recognizable contact
poses. Their RGBA digests are
`539884fa9bf5bea34f9f7bb729e955567b53622a6ab2d5c68ae840ee67b731ef`
and
`39c321a4c34fc2fec37244baf5b35674cce2acf293bd891e413c4af01f4bf75b`.
Human review rejected the pilot: frame 0 is visibly chunkier and more stout,
frame 6 is leaner, and arm construction and belt details differ. These are not
the same locked character in two poses. The complete 12-frame batch must not
start.

The locked identity board already contains front, right-side, and back views
plus palette swatches. This result shows that one composite identity reference
does not constrain independently generated frames tightly enough. A follow-up
using separate ordered identity crops, or sequential prior-frame conditioning,
is a new budgeted hypothesis. It may reuse the production ordered-reference
contract, but it may not reinterpret this failed pilot or combine its frames.

### Next experiment: separated identity views

The next bounded hypothesis changes only identity-reference packaging. Crop the
existing locked board into its front, right-side, and back character views and
submit those as three ordered `subject-identity` references. Submit the exact
frame pose fourth as `pose`. Do not include the composite board or palette
swatch row as another reference: the point is to remove the board layout and
give each character view its own provider-visible input.

The production ordered-reference contract already permits repeated identity
roles and the Codex adapter advertises capacity for all four inputs. Generalize
only the disposable experiment manifest and runner from one `identity_source`
to an ordered `identity_sources` array. Preserve role, order, source crop,
dimensions, and RGBA digest in the locked request and evidence. Continue to use
GPT-5.6-Sol, provider-native 1254 x 1254 output, the same prompt and negative
requirements, the same pose cells, and four-tick playback.

Run one new two-frame pilot for fresh frames 0 and 6. In addition to fresh
render, identity, and pose checks, compare body build, helmet-to-body ratio,
arm construction, belt geometry, backpack construction, palette, and canvas
occupancy directly between the two outputs. Any visible stout-versus-lean
split, substituted costume structure, or material scale change stops the
experiment before a batch. The rejected pilot's frames are evidence only and
must not be reused.

### Implemented operator contract

The implementation entry point is `run_pose_conditioned_animation`. It accepts
one strict schema-v1 experiment manifest and publishes to a new immutable
evidence directory. Stable provider selectors are `fake`, `openai`, and
`codex`. The locked evidence records `openai-codex` as the Codex adapter's
canonical result provenance, and the runner validates that mapping before it
can classify a result as complete.
The configured Codex model is sent to App Server with model fallback disabled,
so the recorded model is the requested model rather than an adapter label.

The experiment manifest contains exactly these top-level fields:

```json
{
  "schema_version": 1,
  "experiment_id": "run-right-pose-conditioned-v1",
  "animation_run_manifest": "animation-run.json",
  "identity_source": {"path": "identity-board.png"},
  "pose_sheet_source": {"path": "run-pose-sheet.png"},
  "pose_sheet_layout": {
    "grid_x": 0,
    "grid_y": 0,
    "cell_width": 256,
    "cell_height": 256,
    "column_gap": 0,
    "row_gap": 0,
    "columns": 6,
    "rows": 2
  },
  "generation": {
    "provider": "fake",
    "model": "zebes-fake-v1",
    "instructions": "Preserve identity and render one isolated game character.",
    "prompt": "Render the locked right-facing run character.",
    "negative_requirements": "No sheet, wireframe, text, or copied identity board.",
    "target_aspect": {"width": 1, "height": 1},
    "transparency": "prefer-transparent",
    "expected_output": {"width": 256, "height": 256}
  }
}
```

Each source contains exactly one of `path` or `source_artwork_id`. Paths are
normalized, confined relative to the experiment manifest, and copied into the
evidence bundle. The independent `pose_sheet_layout` describes guide crops and
may leave right or bottom margins. The referenced animation run manifest
describes the generated 6 x 2 sheet: its origin and gaps are zero, its cell
dimensions equal `expected_output`, and its timing and planted-foot arrays each
contain 12 entries.

Provider-native rectangular outputs can be requested, retained, and assembled,
but they are non-candidate evidence in this slice. The feasibility processor's
square-cell invariant remains intact; aspect-preserving normalization is a
separate design task. Use square output dimensions for this candidate run.

The commands are deliberately phase-separated:

```bash
build/dev/bin/run_pose_conditioned_animation \
  --asset_root=assets \
  --manifest=/absolute/path/to/experiment.json \
  --phase=pilot \
  --output=/absolute/path/to/new-pilot-evidence

build/dev/bin/run_pose_conditioned_animation \
  --asset_root=assets \
  --manifest=/absolute/path/to/experiment.json \
  --phase=batch \
  --pilot_approval=/absolute/path/to/pilot-approval.json \
  --output=/absolute/path/to/new-batch-evidence
```

The batch command rejects an absent approval, a changed request shape, or an
incomplete pilot before contacting the provider. Approval is a separate strict
schema-v1 JSON document naming the reviewed pilot manifest and run ID, reviewer
and timestamp, and six true checks: fresh single render, identity preserved,
and pose obeyed for both frame 0 and frame 6. The runner has no retry path. Once
a request starts, any transport, provenance, count, or dimension failure stops
the phase and atomically publishes non-candidate evidence.

### 1. Lock the input kit

- Retain the original 12-pose sheet and record its RGBA digest.
- Extract all 12 pose cells in row-major order without dropping, reordering, or
  synthesizing poses. Record every crop rectangle and cell digest.
- Use one locked character identity board for all requests.
- Record one shared subject prompt, generation instructions, provider/model,
  target aspect, output settings, and negative requirements.
- Author a 12-entry planted-foot/contact expectation from the actual pose
  sequence. Do not copy the ten-frame mask from the failed experiment.

Cropping the complete sequence into per-pose conditioning inputs does not
change its motion; selecting a subset or replacing individual failures would.

### 2. Settle the 12-frame playback contract

Before any quality-bearing generation request, stage the supplied guide cycle
itself through the live proof. Choose a uniform three- or four-fixed-tick frame
duration based on that baseline and then lock it in the run manifest.

Twelve frames at four ticks produce a 48-tick loop; twelve frames at three
ticks produce a 36-tick loop. Do not introduce uneven timing solely to force
the old ten-frame sequence's 40-tick duration. Uneven timing is valid only if
it is deliberately authored for the actual gait before generation begins.

The pure feasibility processor and runtime animation cursor are already
frame-count-independent. Generalize the disposable preset, CLI, focused tests,
and live-proof stager that currently assume a 5 x 2 ten-frame locomotion sheet.
Read grid shape, ordered timing, and contact expectations from the locked run
manifest rather than adding another hard-coded ten-versus-twelve branch. The
12 processed 48-pixel frames pack into a 576 x 44 runtime texture under the
current feasibility geometry.

The locked animation run manifest is an explicitly script-local schema owned
by the disposable orchestrator. It may describe the 6 x 2 grid, timing,
contact expectations, inputs, and evidence paths, but it is not serialized
asset data and must not become a seed for `AnimationArtworkRecipe`.

### 3. Run a two-frame plumbing pilot

After production contract tests and guide-only staging pass, submit frames 0
and 6 through the selected real provider. These opposing contact poses use the
exact identity-first, pose-second reference order, prompt composition, model,
and output settings intended for the complete batch.

The pilot is transport and gross-conditioning evidence, not an animation
candidate. Preserve both requests and outputs, but never assemble them, score
them as a loop, or reuse them in the 12-frame batch. Each pilot output must:

- be one newly rendered right-facing character rather than a turnaround,
  contact sheet, wireframe, or lightly edited copy of the identity board;
- retain the recognizable subject identity and design landmarks; and
- obey the corresponding contact pose closely enough that a complete batch
  would test the intended hypothesis.

If either pilot fails, stop before the 12-frame batch. Changing reference
order, role assignment, legend wording, or endpoint strategy is a new request
shape and requires a newly reviewed pilot budget.

### 4. Generate one complete candidate batch

For frame `N`, submit exactly two references in this order:

1. the locked `subject-identity` board; and
2. pose cell `N` labelled `pose`.

Only the pose image and explicit frame index differ between requests. Preserve
all raw provider outputs, request IDs, submitted and revised prompts, reference
digests, dimensions, and timestamps. Do not feed frame `N - 1` into frame `N`
in this experiment; sequential conditioning is a distinct follow-up
hypothesis.

The planned visual budget is 14 provider calls: two non-candidate pilot calls
followed by one batch of 12 candidate calls. The runner does not retry a
transport failure, even when no candidate was returned. No replacement pilot
or second visual batch is pre-authorized. A later attempt must be reviewed from
the first run's evidence, regenerate the complete applicable phase, and change
exactly one recorded hypothesis; it may not retry only weak frames or combine
frames from different attempts.

### 5. Assemble and process without repair

- Validate that all 12 unmodified outputs exist and match the locked source
  contract.
- Assemble them deterministically into one 6 x 2 source sheet in manifest
  order.
- Run the existing shared extraction, matte isolation, registration, palette,
  packing, diagnostics, and evidence publication.
- Continue to forbid per-frame scale, translation, warping, repainting,
  identity repair, or frame reordering.
- Run the accepted input twice and compare the deterministic payload and image
  digests.

### 6. Review the live loop

Review the native frames, enlarged overlays, adjacent differences,
last-to-first difference, and live Catacombs playback. The candidate passes
only when all of the following are true:

- all 12 poses appear once, in the supplied order, and read as one running
  cycle;
- character identity, proportions, costume landmarks, palette roles, camera,
  and rendering language remain stable across every independently generated
  frame;
- planted feet and lifted feet follow the locked contact expectations without
  visible sliding;
- silhouette scale and origin remain stable without per-frame correction;
- no anatomy, costume, foreground detail, or frame edge is clipped, detached,
  duplicated, or invented; and
- frame 11 closes naturally into frame 0 at live speed.

An automated structural pass is necessary but not sufficient.

## Decision rules

| Evidence | Conclusion |
|---|---|
| Reference roles, ordering, limits, or cleanup fail production tests | Fix the shared reference implementation before spending generation requests. |
| The 12-pose baseline cannot be processed or staged correctly | The experiment setup is invalid; fix the manifest, processor convenience, or timing contract first. |
| Either contact-pose pilot copies the identity board, loses identity, or ignores the pose | Stop before the batch; the request shape has not established usable conditioning. |
| Generated poses still do not follow the guides | Pose-conditioned independent generation fails this feasibility hypothesis. |
| Pose obedience passes but identity or rendering drifts | Retain the evidence and open a separately budgeted sequential-conditioning experiment. |
| Still frames pass but live playback fails | The batch fails; do not repair individual frames or reinterpret structural evidence as animation quality. |
| The complete batch passes deterministic processing and live review | Record pose-conditioned independent generation as a feasible optional source path, then design the production multi-image frame-set recipe separately. |

## Deferred retry hypotheses

These are candidates for a later single-hypothesis attempt, not additions to
the current implementation or 14-call visual budget:

- **Whole-sheet generation with separate references.** Submit the identity
  board and complete pose sheet as distinct labelled references in one
  coherent-sheet request. This tests whether the failed run's combined board
  constrained conditioning bandwidth while retaining the cross-frame identity
  prior that independent requests give up. Before locking this variant, verify
  the selected provider/model's input-reference limit and encode that limit in
  its advertised capabilities; the OpenAI adapter's output-candidate limit is
  not evidence of its input-reference limit.
- **Pose guide as `edit-source`.** Put the pose guide first as `edit-source`
  and the identity board second as `subject-identity`, asking the provider to
  repaint the guide as the character. This deliberately uses edit bias for
  pose obedience and is a different mechanism from the primary experiment.
- **Sequential conditioning.** Supply frame `N - 1` while generating frame
  `N`. This remains deferred because it changes request state, failure
  recovery, and drift behavior rather than merely changing prompt wording.

Any selected alternative receives its own bounded pilot and complete-candidate
rules. No alternative may reuse or splice frames from the primary batch.

## Verification before handoff

Run focused tests for the changed boundaries:

- generation contract validation and prompt/reference ordering;
- OpenAI multipart serialization;
- Codex App Server `localImage` serialization and operation cleanup;
- fake-provider engine and headless generation bundles;
- reference-manifest confinement, bounds, artifacts, and provenance; and
- 12-frame feasibility processing, packing, diagnostics, and live staging.

Format and lint all edited C++ translation units together. Run the complete
affected test executables and `git diff --check`. A full repository test run is
not required unless the implementation changes serialized provenance, broadly
consumed headers beyond the generation boundary, or central build/toolchain
logic.

## Non-goals

- deriving ten frames by removing two supplied poses;
- selective per-frame retries, splicing, repainting, or manual pose repair;
- an animation editor, recipe manager, persistence transaction, or shipped
  generated character;
- left-facing or idle animation generation;
- provider-specific reference roles in domain or asset interfaces; and
- remote generation in the gameplay runtime.

## Resolved review decisions

1. The initial contract has three roles: `edit-source`, `subject-identity`,
   and `pose`. An `edit-source` is optional, unique, and always reference 0.
2. `ComposeImageGenerationPrompt` owns the indexed legend; Codex reuses its
   shared turn body while retaining developer-level artwork instructions.
3. Filesystem and managed references use one resolver, and existing redraw
   migrates to it rather than keeping a parallel path.
4. The planned visual budget is 14 calls: frames 0 and 6 as non-candidate
   pilots, followed by one new 12-frame candidate batch. No visual retry is
   pre-authorized.
5. Candidate bundles retain exact reference artifacts and role/digest
   provenance. Accepted definitions do not gain managed reference IDs in this
   pass.
6. The animation run manifest stays script-local and non-normative. The
   uniform frame timing and selected real provider/model are locked after the
   guide-only baseline and before the pilot, without changing the production
   implementation contract.
