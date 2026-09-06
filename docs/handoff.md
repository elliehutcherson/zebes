# Active handoff

Updated 2026-09-06. [`roadmap.md`](roadmap.md) owns sequencing; this file is the
short resume point. Completed narratives live in [`history/`](history/README.md).

## Current state

Two tracks proceed independently:

- **Track 4 — environment/content.** The Catacombs production route has zone
  fades, complete finite parallax coverage through 0.5×, independent masonry,
  distributed player-scaled decor, initial floor/foreground variants, and a
  distinct middle ceiling frieze. The complete route review has no objective
  findings. Remaining silhouette variation is non-blocking content polish.
- **Track 5 — runtime/animation.** Runtime Milestones 1–3, pure frame-set
  processing, recipe/bundle lifecycle, and headless animation curation are
  complete. Human review still blocks the production player-art gate. The
  separate interactive mangled-pose experiment is now complete: it binds both
  arms and legs, uses the exact twelve-frame Rig Bench clip as its visible
  authoring skeleton, persists validated repository state, and exports rough
  cleanup inputs. It is experiment tooling, not a production puppet.

The reusable ordered-reference generation boundary remains supported for
OpenAI, Codex, headless generation, and redraw; it is not an animation roadmap
item. The deprecated evidence is indexed under
[`history/`](history/README.md).

## Pick up next

### Track 5: clean completed animation-experiment debt

The next workstream is cleanup, owned by a fresh agent. Do not add editor
features or resume provider requests. Preserve the accepted authoring contracts:
the tracked source/spec/state triplet, exact 23-point Rig Bench clip, twelve
canonical frames, front/rear arm ownership, repository save validation, and
transparent PNG export.

Start with **Cleanup owed from the animation experiments** below. Bound each
deletion through CMake consumers and focused tests; generated outputs and
diagnostic-only tools should not survive merely because this experiment used
them.

### Track 5: finish the layered player-art gate

The technical mouse import and runtime record remains in
[`history/mouse-player-production-2026-09-03.md`](history/mouse-player-production-2026-09-03.md).
The newer art-direction evidence is in
[`animation-artwork-pipeline.md`](animation-artwork-pipeline.md) and
`experiments/character_binding/FINDINGS.md`:

