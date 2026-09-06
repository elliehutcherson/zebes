# Repository cleanup

Closed 2026-09-06. Nine commits, about 7,000 lines removed, driven by a survey
of debt left behind by the animation experiments.

## What was removed and why

**`scripts/migrate_definitions.py`** — 919 lines plus a 1,141-line test. All
twelve migrations had run; `--dry-run` reported every definition current.
Details in [`definition-migrations.md`](definition-migrations.md).

**The generated-animation experiment** — 3,777 lines across
`animation_artwork_feasibility`, `pose_conditioned_animation_batch`,
`animation_artwork_spike`, `animation_artwork_run_manifest`,
`run_pose_conditioned_animation` and their tests. The roadmap already recorded
the experiment as closed. The only piece worth keeping, frame processing, was a
weaker duplicate of `animation_frame_set_pipeline`: it required square source
cells and never compared them to the output canvas, so it passed a square cell
drawn onto a 48x24 canvas and rejected a 100x50 cell drawn onto a 100x50 one.
The pipeline compares the two shapes, which is the real condition.

**The skeleton-conditioning tooling** — 772 lines. See
[`codex-pose-conditioning-2026-09-06.md`](codex-pose-conditioning-2026-09-06.md).

**Nine unreferenced sprite definitions** — eight `kGrass*` variants of the old
sunny tileset and `kSamusJumpingLeft`, all from the 2025-12-14 placeholder set.

## What was consolidated

**Strict JSON readers.** `Required<T>` had been copy-pasted into thirteen files
and `RequireExactObject` into ten, in three incompatible signatures. Both now
live in `src/common/json_schema.h` under `zebes::json_schema`, which lets a
reader pull them in with `using` or wrap them to bind a fixed context without
colliding with a same-named helper in its own anonymous namespace. Error text
is unchanged except where the old copies were wrong.

**The puppet editor page.** 850 lines of HTML, CSS and JavaScript had been one
raw string literal inside `layered_puppet_editor.cc`, checked by nothing. The
page is now `src/artwork/layered_puppet_editor.html`, embedded at build time by
`cmake/embed_text_file.cmake` so the tool stays one binary.

**Inputs and output under `experiments/character_binding/`.** `out/` held both
regenerable renders and irreplaceable source art, which cost 49 lines of
`.gitignore` allowlist. Tracked inputs moved to `inputs/`, closed-experiment
records to `evidence/`, and `out/` is ignored whole in one line.

## Two real bugs found on the way

`EngineConfig::Load` returned `absl::StatusOr` but let nlohmann exceptions
escape, so a truncated `assets/config.json` terminated the editor instead of
returning an error. It now catches at the adapter, where the style guide says to
translate.

`TextureEditor::RenderImport()` was declared in the header and never defined
anywhere; calling it would not have linked.

## One design change

`previous_sprite_id` recorded what each Blueprint state pointed at before a
recipe bound its own Sprite. Its only reader was the importer's failure
rollback, which happens inside one run — two seconds of transaction state
written into the shipped asset format, pinning old placeholder sprites forever
because a rollback target had to stay loadable. The importer now holds the prior
IDs in memory for the length of the run. `blueprint_bindings` became
`blueprint_state_keys`.

Regeneration had used the stored IDs for one case: moving a frame set onto a
different Blueprint state, which needs to know what the old state held. That is
now refused with a message saying to delete and re-import, rather than guessing.
