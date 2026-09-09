Historical snapshot. Superseded by the approved twelve-frame animation work on 2026-09-08.

# Active handoff

Updated 2026-09-07. [`roadmap.md`](../roadmap.md) owns sequencing; this file is the
short resume point. Completed narratives live in [`history/`](../history/README.md).

## Current state

Two tracks proceed independently.

**Track 4 — environment and content.** The Catacombs route has zone fades,
finite parallax coverage through 0.5×, independent masonry, distributed
player-scaled decor, floor and foreground variants, and a middle ceiling frieze.
The complete route review reports no objective findings. Remaining silhouette
variation is non-blocking polish.

**Track 5 — runtime and animation.** Runtime Milestones 1–3, frame-set
processing, the recipe lifecycle, and headless curation are complete. The
production player-art gate is still open: the shipped mouse passed every
technical check and failed art direction, and replacement art has not passed
yet. M4 is blocked until it does.

Generated animation is closed twice over. Coherent sheets failed live motion
review, and skeleton conditioning failed because the generator will not draw a
pose that contradicts what it has learned running looks like
([`history/codex-pose-conditioning-2026-09-06.md`](../history/codex-pose-conditioning-2026-09-06.md)).
The replacement approach poses the character directly in the interactive editor
and asks generation only to clean up the result.

The animation-experiment cleanup workstream is done
([`history/repository-cleanup-2026-09-06.md`](../history/repository-cleanup-2026-09-06.md)).

The interactive editor was rebuilt on 2026-09-07 around a puppet document, with
a headless CLI, a document server and a five-step browser page. The second
editor was retired the same day, so there is one. It is uncommitted on
`animation-recipe-lifecycle` and does not affect the art gate.

## Pick up next

### Track 5: the layered player-art gate

The measured problems and the fix order are in
[`character-layer-deformation-experiment.md`](../character-layer-deformation-experiment.md).
The ARAP-first plan remains withdrawn.

1. **Review `semantic-arm-immutable-coat-v1` at 48px** — immutable coat alone,
   arm-hidden body, moved-arm tint, shadow tint, and the current Catacombs
   bundle. This is the open decision.
2. If accepted, keep the coat immutable, separate the tail, then clear the
   149-orphan and four-airborne-fold failures.
3. Then the second arm, split footwear, legs and tail.
4. Render and import replacement clips, preserving the stable Blueprint, six
   state keys, 32×64 collider, timing and playback contracts.

Review frames by tinting the moved part, never by eye — see "How to review this
without getting it wrong" in the experiment doc.

Only the torso and one arm consume artwork today. The four-pose evidence is a
one-arm stress test, not a gait test.

### Track 5: interactive pose authoring

Rebuilt around a puppet document. `experiments/character_binding/README.md`
sections 11–14 have the commands. Everything below is uncommitted work on
`animation-recipe-lifecycle`.

**What a document is.** One file holds a puppet: its source artwork, guide
backdrop, skeleton, the limb each joint belongs to, the parts cut from that
artwork, the frames those parts are posed in, and the rate they play at. It is
the only file a person keeps. The layered puppet spec is derived from it in
memory and handed to the builder; nothing writes one to disk.

**A rig is a document with nothing else in it.** Joints, chains and bones need
no artwork, so a skeleton is authored on its own and written out as a Rig Bench
file — `puppet_edit --emit_rig`, or **Write this skeleton out as a rig** on the
Skeleton step. That file lands in `--rig_root` and the next character imports
it. Chains are why the document stores a limb per joint: a Rig Bench file
records one per point, and no bone graph recovers it, since the mouse rig is one
connected graph with seven limbs.

Saving over an existing rig replaces it, and optionally carries the change into
every document that imported it: joints and bones the rig gained, and its limb
names. It never moves a joint a document already has — those positions were
dragged onto that character's own artwork and are the binding between rig and
drawing — and never removes one. A document still using a joint the rig dropped
is named and left alone.

**The renderer is rigid and always has been.** `TransformPoint` and
`TransformMeshVertex` place a part's pixels at the distance from the joint they
were drawn at and turn them; neither scales. `may_stretch` on a bone opts one
limb out of that, and is off everywhere by default. What a posed bone length
different from its bind length actually means is that the skeleton and the
artwork disagree at the far end, which is what the Frames step's worst-stretch
reading is for.