The ARAP-first plan remains withdrawn. The latest candidate follows
[`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):

1. **Done.** Preserve useful diagnostics, corrected `shoulder_b`, reachable
   passing pose, moved-part tint, arm-hidden output, and separate shadow.
2. **Rejected.** Relationship-aware stretching still added 592 pixels to a coat
   layer that was already correct.
3. **Done.** Add immutable semantic-layer evidence and a hard gate over decoded
   RGB, alpha additions/removals, and source/final digests.
4. **Done.** Add `mouse_immutable_coat_v1.json` with no coat stretching.
   Source/final coat digests match
   `0400b5084a83957f727c2292d52f481f1af07e29331c628b07c69c2561351fc4`.
5. Review `semantic-arm-immutable-coat-v1` at 48px: immutable coat alone,
   arm-hidden body, moved-arm tint, shadow tint, and current Catacombs bundle.
6. If accepted, keep the coat immutable, separate the tail, then clear the
   149-orphan and four-airborne-fold hard failures.
7. Then second arm, split footwear, legs and tail. Skeleton-conditioned ML stays
   deferred. M4 remains blocked until replacement art passes.

Review frames by tinting the moved part, never by eye — see "How to review this
without getting it wrong" in the experiment doc.

### Track 5: interactive mangled-pose authoring

The preferred `mouse_interactive_run_v1.json` uses the human-selected
`reference_07` generated run frame retained as `interactive-run-source-v1.png`.
Human correction established that the screen-left arm is the **front** arm and
the screen-right arm is the **rear** arm. Part names, bone mappings, and draw
order encode rear arm, both legs, visible body/coat, then front arm. The
canonical blue/orange motion tracks remain attached to their original physical
limbs across all twelve frames.

`layered_puppet_editor` provides twelve-frame dropdown, previous/next controls,
8 FPS playback, three edit modes, 1–6× zoom, mesh paint/erase, and transparent
256px PNG export. `Move pose joints` deforms the composite.
`Calibrate source joints` moves the bind skeleton over an unchanged source
image. `Define attachment mesh` supplies a full-canvas 4px triangle grid for
each arm or leg; the traced region is only an initial seed. Green means painted
mesh and no overlay means no mesh. Pose rendering removes only painted
triangles from the static source, then deforms those exact source pixels.

The exact 23-point / 22-bone `rig-bench.json` clip is the only visible
authoring skeleton and follows the selected frame in every edit mode. Thirteen
mapped joints render as draggable circular handles and drive the internal mesh
controls; the remaining exact reference points render as colored squares. The
reduced 13-joint control still exists internally but has no separate blue
overlay or UI toggle. Frames with no saved override show `Canonical`; the first
pose-joint edit changes only that frame to `Authored`. Only authored frames are
written under `frame_overrides`.

Durable authored state lives at
`experiments/character_binding/editor_states/mouse_run_v1.json`. The C++
loopback `serve_layered_puppet_editor` validates it against generated source
and puppet-contract digests and writes it atomically. Browser edits remain a
local crash-recovery draft until **Save to Repo**; **Revert from Repo** restores
the tracked state. Mismatched source/topology saves return 409 without changing
the file.

The user chooses which canonical frame the source represents. **Set anchor and
regenerate** applies each canonical joint's vector delta from that anchor to the
authored source joints. The anchor frame stays exact; individual frame edits
are stored as offsets. Re-anchoring clears those offsets after confirmation but
preserves source calibration and painted meshes.

Browser verification re-anchored from `reference_07` to `reference_04`, checked
the exact anchor and derived joint delta, navigated and played frames, saved and
reloaded a painted mesh through the repository service, rejected a mismatched
contract, and restored the tracked `reference_07` baseline. It verified all 23
original points and 22 bones, exact per-frame reference changes, canonical
versus authored state, corrected screen-left/front and screen-right/rear
mapping, and image deformation through a mapped reference handle. The reduced
blue skeleton and its toggle are absent. Only the 8769 C++ editor service should
run.

### Track 5: Codex pose conditioning (parallel experiment)

Opened 2026-09-05. Evidence and metrics live in
`experiments/character_binding/evidence/codex-pose-conditioning/` and the new
section of `experiments/character_binding/FINDINGS.md`. This does not block the
layered-puppet gate above; it is a candidate replacement for its *input*.

**Why it opened.** The layered puppet is a still image at 48px. All three
candidates (`v5`, `v6`, `immutable-coat-v1`) are byte-identical on neutral and
differ by 5-21 pixels of 717 on the other poses. Contact changes the silhouette
by 11 pixels, passing by 13. Only `front_arm` is bound to bones, so eight of ten
bones drive nothing and the striding legs in the pose data render nothing.

**What is settled.**

- Generation supplies pose and limb separation; it will not supply registration.
  Measured twice. Attempt 2 stated an exact canvas, height, ground row, block
  size and palette and the model honoured none of them — Codex satisfied the
  numbers afterwards with ImageMagick.
- Registration aligns frames to a **shared world ground line**, never to each
  figure's bounding box. Normalising bounding boxes deleted the model's real
  flight frame and flattened hip oscillation to zero.
- Skeleton conditioning works through the reference channel when the request
  includes a matched pair: an existing frame plus that frame's skeleton, then
  the target skeleton. Horizontal obedience was complete; vertical travel came
  in at about a third of what was asked.
- The approved source art is a 128x128 sprite stored at 256x256, so rig
  coordinates are in doubled space.

**The tool.** A skeleton animation editor is published as an Artifact:
`https://claude.ai/code/artifact/6ad9861a-8782-4aaa-a7c3-3c1be17af5cb`
("Puppet Rig Bench"). It originally owned named clips over one shared
27-point / 26-bone skeleton, a draggable floor, point and bone editing,
cross-frame length constraints, playback, tracing underlays, and cycle checks.
It exports the clip and a COCO-18 form and persists at `rig/bench`.
`out/codex-pose-conditioning-v1/rig-bench.json` is now the repository snapshot;
its current simplified topology has 23 points and 22 bones.

The repository-owned `skeleton_rig` C++ library parses and validates that
snapshot and measures cycle invariants. Its HTML review page and PNG guide
renderers were deleted when the experiment closed on 2026-09-06.

The bind pose was traced by hand from the generated `up` frame and verified
against the art; it owns the character's proportions and every new frame starts
as a copy of it. The earlier procedural seeds were deleted — they produced 13px
shins on a 195px figure.

**Pick up here.** The `run` clip has twelve complete poses mapped left-to-right
across both rows of the supplied running reference. Its simplified 23-point /
22-bone topology removes both lateral hips and both heels, connects knees
directly to `hip_c`, and retains toes for foot direction. The C++ review gate
reports 17 px of hip oscillation, alternating lead feet, and 1.00 px maximum
integer-coordinate bone-length drift.

The first two retained pilots are misconfigured for the intended experiment:
`matched-pilot-v1/` and `matched-pilot-v2-simplified/` supplied an existing
running frame and its skeleton in addition to the standing identity and target.
They are retained as evidence but do not test the requested input contract.

`standing-skeleton-pilot-v3/` corrects the boundary. Each of three sequential
requests supplied exactly two images—standing subject, then target skeleton—and
requested one output. All returned untouched native 1254px images. Pose control
still fails: figure height varies by 17.7%, baseline by 74 px in the wrong phase
order, frame 3 and frame 10 overlap at 0.745 IoU, and the high recovery target
is another grounded split stride. Engineering review blocks the remaining nine
requests; human comparison review is pending.

### Track 4: finite content polish

Add one floor-scatter silhouette and one foreground-shroud silhouette only where
focused 0.5×/1×/2× evidence shows repetition. Preserve density, layer, sort
order, and collider counts; finish with the complete route gate.

## Runtime invariants

- `RuntimeWorld` borrows one frozen loaded-level graph and owns mutable
  entity-keyed transforms, motion, controller state, presentation, and playback.
- Authored entities persist stable Blueprint-local state keys. Boot resolves the
  six player state handles; fixed ticks do no string or catalogue lookup.
- Idle clips loop at 15 ticks per frame; airborne clips hold their final frame.
- Every player state retains the exact 32×64 collider.
- Scene composition presents runtime transforms and frames without mutating the
  authored level.
- M4 uses latest-wins input/frame snapshots and a bounded I/O executor; SDL and
  GPU upload remain on the main thread.

## Relevant boundaries

- [`architecture.md`](architecture.md): architecture index and domain links.
- [`engine-runtime-plan.md`](engine-runtime-plan.md): runtime M4/M5 design.
- [`environment-artwork-plan.md`](environment-artwork-plan.md): active Track 4
  contracts and remaining content work.
- [`headless-level-review-plan.md`](headless-level-review-plan.md): focused and
  complete level-review procedure.
- [`prop-artwork.md`](prop-artwork.md): current prop lifecycle and remaining
  provider/recovery follow-up.
- [`character-layer-deformation-experiment.md`](character-layer-deformation-experiment.md):
  the measured layered-puppet problems, the fix order, and the fallbacks.

## Cleanup owed from the animation experiments

- `layered_puppet_editor.cc` owns the generated browser client as one large raw
  literal. Split only along an existing build boundary or introduce a tested
  asset-embedding step; do not create a second hand-maintained client.
- The exact 23-point authoring rig reaches the 13-joint deformation model
  through `kReferenceJointMapping`. Give that mapping one validated data owner
  before supporting another rig; preserve current names and image-space arm
  ownership during the cleanup.
- `build/codex-run-sheet/` duplicates what now lives in
  `experiments/character_binding/evidence/codex-pose-conditioning/`. `build/` is
  generated output and can be deleted.
- The Rig Bench artifact still carries its diagnostics: an on-page log panel,
  pointer counters, per-move logging, and a `diag/log` document written to the
  store beside `rig/bench`. Strip all of it once the editor has been used for a
  full session without incident, and delete the `diag/log` document.
- `out/codex-pose-conditioning-v1/tools/*.py` are stdlib-only analysis scripts
  with no owner. If the registration pass moves into C++ they should be deleted
  rather than maintained in two languages; `measure_sheet.py` encodes the
  pre-registered metrics, so keep it until the C++ gate replaces it.
- The layered-puppet track's hard gates (149 orphans, four folds, 355/177 hole
  counts) are still red and still measure quantities invisible at 48px. Decide
  whether to retire them or re-express them as silhouette-change gates before
  anyone spends more time clearing them.
