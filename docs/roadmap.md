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
| 4 | Features: layers, prop artwork, environment artwork, zone seaming | **In progress** — Milestone 5 zone fades, Catacombs formation coverage through 0.5×, and the first player-scaled distributed prop pass are accepted; more prop/decal variants remain |

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
complete/selected previews in a fixed logical game viewport, context travel
scrubbing over reachable camera centers, a far-to-near hierarchy, independently
scrolling inspector, collapsible measured repetition/coverage diagnostics,
strict fade geometry, and honest unsupported-fade UI.
The accepted human three-plane review proved repeat rendering and scrubbing and
exposed that Level Editor hid zone-owned theme assignment behind zero-area
level setup and selection state. Milestone 1.5 implements explicit unsaved
setup, an ownership-readable hierarchy, transactional zone creation with
searchable theme selection, a contextual inspector, resizable placement
palette, world/zone framing, and explicit parallax preview modes. Its usability
follow-up removes the false collapsible scene root, keeps Level Settings and
both owned collections visible, standardizes editable inspectors on a shared
labeled property grid, and consolidates readiness blockers behind the toolbar's
review action. The same gate established the next priority before background
processing: Far Fill and Far Formations may repeat, but distinctive Near
Formations need multiple positioned elements within one layer. Milestone 1.6
now implements finite or group-repeating compositions, deterministic migration,
culling, Theme Editor arrangement and dragging, explicit repeat-cell guides,
and adjacent/wrap seam diagnostics without moving composition into levels or
misusing zones. Its automated and human gates are accepted: the Milestone 1.6
review used a four-element, 5000-pixel group-repeating Near composition;
direct manipulation, camera navigation, persistence, the 0.5–2.0 zoom route,
and the first/last wrap were reviewed. Milestone 2 subsequently extended the
saved Near composition to five elements and an 8192-pixel repeat period while
accepting the managed cave-plate workflow. Milestone 3 accepted a live
generated candidate through retained source, deterministic processing,
recipe-owned Texture creation, in-place rename, and theme assignment. Its
usability closeout added content-derived routes for unassigned themes plus a
preview-safe Theme Editor with explicit texture application and in-place draft
discard. The evidence is recorded in
[`cave-parallax-content-gate.md`](cave-parallax-content-gate.md).

**Environment artwork vertical slice — accepted.** The Milestone 3 live gate
retained, processed, committed, reopened, renamed, and assigned a Codex
candidate without changing its IDs. Together with the accepted imported,
composed-Near, level-layer, zone-assignment, and route-scrubbing gates, that
completes the end-to-end engineering slice. Provider failure and compensated
persistence remain covered at their platform-neutral boundaries.

**Environment artwork zone fades — accepted.** The platform-neutral resolver,
strict unsupported-geometry validation, explicit renderer opacity, editor
authoring, preview composition, and weight readout are implemented. The live
`Cave` review accepted the asymmetric Webbed Gallery → Ossuary Descent blend at
their saved boundary and confirmed that Selected Zone isolates either theme.
The exact geometry and weights are recorded in
[`zone-fades-plan.md`](zone-fades-plan.md).

**Production Catacombs baseline — horizontal and 0.5x formation coverage
accepted.** Catacombs Processional is the versioned production level and theme. Its Near
strip uses four 960-pixel formations with 96-pixel neighbour and wrap overlaps;
Webbed Ceiling, Skull Pillars, and Ossuary Ridge were redrawn against their
actual adjacent sources while preserving all managed IDs. Complete-theme route
reviews at 0.5×, 1×, and 2× support the accepted horizontal pass. Alternating
Far lower-foundation and Near lower-wall/rubble companions now extend both
finite formation layers through the complete 0.5× viewport without changing
the accepted upper composition at 1× and 2×. The broader cave prop/decal kit
remains independent content work rather than a reason to weaken the generation
pipeline. This pass has a
first-party headless generation and review loop: the shared `AssetWorkspace`
loads the same catalogs as the editor; `generate_assets` publishes atomic,
strict new-asset candidates through OpenAI, Codex, or a credential-free fake;
and `curate_assets` emits deterministic evidence for parallax artwork/themes,
props, sprites, terrain, and tilesets. Generated pixels retain a new source and
commit through the existing compensated creation transaction. Existing assets
support settings-only recipe regeneration and a guarded parallax source-redraw
candidate. Redraw preserves source, recipe, and texture identities while
advancing source provenance and both pixel digests together; transparent
formations also report lateral-gutter occupancy and warn on hard exposed
edges. Provider-backed redraw now uses the retained reference image directly
and refuses stale candidates. Complete environments are authored through a
versioned name-resolved specification and generic builder; asset-root shared/
exclusive locks make catalog loads and commits safe across concurrent agents.
The production Catacombs Processional spec replaces its one-off C++ authoring
program and now owns strict, name-resolved Blueprint placements with stable
local entity IDs. A Back Decor Cave Crystal proves the idempotent path, and the
first generated Catacombs-specific piece, an Ossuary Reliquary, now exercises
external-image staging, deterministic prop review, compensated creation, and
production placement. A Funeral Brazier now exercises the complete
`generate_assets --provider=codex` path with template-derived portrait
composition and supplies the first Front Decor placement.

