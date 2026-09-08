# Definition migrations, 2026

Closed 2026-09-06. `scripts/migrate_definitions.py` ran these twelve migrations
and was deleted once every shipped definition was current. This file records
what they did and why, so the reasoning survives without the code.

The invariant they served is unchanged and lives in
[`../style-guide.md`](../style-guide.md): readers require every field, writers
emit every field, and there are no defaults on disk. A migration moves old
files forward once; a tolerant reader would reinterpret them forever.

## What each one did

| Record | Change | Value that preserved old behavior |
|---|---|---|
| Sprite | Gained per-frame `offset_x`/`offset_y` | Zero — earlier frames drew at no offset |
| Sprite | Gained explicit `playback_mode` | `loop` — every earlier Sprite looped |
| Blueprint | Made state identity and placement explicit | Derived from the existing state list |
| Level | Gained world layers and sort order | Existing draw order, flattened into layer 0 |
| Level | Embedded parallax themes extracted to their own files | New theme IDs derived from level ID plus the old local index |
| Level | Blueprint state keys became stable ASCII keys | Derived from each state's display name |
| Parallax theme | Layer compositions became explicit | Measured from the referenced texture's real PNG dimensions |
| Tileset | Slope identifiers lost an underscore | Mechanical spelling rename |
| Tileset | Slope vocabulary changed meaning | See below — this one was not a rename |
| Tileset | Collections omitted when empty became explicit `[]` | Empty list |
| Terrain recipe | v1 and v2 brought to v3 | Existing generator settings, restated |
| Prop recipe | Gained explicit grounded attachment | Values reproducing the previous output |
| Source artwork | Moved to an ID-backed directory | File move plus path rewrite |

## The one worth remembering

The slope vocabulary change was not a spelling fix. A slope name used to say
which side the wedge tapered away on. It now says which side reaches full tile
height — the opposite side. `kSlope45BottomLeft` and `kSlope45FloorTallRight`
are the same shape.

The new names deliberately share no spelling with the old ones. Had the rename
reused them, a definition that escaped migration would have loaded as the
mirrored shape in silence, because the numeric `shape` field was untouched and
the identifier lookup would still have resolved. Sharing no spelling makes a
missed file fail the lookup instead of loading wrong.

Apply that rule to any future rename that changes what a name means: pick
spellings that cannot collide with the old ones, so a file nobody migrated
fails loudly.

## Two details that cost time

Migrated files had to match the indentation each resource manager writes with.
A migrated file and one the editor re-saves must be byte-identical, or every
later save produces a whole-file diff that buries the real change.

The parallax theme extraction and the source artwork move both touched several
files at once, so both ran as preflighted all-or-nothing passes rather than
per-file edits.

## Puppet documents gain joint chains and a frame rate, 2026-09-07

Three tracked puppet documents took two new required fields.

`joint_chains` names the limb each joint belongs to — `arm_l`, `spine`, `tail`.
Nothing in the drawing reads it. A Rig Bench file records one per point, so a
skeleton written out of a document needs it, and no bone graph recovers it: the
mouse rig is one connected graph and still has seven limbs.

`fps` is playback, which the browser had hardcoded to 8.

`test-puppet.json` imported the mouse rig, so its chains came straight back from
`inputs/rig-bench.json`. `mouse_interactive_run_v1.json` and
`mouse_profile_v1.json` predate that rig and name their own joints, so their
chains were read off the bones each document actually has: `elbow_a` and
`hand_a` are `arm_a`, `knee_b` and `foot_b` are `leg_b`, and `neck`, `hip` and
both shoulders are `spine`. Every file took `fps: 8`, which reproduces what
playback already did.

The script filled the fields and `puppet_edit` re-saved each file, which is what
guarantees the result is byte-identical to what the editor writes. The script
was deleted after it ran, and `puppet_document_json_test` loads every tracked
document, so a later format change cannot quietly break one.

## Overlap and per-bone stretch, 2026-09-07

Two more required fields, in the same session as the chains and frame rate.

`allow_overlap` on the document decides whether two parts may own the same
source pixel. Every file took `true`, which is what they already did.

`may_stretch` on each bone decides what a change in that bone's length means to
the artwork. Every bone took `false`, which is what the renderer already did:
`TransformPoint` and `TransformMeshVertex` in `layered_puppet.cc` place pixels
at the distance they were drawn at and only turn them. The browser preview was
the one thing that scaled, and it was wrong.

The three legacy `puppet_specs/` files took `may_stretch: false` per bone too,
because the spec reader requires every field and those three are still read from
disk. They are still on the retirement list in README section 12.

`false` had to stay bit-identical, not merely equivalent. A blended stretch of
`w + (1 - w)` is not exactly 1.0 for every weight, so `TransformMeshVertex`
skips the multiply outright when neither bone stretches rather than multiplying
by a computed one.