- The five tracing underlays are derived from a Codex render whose run cycle is
  wrong — the lead foot never alternates. They are useful for limb shape and
  body height only. Do not treat them as an authoring target.
- `animation_artwork_spike`, `animation_artwork_run_manifest`,
  `pose_conditioned_animation_batch` and `run_pose_conditioned_animation` are
  still built and belong to the closed generated-animation experiment. Unlike
  `stage_animation_live_proof`, which was removed because every asset it named
  was gone, these still have live tests; check before removing.

## Non-blocking debt

- Move `SdlWrapper` from `src/common` to `src/platform/sdl` when editor SDL
  composition is next touched; reuse `SdlSubsystem` for editor ownership.
- Replace `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths fully live-verified.
- Windows Codex process transport remains unsupported.
- Dead sprite/blueprint definitions still load at boot: `Player Airborne
  Left/Right Proof`, `kSamusJumpingLeft`, eight `kGrass*` sprites and the whole
  `Samus` blueprint chain. No level entity or blueprint state uses them; they
  survive only as `previous_sprite_id` history. Deleting definitions touches the
  serialized format, so it wants its own change with a migration.
- Catacombs `spawn_point` is (256, 512) while player entity 4 sits at (256, 864),
  so the camera opens 352 px above the mouse until follow corrects it. Content
  fix, not a code fix.

## Last verification

The immutable-coat proof reports zero changed coat pixels, zero alpha additions,
zero alpha removals, and identical source/final digests. Neutral composite
difference remains zero. No attachment or backfill mask is created; the old
full-arm metric reports 745 uncovered pixels and is deliberately not a gate
because pixels outside the coat silhouette may reveal background.

The corrected passing pose casts a separate 288-pixel shadow without mutating
the stored coat. Focused Catacombs review at 0.5×, 1×, and 2× reports no
objective findings.

Hard validation still rejects 149 body-visible orphan pixels and four airborne
folds. Contact has 355 interior holes against 174 neutral and passing has 177.
`layered_puppet_test` passes 23 cases,
`layered_puppet_diagnostics_test` passes 25, and
`semantic_layer_import_test` passes eight; both affected-target gates and
clang-tidy pass.

The interactive editor's final browser pass confirmed the sole 23-point
authoring skeleton, corrected arm mapping, exact frame switching, mapped-handle
deformation, canonical/authored state, and clean `reference_07` repository
reload. `layered_puppet_editor_test`, its affected-target gate, clangd,
clang-tidy, and `git diff --check` pass. The broader `image_digest`
affected-target gate passed all 64 consumers after the shared SHA-256 helper was
added.
Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until the replacement art passes.
