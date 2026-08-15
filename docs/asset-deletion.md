# Deleting an asset something else might be using

**Status: analysis, not implemented.**

The editor can delete a tileset, a level, a sprite, a collider and a blueprint.
It cannot delete a texture or a terrain recipe at all: `Api::DeleteTexture`
(`src/api/api.h:59`) and `Api::DeleteTerrainRecipe` (`:107`) both exist and no UI
calls either. The only `DeleteTexture` call anywhere in the editor is a rollback
path when recipe creation fails (`terrain_creation.cc:140`).

None of the deletes that do exist check whether anything still references what
they are removing.

## 1. What actually references what

Measured, not assumed:

| Asset | Named by | Where |
|---|---|---|
| **Texture** | `Tileset::texture_id` | `tileset.h:326` |
| | `Sprite::texture_id` | `sprite.h:31` |
| | `ParallaxLayer::texture_id` | `level.h:26`, inside `Level::themes` |
| **Tileset** | `Level::tileset_id` | `level.h:64` |
| | `TerrainRecipe::tileset_id` | `terrain_recipe.h:18` |
| **Sprite** | `Blueprint::State::sprite_id`, `Entity::sprite_id` | |
| **Collider** | `Blueprint::State::collider_id`, `Entity::collider_id` | |
| **Blueprint** | `Entity::blueprint_id` | |
| **Tile** | `Level::tile_chunks`, as a bare `int` | `level.h:80` |

Two entries on that list are not assets at all:

- **A terrain is a member of a tileset**, `std::vector<Terrain> terrains`
  (`tileset.h:338`), with an ID unique only within it. Deleting one already
  exists in the Tileset Editor (`tileset_panel.cc:277`) and is a within-tileset
  edit, not an asset deletion.
- **A tile is likewise a member**, and a level stores its ID as a bare integer
  with no tileset qualifier. This is the most weakly protected reference in the
  system and the one most easily broken.

## 2. A dangling reference blanks the level, it does not degrade it

This is what decides the whole design. `ComposeLevelTileRenderBatch` returns
`InvalidArgumentError("level references unknown tile ID: N")` and aborts the
entire frame's scene when a level names a tile the tileset no longer has.
`ComposeParallaxRenderBatch` fails the same way for a missing texture.

So a dangling reference does not leave a hole or a placeholder in an otherwise
working level. It stops the viewport rendering anything at all.

Note the deliberate contrast: an **empty** `texture_id` on a parallax layer is a
valid, incomplete authoring state and is simply omitted. Empty and dangling are
different, and only dangling is the failure. Blocking a delete prevents dangling
without touching anyone's ability to leave a layer unfinished.

## 3. Are a terrain and its texture "the same thing"?

Not as a type rule, and the counterexamples are already on disk.

A texture is referenced by three different kinds, not one. `sunny-props.png`
backs a sprite; `sky_mountains_background` backs a parallax layer; a tileset
atlas backs a tileset. "A texture belongs to its tileset" is false for two of the
three.

But the intuition behind the question is right, and it has a precise form. What
`Create` produces is not a texture and a tileset that happen to reference each
other — it is **one build product with three records**: the artwork, the tileset
that indexes it, and the recipe that generated both. `TerrainRecipe` already
records exactly that binding: `tileset_id`, `texture_id`, `terrain_id`
(`terrain_recipe.h:15-23`).

So the axis is **provenance, not type**. A texture that a recipe names is derived
artwork owned by that recipe. A texture that was imported is authored input that
anything may reference. The format already distinguishes them; nothing reads the
distinction yet.

## 4. The rule

Co-delete only when both hold:

1. **Recorded provenance** — a `TerrainRecipe` names this tileset *and* this
   texture, so the three were created together as one unit.
2. **Verified exclusivity** — a full referrer scan finds no referrer from outside
   that bundle.

Then the three go together, because deleting the tileset alone would strand
artwork nothing can reach and a recipe pointing at a tileset that is gone.

