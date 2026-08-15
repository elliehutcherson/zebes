#!/usr/bin/env python3
"""Fill in definition fields that older files predate.

The asset readers in `src/resources/` require every field a writer emits, so a
definition missing one fails to load rather than being silently reinterpreted
with a default. That invariant is only affordable if the files on disk are kept
current, which is what this script is for: each migration below names one field,
the value that reproduces the behaviour the old file had, and the record it
belongs to.

Adding a field to a definition means adding a migration here and running it
once. Migrations are idempotent, so running the script twice is harmless and
running it against already-current data reports no changes.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


# Sprite frames gained authored offsets after the first sprites were cut. A
# frame written before that drew at no offset, so zero is the value that
# preserves exactly what the old file rendered.
SPRITE_FRAME_DEFAULTS = {"offset_x": 0, "offset_y": 0}


def migrate_sprite(document: dict) -> bool:
    """Fills missing frame offsets. Returns whether anything changed."""
    changed = False
    for frame in document.get("frames", []):
        for field, value in SPRITE_FRAME_DEFAULTS.items():
            if field not in frame:
                frame[field] = value
                changed = True
    return changed


def migrate_level(document: dict) -> bool:
    """Brings a level current. Returns whether anything changed.

    Entities gain an explicit draw order. They used to draw in ascending ID
    order, which is insertion order; zero for all of them preserves exactly that,
    because ties still resolve by ID.

    The level-wide `parallax_layers` list is dropped. Themes and zones replaced
    it -- a theme owns an ordered stack of layers, a zone binds a theme to a
    region -- and nothing has read the flat list since. The reader now rejects
    the key rather than loading a list no editor can reach and no renderer draws.
    """
    changed = False
    for entity in document.get("entities", []):
        if "sort_order" not in entity:
            entity["sort_order"] = 0
            changed = True
    if "parallax_layers" in document:
        del document["parallax_layers"]
        changed = True
    return changed


# The slope enumerators lost the underscore before their final segment when
# TileShape was brought in line with the kPascalCase constant rule. Derived
# artwork names a tile "<terrain name> <shape identifier>", so a tileset written
# before the rename carries the old spelling in tile names and would no longer
# match what the editor regenerates. The `shape` field is a number and is
# unaffected.
SLOPE_IDENTIFIER_RENAMES = {
    "kGentleSlopeBottomLeft_Lower": "kGentleSlopeBottomLeftLower",
    "kGentleSlopeBottomLeft_Upper": "kGentleSlopeBottomLeftUpper",
    "kGentleSlopeBottomRight_Lower": "kGentleSlopeBottomRightLower",
    "kGentleSlopeBottomRight_Upper": "kGentleSlopeBottomRightUpper",
    "kGentleSlopeTopLeft_Lower": "kGentleSlopeTopLeftLower",
    "kGentleSlopeTopLeft_Upper": "kGentleSlopeTopLeftUpper",
    "kGentleSlopeTopRight_Lower": "kGentleSlopeTopRightLower",
    "kGentleSlopeTopRight_Upper": "kGentleSlopeTopRightUpper",
    "kSteepSlopeBottomLeft_Bottom": "kSteepSlopeBottomLeftBottom",
    "kSteepSlopeBottomLeft_Top": "kSteepSlopeBottomLeftTop",
    "kSteepSlopeBottomRight_Bottom": "kSteepSlopeBottomRightBottom",
    "kSteepSlopeBottomRight_Top": "kSteepSlopeBottomRightTop",
    "kSteepSlopeTopLeft_Bottom": "kSteepSlopeTopLeftBottom",
    "kSteepSlopeTopLeft_Top": "kSteepSlopeTopLeftTop",
    "kSteepSlopeTopRight_Bottom": "kSteepSlopeTopRightBottom",
    "kSteepSlopeTopRight_Top": "kSteepSlopeTopRightTop",
}


def migrate_tileset(document: dict) -> bool:
    """Materialises collections that used to be omitted when empty, and brings
    tile names onto the current slope identifier spellings.

    An absent list and an empty one always meant the same thing, and offering
    the reader two spellings of one state is what forced it to guess. The writer
    now emits both unconditionally.
    """
    changed = False
    for tile in document.get("tiles", []):
        name = tile["name"]
        for old, new in SLOPE_IDENTIFIER_RENAMES.items():
            name = name.replace(old, new)
        if name != tile["name"]:
            tile["name"] = name
            changed = True
    if "terrains" not in document:
        document["terrains"] = []
        changed = True
    for terrain in document["terrains"]:
        if "member_tile_ids" not in terrain and "shape_tile_ids" not in terrain:
            terrain["shape_tile_ids"] = []
            changed = True

        # "member" outlived the concept of membership. The list used to mean
        # "tiles the brush must never rewrite"; a refresh now hands every cell
        # back the shape it already had, so what remains is a scheme's
        # shape-to-artwork table.
        if "member_tile_ids" in terrain:
            terrain["shape_tile_ids"] = terrain.pop("member_tile_ids")
            changed = True

        # The neighbourhood each derived tile depicts. Every terrain on disk
        # predates deriving artwork, so the list is empty and the field is
        # present rather than optional.
        if "derived_tiles" not in terrain:
            terrain["derived_tiles"] = []
            changed = True
    return changed


TERRAIN_RECIPE_SCHEMA_VERSION = 3

# What a v2 recipe means under v3. v2 predates edge decoration entirely, so it
# selects None explicitly rather than inheriting a default that may become
# visible later; the remaining values are TerrainEdgeDetailConfig's defaults and
# are inert while family is None. Mirrors the migration that used to live in
# ConfigFromJson, which was deleted once no v2 file remained.
TERRAIN_V2_EDGE_DETAIL = {
    "family": 0,
    "amount": 0.65,
    "length": 4,
    "clump_size": 5,
    "lean": 0.0,
    "highlight": 0.35,
}


def migrate_terrain_recipe(document: dict) -> bool:
    """Upgrades a v1 or v2 recipe in place. Returns whether anything changed."""
    version = document.get("schema_version")
    if version == TERRAIN_RECIPE_SCHEMA_VERSION:
        return False
    if version != 2:
        # v1 stored a different surface record entirely and none has ever
        # existed in this repo. Refusing is better than guessing at one.
        raise ValueError(
            f"Cannot migrate terrain recipe schema version {version!r}; only 2 is supported"
        )

    surface = document["config"]["surface"]
    surface.setdefault("edge_detail", dict(TERRAIN_V2_EDGE_DETAIL))
    document["schema_version"] = TERRAIN_RECIPE_SCHEMA_VERSION
    return True


# Each definition directory, the migration that brings its files current, and
# the indent its manager writes with. Matching the indent matters: a migrated
# file and one the editor re-saves must be byte-identical, or every later save
# produces a whole-file diff that hides the real change.
MIGRATIONS = {
    "levels": (migrate_level, 4),
    "sprites": (migrate_sprite, 4),
    "terrain_recipes": (migrate_terrain_recipe, 2),
    "tilesets": (migrate_tileset, 4),
}


def load_document(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        document = json.load(handle)
    if not isinstance(document, dict):
        raise ValueError(f"Definition is not a JSON object: {path}")
    return document


def write_document(path: Path, document: dict, indent: int) -> None:
    # Sorted keys and no trailing newline are what nlohmann::json's dump()
    # produces, since it stores objects in a std::map and the managers write the
    # string with no terminator.
    with path.open("w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=indent, sort_keys=True)


def migrate_directory(definitions_root: Path, kind: str, dry_run: bool) -> list[Path]:
    """Runs one directory's migration, returning the files that needed it."""
    directory = definitions_root / kind
    if not directory.is_dir():
        raise ValueError(f"Definition directory does not exist: {directory}")

    migrate, indent = MIGRATIONS[kind]
    changed = []
    for path in sorted(directory.glob("*.json")):
        document = load_document(path)
        if not migrate(document):
            continue
        changed.append(path)
        if not dry_run:
            write_document(path, document, indent)
    return changed


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--definitions",
        type=Path,
        default=Path("assets/definitions"),
        help="Root of the definition tree to migrate.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report what would change without writing anything.",
    )
    args = parser.parse_args(argv)

    total = 0
    for kind in sorted(MIGRATIONS):
        # Some definition directories are created by the editor on first save
        # rather than shipped, so a whole-tree run must tolerate their absence.
        # migrate_directory itself still refuses a path that is not there, which
        # is what catches a mistyped --definitions.
        if not (args.definitions / kind).is_dir():
            print(f"skipped (no directory): {kind}")
            continue
        changed = migrate_directory(args.definitions, kind, args.dry_run)
        total += len(changed)
        for path in changed:
            print(f"{'would migrate' if args.dry_run else 'migrated'}: {path}")

    if total == 0:
        print("All definitions are current.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