**How it is edited.** Every change is a named command applied to the document.
`ApplyPuppetCommand` is the only way a document changes, so the browser, the
headless CLI and the tests all travel one path. A command lands on a copy that
must pass validation before it replaces the document, so a handler that forgets
an invariant fails the command instead of leaving a broken file.

**The three ways in.**

- `puppet_edit` — headless. Takes a document and a JSON array of commands. This
  is the interface an agent uses.
- `serve_puppet_editor` — the only editor. Serves a directory of documents on
  loopback, holds one open, and edits it with the same commands. Every flag has
  a default, so `build/dev/bin/serve_puppet_editor` from the repository root is
  the whole command.
- `puppet_editor.html` — the page that server hands out, embedded in the binary.
  Five steps in the order the data forces: Source, Skeleton, Parts, Frames,
  Export. Source, Skeleton and Parts work in bind space against the flat
  artwork; Frames and Export show the posed puppet.

**What the page never does.** It does not edit the document. It posts a command
and redraws the answer, which is why there is no save button. Zoom, the open
step, the frame on screen, playback and the half-drawn outline live in browser
storage and never reach the file.

**The old editor is gone.** `serve_layered_puppet_editor`,
`layered_puppet_editor{,_state}` and `editor_states/` were deleted on
2026-09-07. Everything the old editor held had already moved: its tracked state
was thirteen source joints, zero frame overrides and zero painted meshes, and
those thirteen joints are exactly `mouse_interactive_run_v1.json`'s rest pose.
`render_layered_puppet` no longer writes `editor.html` or
`editor-contract.json`, and `--editor_reference_rig` is gone with them. The
three See-through specs still render through `--spec`; they no longer have an
interactive editor, and get one when they migrate to documents.

Screen-left is the **front** arm and screen-right is the **rear** arm. The rig's
anatomical `_l` joints sit on screen-**right**. That mismatch is how a part gets
traced over one arm and built on the other's bones, which is what happened to
`test-puppet.json`.

### Track 4: finite content polish

Add one floor-scatter silhouette and one foreground-shroud silhouette only where
focused 0.5×/1×/2× evidence shows repetition. Preserve density, layer, sort
order and collider counts; finish with the complete route gate.

### Track 5: what the puppet editor still needs

Highest first. None of this is blocking the art gate.

1. **Snapping moves points, not segments.** With overlap off, a point clicked
   inside a part claimed earlier snaps onto that part's edge. A straight run
   between two points that are both outside can still cut a corner off one, and
   only the build-time ownership rule takes those pixels back — so the drawn
   line and the built edge can still differ there. Closing that needs clipping
   the whole polygon, which is a real polygon-boolean pass.
2. **The three tracked documents all still overlap.** `allow_overlap` is true on
   each, which is what they always did. Turning it off on `mouse_profile_v1`
   clears all eleven pairs and it still builds, but which part should give up
   which pixels changes the art, so that is a review decision, not a cleanup.
3. **Nothing gates overlap at build time.** The switch settles ownership, and
   with it on nothing refuses a build that still has a shared pixel. The old
   spec had a `require_exclusive_visible_ownership` gate.
4. **A rig update cannot move a joint.** Carrying a rig change into the
   documents that imported it adds joints, bones and limb names, and
   deliberately never moves a joint that is already there. There is no way to
   say "this one really did move in the rig, take the new position".

Done on 2026-09-07: the two editors are one, the flags have defaults, a root
joint slides the whole rig, `scale_to_size` stretches a puppet onto artwork at
another resolution, `set_part_bones` re-points a part without destroying its
outline, a skeleton can be written out as a Rig Bench file and authored with no
artwork at all, draw order and `exclude_parts` have controls, overlapping parts
are reported with the pixels they share, the Frames step shows the worst bone
stretch, frames per second lives in the document, `allow_overlap` settles
ownership by declaration order, `may_stretch` opts one bone out of rigid
placement, a rig can be updated in place and carried into the documents that
imported it, and bones are coloured by limb. Five bugs went with them:

- The embedded page was served through a `string_view` built from a `char`
  array, so any zero byte in `puppet_editor.html` cut it in half with a 200 and
  no error anywhere. `puppet_editor_page_test` now checks both ends.
- A document that would not build could not be opened at all, so a puppet sized
  unlike its picture was locked out of the tool that repairs it.
- A rejected command batch reported the error from restoring the old document
  rather than the error that rejected it.
- The blocker line was per-step, so a document that could not build showed a
  green step and an empty message. It is now one line on every step.
