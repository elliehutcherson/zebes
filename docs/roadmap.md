# Roadmap

What is left, in the order the dependencies allow. Updated as each track closes.

This outlives any one phase. [`history/handoff.md`](history/handoff.md) records
the earlier implementation handoffs; completed design records live under
[`history/`](history/README.md). Active design documents state what a current
phase has decided and why. This document only says what has not happened yet.

| Track | What | State |
|---|---|---|
| 0 | Land the clang-tidy tooling and the slope rename | **Done** |
| 1 | The clang-tidy backlog | **Done** |
| 2 | Repo hygiene | **Done** |
| 3 | Terrain carry-overs | **Done** |
| 4 | Features: layers, prop artwork, environment artwork, zone seaming | **In progress** — imported parallax authoring next |

Track 0 merged through PR #1. CI now compiles one UI-enabled test tree and runs
the headless, SDL/ImGui, and Python suites from that single build.

---

## Track 0 — Tooling and the slope rename (done)

clang-tidy runs through `scripts/lint.sh`. The wrapper finds the keg-only
Homebrew LLVM installation, supplies the macOS SDK, and requires either named
translation units or an explicit `--all`. Full scans are capped at two workers
by default. An anchored source expression and the header filters keep the
output about this project — `HeaderFilterRegex` alone was not enough, because
stb is reached as `src/common/../../include/stb/stb_image.h` and so matched
`/zebes/src/`. That one path was 741 of 961 findings before the filters.

Local verification is now target-oriented through `scripts/test.sh`; GitHub
Actions owns the comprehensive headless, UI, and full-tree analysis runs.

Then the first thing it found got fixed, twice over:

- The slope enumerators lost the underscore before their final segment
  (`kGentleSlopeBottomLeft_Lower` → `kGentleSlopeBottomLeftLower`).
- All twenty then moved to a vocabulary that says which side is tall rather
  than which side is thin (`kSlope45BottomLeft` → `kSlope45FloorTallRight`).

Both reach disk, because a derived tile is named `"<terrain> <shape
identifier>"`. `scripts/migrate_definitions.py` carries a block for each, chained
so a definition from either era arrives current in one pass.

The naming choice worth remembering: the new names share no spelling with the
old ones. Renaming each shape to its right-angle corner — the obvious fix —
would have swapped `kSlope45BottomLeft` and `kSlope45BottomRight`, and a
definition that escaped the migration would then have loaded as the mirrored
shape in silence, since the numeric `shape` field is untouched and
`TileShapeFromIdentifier` would still have resolved it.

---

## Track 1 — The clang-tidy backlog (done)

The baseline, with vendored code excluded and repeated diagnostics deduplicated
by location and check, was **220 findings in `src/`, 86 in `tests/`**. The tree
is now clean under `scripts/lint.sh --strict --all`; its raw output can repeat a
header finding for multiple translation units.

| Count | Check | Where |
|---|---|---|
| 153 | `google-default-arguments` | **Done** — `gui_interface.h`, `gui.h`, `gui.cc` |
| 28 | `google-readability-casting` | **Done** — `sprite_editor.{h,cc}`, `canvas_sprite.cc`, `canvas.cc` |
| 13 | `google-explicit-constructor` | **Done** — RAII conversions and constructors are explicit |
| 8 | `readability-identifier-naming` | **Done** — motif-table aliases retained with a scoped rationale |
| 6 | `google-readability-braces-around-statements` | **Done** |
| 4 | `readability-convert-member-functions-to-static` | **Done** |
| 6 | runtime-float / runtime-int / todo | **Done** |

**1. `GuiInterface`'s virtual defaults (153) — done.** A default argument on a
virtual method binds statically to the declared type, so a call through
`GuiInterface&` and a call through `Gui&` could pass different values for the
same omitted argument. Defaults now live in non-virtual convenience overloads
that forward to full-arity virtual methods. A regression test exercises the
forwarding through `GuiInterface&`, and a compile-time check keeps the same
convenience surface on concrete `Gui`.