The 2026-08-28 production route review keeps the broader prop/decal variation
gate open. Its initial bundle exposed two finite-layer coverage gaps at 0.5×. The
follow-up lower-companion pass resolved both findings and retained all supported
zooms; its 343-artifact bundle reports no objective findings. Visual review
rejected a one-image-per-layer draft because of obvious stamp cadence, then
accepted distinct A/B companions in both Far and Near layers. A subsequent
scale-aware content pass establishes a static Mouse Player Placeholder with an
exact 32×64 collider, adds low/wide floor debris, ceiling friezes, and sparse
foreground shrouds, and distributes decor through x=14,912 of the 16,384-pixel
route. A second A/B pass replaces two repeated floor scatters with Fallen
Votive Tablets and one repeated foreground shroud with a Tattered Valance while
keeping entity and collider counts fixed. Production review rejects two wall
plaque drafts because they resemble interactables at gameplay scale. The
generation and staging commands now support validated prop canvas and
grounded, ceiling, or explicit free-anchor overrides instead of forcing every
new asset through the old 1×2 portrait template shape.
The persisted integrated-level reviewer now reproduces production parallax,
tile, and entity composition without SDL or ImGui, sampling authored-content
tracks at 0.5×, 1×, and 2× and publishing native frames, per-track contact
sheets, isolated passes, a layout map, and objective findings. Large bundles
stream through atomic staging instead of retaining every decoded frame; the
Catacombs bundle peaks at about 174.5 MiB while preserving the same 343
artifact digests. Its first
persisted Catacombs bundles expose the resolved formation coverage and current
prop distribution directly. The final A/B pass still reports no objective
findings. Catacombs Processional now also owns an independent, deterministic
`Catacombs Masonry` terrain bundle instead of borrowing `lucinda_cave`; Cave and
Donut Plains retain that shared material unchanged. The first masonry draft was
rejected because its collision mass read as a flat editor mask at 0.5×. The
accepted revision adds restrained three-phase stone relief and a clearer rim
without the cave material's bright repeated crystals. Its 425-artifact terrain
review and rebuilt 343-artifact level review report no findings, repeat builds
are byte-identical, and normalized level geometry and collision shapes match
the prior level exactly. A subsequent managed `generate_assets` pass explored
subdued wall weathering. Production review rejected both focal relief drafts
and the quieter damp-stain variants: the former read as pasted slabs, while
world-space Back Decor stains drifted across the parallax masonry and crossed
bright arches at some camera positions and zooms. None ships. Any future wall
aging must be baked into the relevant parallax artwork or rendered in a
parallax layer with the same scroll factor as its supporting wall; avoid
increasing density merely to fill the route. Focused entity review now provides
one annotated camera at 0.5×, 1×, and 2× plus isolated passes, reducing current
Catacombs placement iteration from 343 artifacts to 31 without replacing the
full-route final gate. Command timing is reported separately so manifests stay
byte-deterministic.
See
[`headless-level-review-plan.md`](headless-level-review-plan.md) and
[`headless-curation.md`](headless-curation.md).

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
builder. Theme extraction, separate editor ownership, the imported layered cave
composition, and retained background recipes are implemented and accepted.
Milestone 3 shares generation-request and candidate-review ownership between
Prop and Parallax Artwork and exposes generated parallax candidates through
the same retained-source path; its automated and live human gates are accepted.
Milestone 4 now includes the usable horizontal Catacombs Processional baseline
and continues as an independent vertical-space and prop/decal content pass.
Milestone 5 zone fades pass both their automated and live visual gates. The
document is also the source of truth for migration, validation, and the human
authoring workflow.

**`ParallaxZone::fade_length`** — authored, serialized, editable, intrinsically
and cross-zone validated, and rendered through the two-theme compositor.
`ResolveParallaxEnvironment` preserves the half-open active zone while
returning a stable primary/secondary pair and weight across supported exact
shared edges. The supported two-theme fade contract lives in
[`environment-artwork-plan.md`](environment-artwork-plan.md); its detailed
implementation sequence is [`zone-fades-plan.md`](zone-fades-plan.md).

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