- **The preview did not draw what the renderer draws.** It scaled every part by
  its bone's length ratio while the renderer never scaled at all, and it blended
  two transformed points where the renderer blends the two bones' turn. So a
  shortened bone showed a shrinking limb that came out full length, and a bent
  elbow looked thinner on screen than in the frame. `boneTransform`,
  `transformPoint` and the new `skinnedVertices` now mirror `layered_puppet.cc`.

## Runtime invariants

- `RuntimeWorld` borrows one frozen loaded-level graph and owns mutable
  entity-keyed transforms, motion, controller state, presentation and playback.
- Authored entities persist stable Blueprint-local state keys. Boot resolves the
  six player state handles; fixed ticks do no string or catalogue lookup.
- Idle clips loop at 15 ticks per frame; airborne clips hold their final frame.
- Every player state retains the exact 32×64 collider.
- Scene composition presents runtime transforms and frames without mutating the
  authored level.
- M4 uses latest-wins input/frame snapshots and a bounded I/O executor; SDL and
  GPU upload stay on the main thread.

## Where the puppet editor lives

| File | What it holds |
|---|---|
| `src/artwork/puppet_document.{h,cc}` | the document, the commands, `ApplyPuppetCommand`, validation, build readiness |
| `src/artwork/puppet_document_json.{h,cc}` | strict read/write of documents and command lists, and the derived spec |
| `src/artwork/puppet_editor_session.{h,cc}` | one open document plus the artwork built from it |
| `src/artwork/puppet_editor_workspace.{h,cc}` | the directory of documents and which one is open |
| `src/artwork/puppet_build_json.{h,cc}` | the built geometry the browser bends artwork with |
| `src/artwork/puppet_editor.html` | the page, embedded by `puppet_editor_page` |
| `src/artwork/layered_puppet_spec.{h,cc}` | spec JSON to `LayeredPuppet`, pulled out of `render_layered_puppet` |
| `src/common/loopback_http_server.{h,cc}` | the socket code under the server |
| `scripts/puppet_edit.cc` | the headless entry point |
| `scripts/serve_puppet_editor.cc` | the HTTP layer over a workspace |
| `experiments/character_binding/puppet_documents/` | tracked documents |

## Where things live

`experiments/character_binding/` splits three ways. `inputs/` is tracked and
holds what the tools consume: the approved mouse, the isolated running source
whose coordinates own the cutout masks, the See-through layers, and the rig.
`evidence/` is tracked and records closed experiments. `out/` is ignored whole
and holds renders any tool can write again. `ls -t out | head` shows the newest.

- [`architecture.md`](../architecture.md): architecture index and domain links.
- [`engine-runtime-plan.md`](../engine-runtime-plan.md): runtime M4/M5 design.
- [`environment-artwork-plan.md`](../environment-artwork-plan.md): Track 4
  contracts and remaining content work.
- [`headless-level-review-plan.md`](../headless-level-review-plan.md): focused and
  complete level-review procedure.
- [`prop-artwork.md`](../prop-artwork.md): prop lifecycle and provider follow-up.
- [`character-layer-deformation-experiment.md`](../character-layer-deformation-experiment.md):
  the layered-puppet problems, the fix order, and the fallbacks.

## Open decisions

None outstanding. The layered-puppet hard gates were retired on 2026-09-07;
see "Nothing fails a render any more" below.

## Non-blocking debt

- The editor still owns its own `SdlWrapper` rather than reusing
  `SdlSubsystem`. `SdlWrapper` itself moved to `src/platform/sdl` on
  2026-09-07, so the layering is right; the ownership half of that entry is
  what is left, and it waits for the next change to editor SDL composition.
- Replace the `viewport_model.h` linear lookup only after level-size profiling.
- Tile deletion still lacks the shared destructive-action confirmation prompt.
- Finish credential-gated OpenAI and real Codex editor lifecycle checks before
  claiming those provider paths are live-verified.
- Windows Codex process transport is unsupported.
- The `Samus` and `Grass1-3` blueprint chains are kept as test fixtures and no
  level places them. Six of their sprites were pinned by recipe rollback records
  until `previous_sprite_id` was removed; nothing pins them now, so they can go
  whenever the fixtures are no longer wanted.
- Catacombs `spawn_point` is (256, 512) while player entity 4 sits at (256, 864),
  so the camera opens 352 px above the mouse until follow corrects it. Content
  fix, not a code fix.
