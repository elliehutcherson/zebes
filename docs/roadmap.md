# Roadmap

What is left, in the order the dependencies allow. Updated as each track closes.

This outlives any one phase. [`handoff.md`](handoff.md) is the record of the
derived terrain phase and stays that; the design documents state what a phase
decided and why. This document only says what has not happened yet.

| Track | What | State |
|---|---|---|
| 0 | Land the clang-tidy tooling and the slope rename | **Done** |
| 1 | The clang-tidy backlog | **Done** |
| 2 | Repo hygiene | **Done** |
| 3 | Terrain carry-overs | **Done** |
| 4 | Features: layers, prop artwork, zone seaming | **In progress** — layers done |

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

**Layers — done.** [`level-layers.md`](level-layers.md) records the implemented
contract. `Level` owns ordered `WorldLayer` depth slices, each with one sparse
tile grid and entity map; `Entity::sort_order` remains within-layer ordering.
The strict format and migration wrap old root collections as `Base`, editor
visibility/locking stay transient, and the viewport renders and edits one
explicit active layer while keeping parallax theme layers specialized.

**Prop artwork from a generated image** — [`prop-artwork.md`](prop-artwork.md).
Milestone 0 is accepted after boulder/cave and tree/meadow checks. Full resolved
terrain colours are the production palette policy. Milestones 1-4a now provide
the deterministic pipeline, strict source and recipe resources, compensated
bundle lifecycle, regeneration/deletion, and the imported-source Prop Artwork
tab. Its preview reuses the editor `Canvas` for rulers, pan, zoom, and Fit, and
context framing retains the complete prop texture. Uncommitted imports are
discarded on replacement, Clear, or normal shutdown; retained sources can be
deleted explicitly through the same reference checks as other assets.
Grounded, ceiling, and free/background attachment modes are persisted and feed
composition, validation, context preview, sprite offsets, and regeneration.
Provider integration remains, but the local feedback-loop milestone comes first
so provider work does not multiply an already expensive verification cycle.
Its §12 sequence:

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
4b. **Next.** Optimize the developer feedback loop. Begin with controlled
    baselines, then batch affected test targets into one build, preserve concise
    success and complete failure output, evaluate Ninja, and allow at most two
    scoped clang-tidy workers. The observed Milestone 4a session spent 5m44s in
    the affected-target sweep and 1m24s in clang-tidy even though the test bodies
    themselves took less than one second.
5. Add the cancellable image-generation service and first provider adapter.
6. Harden shutdown, retry, staging cleanup, and provider failure behavior.

**`ParallaxZone::fade_length`** — authored, serialized, validated, and ignored.
`ResolveActiveParallaxZone` returns one zone by a half-open bounds test, so every
transition is a hard cut. Making it real means returning two zones with a blend
weight and giving the parallax draw a tint parameter it does not have. Small,
self-contained, and the most visible unimplemented authored field.

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
