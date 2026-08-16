# Roadmap

What is left, in the order the dependencies allow. Updated as each track closes.

This outlives any one phase. [`handoff.md`](handoff.md) is the record of the
derived terrain phase and stays that; the design documents state what a phase
decided and why. This document only says what has not happened yet.

| Track | What | State |
|---|---|---|
| 0 | Land the clang-tidy tooling and the slope rename | **Done** |
| 1 | The clang-tidy backlog | Next |
| 2 | Repo hygiene | Next |
| 3 | Terrain carry-overs | After 1-2 |
| 4 | Features: layers, prop artwork, zone seaming | After 3 |

---

## Track 0 — Tooling and the slope rename (done)

clang-tidy runs. It ships with the keg-only Homebrew llvm formula, so it was
never on `PATH` and the config said so; the working invocation and the reason
`-isysroot` cannot be dropped now live in `.clang-tidy` itself. Two filters keep
the output about this project — `HeaderFilterRegex` alone was not enough,
because stb is reached as `src/common/../../include/stb/stb_image.h` and so
matched `/zebes/src/`. That one path was 741 of 961 findings.

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

## Track 1 — The clang-tidy backlog

Measured with vendored headers excluded, in-scope checks only: **220 findings in
`src/`, 200 in `tests/`**. Re-measure with the command in
[`style-guide.md`](style-guide.md) §Verification.

| Count | Check | Where |
|---|---|---|
| 153 | `google-default-arguments` | `editor/gui_interface.h`, `gui.h`, `gui.cc` — 51 each |
| 28 | `google-readability-casting` | `sprite_editor.cc` 15, `canvas_sprite.cc` 10, `canvas.cc` 2 |
| 13 | `google-explicit-constructor` | `imgui_scoped.h` 10, `camera_controller.h`, `db.h`, `api.h` |
| 9 | `readability-identifier-naming` | `terrain_motifs.cc` 8, `sprite_editor.cc` 1 |
| 6 | `google-readability-braces-around-statements` | `sprite_editor_model.cc` 5 |
| 5 | `readability-convert-member-functions-to-static` | spread across the editor |
| 6 | runtime-float / runtime-int / todo | `viewport_model.cc` 4, two others |

**1. `GuiInterface`'s virtual defaults (153).** The one item here that is a
defect rather than a cleanup. A default argument on a virtual method binds
statically to the declared type, so a call through `GuiInterface&` and a call
through `Gui&` can pass different values for the same omitted argument. `MockGui`
is what makes that reachable: tests observe the interface, the editor calls the
concrete type. Fix by hoisting each default into a non-virtual overload that
forwards to a full-arity virtual, which also shrinks what a mock implements.

**2. Mechanical (45).** C-style casts, braces, static members, int and float
widths. One commit per file group.

**3. Judgement calls (22), decided rather than mass-fixed.**
- `imgui_scoped.h`'s implicit conversions are the RAII-guard idiom
  (`if (ScopedCombo c = ...; c)`). Keep, with `NOLINT` and the reason.
- `terrain_motifs.cc`'s `T`/`A`/`D` constexpr aliases exist so a motif table
  reads as a picture. Keep with `NOLINT`, or rename if the table survives it.
- `api.h`, `db.h`, `camera_controller.h` single-argument constructors: mark
  `explicit` unless the conversion is wanted.

**4. `tests/` (200).** 102 are the same `GuiInterface` defaults seen from the
other side and disappear with step 1. The real item is **79
`google-readability-avoid-underscore-in-googletest-name`**, concentrated in
`tileset_manager_test.cc` (23), `level_manager_test.cc` (16) and
`api_validation_test.cc` (15). Renaming a test changes what `--gtest_filter`
matches, so sweep it once rather than alongside other edits.

**5. Then decide enforcement.** A `scripts/lint.sh`, or a `--lint` flag on
`build_and_test.sh`. A clean tree that nothing runs goes stale in a week.

---

## Track 2 — Repo hygiene

`check_test.cc`, `test_issue.cc`, `test_output.txt` and `old/`
(`level_panel.{h,cc}`, `level_panel_test.cc`, `rules-0.md`) are tracked in the
repo root and are not part of the project. Confirm no `CMakeLists.txt` names
them, then delete.

---

## Track 3 — Terrain carry-overs

The phase is merged and the editor walk is done, so nothing here blocks layers.

1. **The Autumn Forest visual check.** Wall darkness became a bounded blend
   toward the authored outline colour and the preset was retuned 1.8 → 1.2.
   Tests pin both endpoints; only looking at it can say whether it is right.
2. **`Create` blocks for seconds** with no progress indication — it renders all
   47 masks per phase on the render thread. The fix is moving generation off
   that thread. Largest remaining terrain debt.
3. **Atlas compaction does not exist.** Deliberate: reclaiming fragmented tiles
   renumbers IDs that levels already name, so it has to be an explicit tool
   rather than something that happens on its own. Leave it until an atlas is
   uncomfortably large.

---

## Track 4 — Features

**Layers.** An ordered list of depth slices, each holding its own tile grid and
its own entities. `Level` (`src/objects/level.h`) holds one `tile_chunks` map
and one `entities` map directly today; both move inside a layer.
`Entity::sort_order` was deliberately named for within-layer ordering, so it
becomes the tiebreaker rather than being renamed. A format change, so it needs a
`migrate_definitions.py` block wrapping the existing grid as layer 0. This is
what makes a canopy the player walks under possible, and
[`prop-artwork.md`](prop-artwork.md) §7 names it as the blocker there too.

**Prop artwork from a generated image** — [`prop-artwork.md`](prop-artwork.md),
design only, nothing built. Its §6 sequence; steps 1-3 need no format change:

1. Expose the resolved palette from `terrain_generator` (`BuildPalette` is
   file-local, `Colorize` private). Pure addition.
2. The Python quantiser, tested standalone.
3. Sheet packing and a manifest, mirroring `blob47_compose`'s split.
4. `PropRecipe` and its manager, mirroring `TerrainRecipeManager`.
5. An editor tab, mirroring the Terrain Editor.

Landing 1-3 gives usable art before committing to the format change in 4-5.

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