**2. Mechanical (45) — done.** C-style casts, braces, static members, and int
and float widths are clean. The final scan also caught newer findings in test
support and editor utility code; those were fixed in the same sweep.

**3. Judgement calls (22) — done.**
- `imgui_scoped.h`'s conversions are explicit. Contextual conversion still
  supports the RAII-guard idiom (`if (ScopedCombo c = ...; c)`) without allowing
  unrelated implicit conversions.
- `terrain_motifs.cc`'s `T`/`A`/`D` constexpr aliases remain so each motif table
  reads as pixel art. A narrow `NOLINT` block records that exception.
- `api.h`, `db.h`, and `camera_controller.h` single-argument constructors are
  explicit.

**4. `tests/` (86 unique findings) — done.** All 79 affected GoogleTest names
were renamed in one sweep. No repository-owned filter referenced the old names.

**5. Enforcement — done.** `scripts/lint.sh` provides scoped local checks and
GitHub Actions runs `scripts/lint.sh --strict --all`, so new findings fail the
merge gate.

---

## Track 2 — Repo hygiene (done)

The unreferenced root scratch files (`check_test.cc`, `test_issue.cc`, and
`test_output.txt`), obsolete `old/` tree, and superseded `notes/prompts.txt`
were removed. A repository `.ignore` keeps default searches out of vendored,
generated, and non-normative trees without changing Git's tracking behavior.

---

## Track 3 — Terrain carry-overs (done)

The phase is merged and the editor walk is done, so nothing here blocks layers.

1. **The Autumn Forest visual check passed.** Wall darkness is a bounded blend
   toward the authored outline colour, and the 1.2 preset was accepted in the
   live editor on 2026-08-16.
2. **Terrain generation is off the render thread.** Create and Regenerate use
   the reusable `common/BackgroundTask` boundary to render platform-neutral
   output from copied inputs on a worker, show an in-progress status, and commit
   resource-manager and GPU state on the editor thread. A regeneration commit
   refuses a stale tileset snapshot rather than overwriting derived tiles
   appended by a level save while rendering was in flight.
3. **Atlas compaction does not exist.** Deliberate: reclaiming fragmented tiles
   renumbers IDs that levels already name, so it has to be an explicit tool
   rather than something that happens on its own. Leave it until an atlas is
   uncomfortably large.

---

## Track 4 — Features

**Layers — done.**
[`history/level-layers.md`](history/level-layers.md) records the implemented
contract. `Level` owns ordered `WorldLayer` depth slices, each with one sparse
tile grid and entity map; `Entity::sort_order` remains within-layer ordering.
The strict format and migration wrap old root collections as `Base`, editor
visibility/locking stay transient, and the viewport renders and edits one
explicit active layer while keeping parallax theme layers specialized.

**Standalone parallax themes — implemented.** `ParallaxThemeManager` now owns
string-identified resources outside levels. Zones retain only stable theme IDs;
the dedicated Theme Editor owns theme/layer drafts, while Level Editor owns zone
assignment, contextual preview, Edit Theme, and Duplicate and Assign. The
deterministic migration extracted all shipped embedded themes without
deduplication, and catalog validation blocks missing references and referenced
deletion. Milestone 1 of
[`environment-artwork-plan.md`](environment-artwork-plan.md) now has its
imported-authoring tooling: depth presets, live searchable texture thumbnails,
complete/selected previews, context travel scrubbing, measured repetition and
coverage diagnostics, strict fade geometry, and honest unsupported-fade UI.
The human three-plane review remains pending in
[`cave-parallax-content-gate.md`](cave-parallax-content-gate.md); its evidence
must precede background-processing defaults.