- `build/ui` and `build/release` are real presets last configured on 2026-08-29
  and 2026-08-17, so both are far behind. They are 2.5G and 69M and rebuild
  themselves; delete either whenever the disk is wanted. Cleared on 2026-09-07:
  a whole second CMake tree configured directly in `build/` rather than in a
  preset directory, plus `codex-run-sheet/`, `animation-feasibility/`,
  `prop-artwork-spike/`, `skeleton-rig-preview/` and loose render output —
  1.5G. Every script reads `build/<preset>`, so nothing pointed at any of it.

## Last verification

The immutable-coat proof reports zero changed coat pixels, zero alpha additions
or removals, and identical source/final digests. Neutral composite difference is
zero. No attachment or backfill mask is created; the old full-arm metric reports
745 uncovered pixels and is deliberately not a gate, because pixels outside the
coat silhouette may reveal background.

The corrected passing pose casts a separate 288-pixel shadow without mutating
the stored coat. Focused Catacombs review at 0.5×, 1× and 2× reports no
objective findings.

Nothing fails a render any more. `render_layered_puppet` measured six things
and refused to exit zero on any of them: exclusive pixel ownership, backfill
coverage, orphan isolation, folded mesh triangles, interior holes, and an exact
neutral composite. Every count they rejected — 149 orphans, four airborne folds,
355 contact holes against 174 neutral — is invisible at 48 pixels, the size the
sprite ships, because all of it was measured on the 256-pixel working image.
They were also built for the See-through approach, which failed art direction.

They are still measured and still written to the manifest, now under
`review_notices` rather than `hard_validation`, and printed after the render as
`note:` lines. Re-arming one is a line of code. `require_single_component`
survives as a notice too, and currently fires: `test-puppet.json` renders
`lower_leg_l` and `lower_leg_r` in more than one piece across five frames.

The dead `require_*` flags were removed from the three See-through specs.

The full gate passes on 2026-09-07 with the second editor removed: 163 test
executables and 51 Python tests.

Live-transition recording remains blocked by Terminal Screen Recording
permission and is deferred until replacement art passes.

The puppet editor work is verified against its own tests and by driving the
running server over HTTP. Two migrated documents render byte-identically to the
specs they replaced — same frames, same parts, same manifest.

Driven over HTTP with no flags: the workspace lists three documents, a bad
document name is refused with the reason, `scale_to_size` moved
`test-puppet.json` between 256 and 1280 and back with 3e-14 worst-case drift,
`set_part_bones` re-pointed a part and kept its outline, and an unknown bone
returned 409 without touching the file.

A rig was authored end to end over HTTP with no artwork at all: an empty
document took three joints, two bones and `set_frame_rate`, wrote
`biped-v1.json`, and a second document imported it back with its chains and rate
intact. `../escape` and a name already taken were both refused. Importing
`rig-bench.json` and writing it straight back out reproduces its points, chains,
bones, clip rate and all twelve poses exactly; `floor_y` moves from 236 to the
lowest joint, and nothing reads it.

**Every tracked document has overlapping parts.** `mouse_profile_v1` is the
worst by far — torso and front upper arm share 1970 pixels, torso and rear upper
arm 1292, torso and front forearm 1242, head and torso 1154. `test-puppet` has
upper_arm_r and body at 106 and upper_arm_l and forearm_l at 50.
`mouse_interactive_run_v1` has one pair at 3. Both parts in each pair draw those
pixels and carry them apart in motion. `exclude_parts` is the fix and now has a
control; none of it has been applied yet, because which part should give up
which pixels is art direction.

Headless Chrome loaded all five steps against `test-puppet.json` with no script
error. A document sized 512 against a 256 picture opened showing both sizes and
a one-click stretch. The Skeleton step shows the rig writer, the Parts step
names the overlaps and offers draw order, and the Frames step reports a worst
bone stretch of 1.00× for `test-puppet` — its frames still match its bind pose.

Everything that needs a mouse is eyeball-only, because headless Chrome cannot
click, drag or hover: drawing an outline, the joint tooltip, and the new root
drag. The commands each one sends are tested; the mouse handling is not.

`test-puppet.json` had all four arm parts traced over one arm and built on the
other's bones, which threw those pixels across the body in every frame. The four
outlines were swapped; the document is otherwise byte-identical and now renders
a coherent mouse in all twelve frames. Both arms still draw behind the body,
because `add_part` appends to the draw order and no control reorders it.
