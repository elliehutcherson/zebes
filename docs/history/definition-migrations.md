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