**Prop artwork from a generated image** — [`prop-artwork.md`](prop-artwork.md).
Milestone 0 is accepted after boulder/cave and tree/meadow checks. Full resolved
terrain colours are the production palette policy. Milestones 1-4a now provide
the deterministic pipeline, strict source and recipe resources, compensated
bundle lifecycle, regeneration/deletion, and the imported-source Prop Artwork
tab. Its preview reuses the editor `Canvas` for rulers, pan, zoom, and Fit, and
context framing retains the complete prop texture. Finished context props can
be dragged transiently along their valid terrain surface without changing the
recipe; the no-jump gesture state is shared with Level Editor entity movement.
Uncommitted imports are
discarded on replacement, Clear, or normal shutdown; retained sources can be
deleted explicitly through the same reference checks as other assets.
Grounded, ceiling, and free/background attachment modes are persisted and feed
composition, validation, context preview, sprite offsets, regeneration, and
Level Editor origin snapping without canceling the authored render offset.
Legacy entity positions can be migrated explicitly from the Level Editor
inspector with an idempotent nearest-anchor resnap; schema migration never
silently rewrites authored level composition.
Blueprint selection now uses a searchable, sorted thumbnail grid with explicit
placeholders for assets whose artwork cannot yet be previewed. Selection and
filtering live in a platform-neutral stable-ID model, while blueprint, tile,
and terrain palettes reuse common grid and item-frame presentation. Blueprint,
Level, Prop Artwork, and Sprite editors also reuse one anchor-gizmo geometry and
rendering path for origin and attachment-surface feedback. Tile and terrain
palettes share a stable-ID tileset selector instead of duplicating combo logic
and retaining authoritative resource pointers.
The provider-neutral generation, credential, and bounded HTTP contracts, the
poll-driven libcurl transport, the session-lifetime generation engine, the
first provider adapter, and the generated-source editor flow are all
implemented. `ImageGenerationService` assembles and owns that stack for the
process; a generated candidate reaches `SelectSource` through the same
retention the imported path uses. The editor now composes the Codex and OpenAI
providers independently and offers runtime selection without making either a
startup requirement. Live Codex startup, authentication, skill discovery, and
generation are confirmed; the corrected Codex-cache decode plus review and
acceptance flow remain. Also outstanding are the credential-gated opt-in
OpenAI integration run and Milestone 6 hardening. The local feedback-loop
milestone is complete, so provider work can proceed
without multiplying an expensive verification cycle. Its §12 sequence:

0. **Accepted.** Run the visual feasibility spike:
   one imported boulder, one real terrain recipe, the deterministic C++ stages,
   and three palette policies. Do not build persistence, UI, or provider
   infrastructure unless an in-context result looks like production-quality
   Zebes art.
1. **Implemented.** Keep the accepted shared palette and image primitives.
2. **Implemented.** Harden the deterministic stage coordinator and diagnostics.
3. **Implemented.** Add resources and compensated bundle lifecycle.
4. **Implemented.** Prove the imported-source editor workflow, including shared
   Canvas navigation and unclipped context framing.
4a. **Implemented.** Persist grounded, ceiling, and free/background attachment
    modes, with a version-1 grounded migration.
4b. **Implemented.** Affected tests now configure once, build all 28 selected
    executables in one invocation, run each once, and keep complete failure
    logs behind concise success output. Warm time fell from 87.59s to 73.24s;
    the controlled source-touch case was effectively unchanged at 97.83s versus
    97.06s because compile/link work dominates. Two-worker scoped clang-tidy cut
    the same 18-file check from 84s to 51.36s. Focused orchestration tests cover
    the success and failure contracts. Ninja reduced the real full warm cycle
    to 6.32s and the source-touch cycle to 22.23s. Focused `dev` and `ui` builds
    retain two workers. Comprehensive `dev-full` and `ui-full` builds expose
    eight workers while a two-worker Ninja link pool protects post-link
    GoogleTest discovery; a clean 1,081-action UI build completed in 5m49.7s
    without a discovery timeout. The comprehensive test runner now launches
    each CTest-registered executable once: 96 headless executables take about
    12s, while a warm complete UI-enabled wrapper run takes about 37s including
    configure, build checks, 99 C++ executables, and 79 Python tests. The former
    CTest C++ path alone launched 1,003 cases over 62–75s. Apple ld debug-speed
    flags slightly regressed the source-touch cycle. Ccache made a 2.30s compile
    a 0.03s hit, but linking limits that to about 10% of the focused loop, so
    caching remains in CI without becoming a required local dependency.
