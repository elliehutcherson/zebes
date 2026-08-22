# Deleting an asset something else might be using

**Status: implemented.** §§1-8 and all of §9.

Every delete refuses when something still references what it would remove, and
says what. A generated terrain deletes as one thing. Both buttons exist: Delete
in the Texture Editor's details column, and Delete beside New and Save As in the
Terrain Editor's output panel, each behind the same `ConfirmPrompt` every other
destructive control uses.

The rest of this document is the analysis the implementation followed.

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

Running the scan over the shipped catalogue makes both cases concrete:

```
texture of 'lucinda_cave':  2 referrers
  Tileset 'lucinda_cave' (texture_id)
  Terrain recipe 'lucinda_cave' (texture_id)     <- the bundle

texture of 'Sunny Tileset': 6 referrers
  Tileset 'Sunny Tileset' (texture_id)
  Sprite 'kGrass1' (texture_id)                  <- and four more sprites
```

The second is the counterexample in shipped data. A sprite-only check would have
found the five sprites and missed the tileset; assuming the texture belongs to
the tileset would have done the reverse.

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

1. **Done.** `src/resources/asset_references.{h,cc}` — a pure scan over the
   loaded definitions, one function per referenced kind.
2. **Done.** `DeleteTexture` removes the image with its definition. The three
   orphans were resolved rather than deleted: `samus.png` is the ripped sheet the
   shipped `samus-*.png` frames were cut from, so it moved to `source_art/` where
   inputs belong; `arrow.png` and the clouds background are finished single
   images like `sky_mountains_background` beside them, so they got definitions. A
   test in `shipped_assets_test` now holds the invariant.
3. **Done.** Every `Api` delete refuses with its referrer list, and tile deletion
   is checked through `Api::CheckTileDeletable`. Four narrow "is it used" queries
   were removed rather than left beside the scan — `BlueprintManager::IsSpriteUsed`
   and `IsColliderUsed`, `SpriteManager::IsTextureUsed`,
   `TilesetManager::IsTextureUsed` — because two mechanisms answering one
   question is how a future delete gets wired to the incomplete one.
4. **Done.** Both buttons, each behind the same `ConfirmPrompt` every other
   destructive control in the editor already uses.
   - **Texture Editor**: Delete in the details column, offered only for a
     texture that exists — a texture being created has nothing on disk, and New
     Texture already abandons it. A refusal lands in `model_.error()` as the
     whole message rather than a reason appended to one, because it already
     names the texture and every referrer. The selection survives a refusal, so
     the thing the message is about is still on screen.
   - **Terrain Editor**: Delete beside New and Save As, on its own row because
     an armed prompt grows a wrapped question. It reports
     `Action::kDeleteTerrain`; the editor calls `DeleteGeneratedTerrain` and, on
     success, resets the whole model rather than only the recipe binding. The
     tuning left on screen described artwork that no longer exists, and leaving
     it would invite a Create that quietly made a second terrain of the same
     name.

   Decided against: a bare "delete recipe" button. Nothing references a recipe,
   so it would always succeed, and it would leave a tileset nothing can
   regenerate — the state `lucinda_cave` was in, and the one
   `RegenerateTerrainTileset` now refuses. A one-click way to manufacture the
   problem this work removed is not worth the flexibility.

   One thing planned here turned out to be unnecessary: the Texture Editor's
   error banner was described as a bare `TextColored` unlike the other five
   tabs. It already has a Dismiss button (`texture_editor.cc:95-99`), so there
   was nothing to make consistent.

   Pinning the confirmation wording needed one change to shared test
   scaffolding: `MockGui::TextWrapped` discarded its arguments, so no test could
   tell a question naming all three assets from an empty string. It records them
   now. That is where panels put every sentence a user has to read and act on,
   and none of it was observable.

5. **Done.** The bundle is an ordering over the existing guards rather than a
   bypass: recipe, then tileset, then texture, each member gone before it can
   block the next, every step still going through its own checked delete. It
   pre-flights first because the sequence cannot be unwound, subtracting the
   bundle's own cross-references — those are what make the three one thing.

   Not atomic, deliberately. Pre-flight removes every referential reason to fail;
   a filesystem error mid-sequence still leaves a partial state, which is
   reported by step and finishable by hand once step 4 exists. A rollback journal
   for three files is not worth it.

Steps 1-3 removed the ways a level could be broken; steps 4 and 5 added the
capability that was missing. Deleting `lucinda_cave` — the request that started
this — is now one button in the Terrain Editor, and it refuses rather than
blanking a level if anything has been pointed at it since.