If provenance holds but exclusivity does not — someone pointed a sprite or a
parallax layer at the generated atlas — **block, and name the referrer.** Do not
cascade, and do not delete the tileset while leaving the texture: the user
pointed something at it deliberately, and the bundle stopped being a bundle.

If there is no provenance record, delete only what was asked for, after the same
exclusivity check.

The discipline here is the one the terrain phase settled on for deduplication:
**do not encode a rule asserting what must be true, check it.** "A generated
texture belongs to its tileset" is a claim about how an asset was made. A referrer
scan is a fact about how it is used now. The first would need re-proving every
time someone found a new way to reference a texture; the second cannot go stale.

## 5. What blocking should say

The user needs to know what to go and change, so a refusal names each referrer by
kind, display name, ID, and the field that holds the reference:

```
Cannot delete texture 'lucinda_cave'. 3 things reference it:
  Tileset  'lucinda_cave'   ad25d175…   texture_id
  Sprite   'cave_crystal'   6d87d6ea…   texture_id
  Level    'Donut Plains'   a92525fe…   theme 'SkyBackground', layer 'Clouds'
```

The parallax case has to name the theme and layer, not just the level: a level
may hold many themes and a "Level references this" message would leave the user
hunting.

## 6. Where the scan lives, and why it can be trusted

Only `Api` holds every manager, so the query belongs there —
`Api::FindReferrers(id)` returning a list of `{kind, id, display_name, field}`.
Panels render the refusal; no manager learns about any other manager.

The scan is only sound because **`LoadAll*` reports the files it could not read**.
Every `LoadAll*` used to swallow per-file failures and return OK, so a definition
the editor could not parse vanished from the catalogue — and a referrer scan over
a catalogue with silent holes would cheerfully approve deleting something still
referenced by the file that failed to load. That fix (recorded in
`handoff.md`) is a precondition for this feature, not merely adjacent to it.

## 7. `DeleteTexture` also has to delete the PNG

Today it removes the JSON definition and leaves the image
(`texture_manager.cc:320-338`). That produces exactly the state
`scripts/README.md:10-14` says the `source_art` / `textures` split exists to
prevent — a file in `assets/textures/` with no definition. Three already exist:
`arrow.png`, `samus.png`, `sky_clouds_background_1280x720.png`.

Deleting the image is safe because it is never the only copy. `CreateTexture`
copies the source file into `assets/textures/` (`texture_manager.cc:204-215`), so
an imported texture's original is wherever the user imported it from, and a
generated one is reproducible from its recipe.

## 8. The bigger hole is tiles, not textures

`DeleteSelectedTile` and `DeleteTerrain` (`tileset_editor_model.cc:281`, `:223`)
check nothing, and per §2 a level naming a deleted tile stops rendering entirely.

Deleting a tile is also the one destructive action deliberately left
unconfirmed. The stated reason is that it can be trivially recreated — "a tile is
an Add and a click away". That reasoning does not survive contact with a level:
`NextTileId()` is max+1 (`tileset_editor_model.cc:168-172`), so re-adding gives a
**new** ID, while the level still names the old one. The tile is recoverable; the
reference is not.

So tile deletion needs the same referrer check, and once a level has painted a
tile it needs confirmation too. Deleting a *terrain* is milder — the tiles survive
and keep rendering, and only the brush's ability to re-resolve them is lost — but
it should still say what it is about to disconnect.

## 9. Sequence

1. `Api::FindReferrers` plus its tests. No UI, no behaviour change.
2. Make `DeleteTexture` delete the image with its definition, and clean up the
   three existing orphans.
3. Block the deletes that already exist — tileset, sprite, collider, blueprint,
   tile — on a referrer check.
4. Add Delete to the Texture Editor and the Terrain Editor, behind the existing
   `ConfirmPrompt` pattern.
5. Bundle deletion for a recipe-owned texture + tileset + recipe, per §4.

Steps 1-3 remove the ways a level can currently be broken. Steps 4-5 add the
capability that is missing. Doing them in that order means the new buttons are
safe on the day they appear.