5. **Implemented.** The cancellable image-generation service, move-only
   environment credential source, bounded HTTPS seam, poll-driven libcurl
   transport, session-lifetime `ImageGenerationEngine`, the first provider
   adapter (OpenAI `gpt-image-2`), `ImageGenerationService` as the composition
   root's owner of the whole stack, and the editor's prompt, candidate review,
   and acceptance controls are all in. A generated candidate is retained
   exactly as an imported PNG is, so both reach `SelectSource` the same way.
   Remaining: an opt-in live integration test, which is the only thing that
   will exercise the curl negative-timeout branch.
6. Harden shutdown, retry, staging cleanup, and provider failure behavior.

**Environment artwork and parallax** —
[`environment-artwork-plan.md`](environment-artwork-plan.md). The accepted plan
keeps camera-relative parallax layers distinct from world-relative background,
gameplay, and foreground prop layers. It reuses the existing generation,
retained-source, palette, texture, API-compensation, and reference-scan
boundaries while adding a background-specific recipe and deterministic output
builder. Theme extraction and separate editor ownership are implemented. Next
it validates one imported layered cave composition, then adds retained
background recipes, exposes generated candidates, and finally implements zone
fades. The document is also the source of truth for migration, validation, and
the human authoring workflow.

**`ParallaxZone::fade_length`** — authored, serialized, exposed in the editor,
and ignored. It is not currently checked by `ValidateLevel`, despite an earlier
version of this roadmap calling it validated. `ResolveActiveParallaxZone`
returns one zone by a half-open bounds test, so every transition is a hard cut.
The supported two-theme fade contract, validation, and implementation sequence
now live in [`environment-artwork-plan.md`](environment-artwork-plan.md).

**Smaller, already recorded:**

- `viewport_model.h:81` — a linear scan where a spatial index belongs, when
  level size requires it.
- [`architecture.md`](architecture.md) §Transient texture previews —
  `TextureEditor` owns a raw preview `SDL_Texture*` because an imported preview
  is not a managed engine resource.
- Tile deletion is guarded by `Api::CheckTileDeletable` but has no
  `ConfirmPrompt`, unlike every other destructive control. The refusal covers the
  referenced case, so this is consistency rather than safety.

---

## Deliberately settled

Decided, with reasons, and not to be reopened without a new one:

- **Remote polling is bounded, not socket-driven.** Waking exactly when a
  response arrives means `curl_multi_socket_action` and registering transport
  sockets as they appear, which requires mutating a notification set while a
  thread is armed on it — the one thing sealing exists to prevent. The engine
  sleeps until curl's own timer instead, capped so a transfer is never stalled
  behind its total timeout. It costs a fraction of a second on requests that
  take tens.
- **A wake deadline bounds sleeping; it is not a wake source.** Every source
  that can notify still needs a `Notification` in the set. The deadline exists
  only for sources that cannot have one, such as a transfer with no registered
  descriptor or a timestep that is due whether or not anything notifies.
- **Generated sources arrive opaque.** `gpt-image-2` rejects transparent
  backgrounds, and rather than pick a weaker model for real alpha, isolation
  removes the background — the same path every imported source already takes.
  This makes generated and imported sources converge more completely, not less.
- **Deduplication is by exact pixel content**, not by a rule about which keys
  collide. An approximate comparison needs a threshold, and a threshold is a
  claim about how much difference the eye forgives.
- **A derived terrain has no rule table.** Resolving by mask is the lossy step
  that the phase existed to remove.
- **`TerrainScheme` is a tagged union**, not a record with optional fields.
- **Painting writes one cell.** Stamping a two-cell unit would make a real
  arrangement unreachable.
- **There is no bare "delete recipe" button.** It would always succeed and would
  leave a tileset nothing can regenerate.
- **Slopes ignore `variant_period`** for hand-drawn terrain. A derived terrain's
  key carries the phase, so it is fixed there.
- **Edge motifs inherit the material's surface palette** and have no tint of
  their own; short grass and snow favour upward-facing edges while moss may
  continue onto walls. Neither should be fixed by overloading an existing
  control — an edge palette or a facing policy would be explicit recipe state.
