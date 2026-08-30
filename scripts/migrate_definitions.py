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
import math
import re
import struct
import sys
import uuid
from pathlib import Path


# Sprite frames gained authored offsets after the first sprites were cut. A
# frame written before that drew at no offset, so zero is the value that
# preserves exactly what the old file rendered.
SPRITE_FRAME_DEFAULTS = {"offset_x": 0, "offset_y": 0}


def blueprint_state_key(name: str) -> str:
    """Derives a stable ASCII authoring key from one legacy display name."""
    key = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    if not key:
        raise ValueError("Cannot derive a blueprint state key from its display name")
    return key


def migrate_blueprint(document: dict) -> bool:
    """Makes Blueprint state identity and placement behavior explicit."""
    changed = False
    state_keys: set[str] = set()
    for state in document.get("states", []):
        if "key" not in state:
            name = state.get("name")
            if not isinstance(name, str):
                raise ValueError("Cannot migrate a blueprint state without a display name")
            state["key"] = blueprint_state_key(name)
            changed = True

        key = state["key"]
        if not isinstance(key, str) or not key or key in state_keys:
            raise ValueError(f"Blueprint state key is invalid or duplicated: {key!r}")
        state_keys.add(key)

        if "placement_mode" not in state:
            state["placement_mode"] = "grounded"
            changed = True
    return changed


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

    The former root tile grid and entity map become world layer 0. Their
    contents are moved, not copied, so one definition has exactly one owner for
    each collection after migration. A document containing both representations
    is ambiguous and refused rather than guessed at.

    Entities also gain an explicit draw order. They used to draw in ascending
    ID order, which is insertion order; zero for all of them preserves exactly
    that, because ties still resolve by ID.

    The level-wide `parallax_layers` list is dropped. Themes and zones replaced
    it -- a theme owns an ordered stack of layers, a zone binds a theme to a
    region -- and nothing has read the flat list since. The reader now rejects
    the key rather than loading a list no editor can reach and no renderer draws.
    """
    changed = False

    has_layers = "layers" in document
    has_tile_chunks = "tile_chunks" in document
    has_entities = "entities" in document
    if has_layers and (has_tile_chunks or has_entities):
        raise ValueError(
            "Cannot migrate level containing both layers and root tile/entity collections"
        )
    if not has_layers and has_tile_chunks != has_entities:
        raise ValueError(
            "Cannot migrate level with only one root tile/entity collection"
        )
    if not has_layers and not has_tile_chunks:
        raise ValueError(
            "Cannot migrate level missing layers and root tile/entity collections"
        )

    if not has_layers:
        document["layers"] = [
            {
                "id": 0,
                "name": "Base",
                "tile_chunks": document.pop("tile_chunks"),
                "entities": document.pop("entities"),
            }
        ]
        changed = True

    for layer in document["layers"]:
        for entity in layer["entities"]:
            if "sort_order" not in entity:
                entity["sort_order"] = 0
                changed = True

    if "parallax_layers" in document:
        del document["parallax_layers"]
        changed = True
    return changed


PARALLAX_THEME_NAMESPACE = uuid.UUID("8db10f18-b15d-4c0c-9208-c687af105d5a")


def parallax_theme_id(level_id: str, local_theme_id: int) -> str:
    """Stable identity for one formerly level-owned theme."""
    return str(uuid.uuid5(PARALLAX_THEME_NAMESPACE, f"{level_id}:{local_theme_id}"))


def migrate_parallax_themes(definitions_root: Path, dry_run: bool) -> list[Path]:
    """Extracts embedded level themes as one preflighted multi-file migration.

    Every output is calculated and checked before any write. Existing theme
    files are accepted only when their complete JSON content is identical to
    the deterministic output, which makes retrying safe without permitting a
    half-migrated level to overwrite independently authored work.
    """
    levels_directory = definitions_root / "levels"
    if not levels_directory.is_dir():
        raise ValueError(f"Definition directory does not exist: {levels_directory}")
    themes_directory = definitions_root / "parallax_themes"

    level_updates: list[tuple[Path, dict]] = []
    theme_outputs: dict[Path, dict] = {}
    for level_path in sorted(levels_directory.glob("*.json")):
        level = load_document(level_path)
        if "themes" not in level:
            for zone in level.get("zones", []):
                if not isinstance(zone.get("theme_id"), str):
                    raise ValueError(
                        f"Half-migrated level has no themes but a non-string theme_id: {level_path}"
                    )
            continue

        old_themes = level["themes"]
        if not isinstance(old_themes, list):
            raise ValueError(f"Level themes are not a list: {level_path}")
        by_local_id: dict[int, str] = {}
        for old_theme in old_themes:
            local_id = old_theme.get("id")
            if not isinstance(local_id, int) or local_id in by_local_id:
                raise ValueError(f"Level has an invalid or duplicate theme ID: {level_path}")
            theme_id = parallax_theme_id(level["id"], local_id)
            by_local_id[local_id] = theme_id
            extracted = {
                "id": theme_id,
                "name": f'{level["name"]} — {old_theme["name"]}',
                "layers": old_theme["layers"],
            }
            output_path = themes_directory / f"{theme_id}.json"
            existing_planned = theme_outputs.get(output_path)
            if existing_planned is not None and existing_planned != extracted:
                raise ValueError(f"Conflicting deterministic theme output: {output_path}")
            theme_outputs[output_path] = extracted

        migrated = dict(level)
        migrated.pop("themes")
        migrated["zones"] = [dict(zone) for zone in level.get("zones", [])]
        for zone in migrated["zones"]:
            local_id = zone.get("theme_id")
            if local_id not in by_local_id:
                raise ValueError(
                    f"Level zone references missing embedded theme {local_id!r}: {level_path}"
                )
            zone["theme_id"] = by_local_id[local_id]
        level_updates.append((level_path, migrated))

    # The complete preflight happens before mkdir or any write.
    for output_path, expected in theme_outputs.items():
        if output_path.exists() and load_document(output_path) != expected:
            raise ValueError(f"Existing parallax theme conflicts with migration: {output_path}")

    changed = [path for path, _ in level_updates]
    changed.extend(path for path in sorted(theme_outputs) if not path.exists())
    if dry_run or not changed:
        return changed

    themes_directory.mkdir(parents=True, exist_ok=True)
    for output_path, document in sorted(theme_outputs.items()):
        if not output_path.exists():
            write_document(output_path, document, 2)
    for level_path, document in level_updates:
        write_document(level_path, document, 4)
    return changed


PARALLAX_THEME_SCHEMA_VERSION = 2
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _positive_png_dimensions(path: Path) -> tuple[int, int]:
    """Reads the fixed PNG signature and IHDR dimensions without a dependency."""
    try:
        with path.open("rb") as handle:
            header = handle.read(24)
    except OSError as error:
        raise ValueError(f"Parallax texture PNG cannot be read: {path}") from error
    if len(header) != 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise ValueError(f"Parallax texture is not a readable PNG: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if width <= 0 or height <= 0:
        raise ValueError(f"Parallax texture has invalid dimensions: {path}")
    return width, height


def _parallax_texture_dimensions(
    definitions_root: Path, required_ids: set[str]
) -> dict[str, tuple[int, int]]:
    textures_directory = definitions_root / "textures"
    if not textures_directory.is_dir():
        raise ValueError(f"Definition directory does not exist: {textures_directory}")

    assets_root = definitions_root.parent.resolve()
    texture_paths: dict[str, Path] = {}
    for definition_path in sorted(textures_directory.glob("*.json")):
        texture = load_document(definition_path)
        texture_id = texture.get("id")
        relative_path = texture.get("path")
        if not isinstance(texture_id, str) or not texture_id:
            raise ValueError(f"Texture has an invalid ID: {definition_path}")
        if texture_id in texture_paths:
            raise ValueError(f"Duplicate texture ID {texture_id!r}: {definition_path}")
        if not isinstance(relative_path, str) or not relative_path:
            raise ValueError(f"Texture has an invalid path: {definition_path}")
        if relative_path.startswith("textures/"):
            image_path = (assets_root / relative_path).resolve()
        else:
            image_path = (assets_root / "textures" / relative_path).resolve()
        if not image_path.is_relative_to(assets_root):
            raise ValueError(f"Texture path escapes the asset root: {definition_path}")
        texture_paths[texture_id] = image_path

    missing = required_ids - texture_paths.keys()
    if missing:
        raise ValueError(f"Parallax layers reference missing textures: {sorted(missing)!r}")
    return {
        texture_id: _positive_png_dimensions(texture_paths[texture_id])
        for texture_id in sorted(required_ids)
    }


def _migrate_parallax_theme_composition(
    document: dict, texture_dimensions: dict[str, tuple[int, int]], path: Path
) -> bool:
    version = document.get("schema_version")
    if version == PARALLAX_THEME_SCHEMA_VERSION:
        for layer in document.get("layers", []):
            if isinstance(layer, dict) and any(
                field in layer
                for field in ("texture_id", "base_scale", "repeat_x", "repeat_y")
            ):
                raise ValueError(f"Current parallax theme retains legacy layer fields: {path}")
        return False
    if version is not None:
        raise ValueError(
            f"Cannot migrate parallax theme schema version {version!r}: {path}"
        )

    layers = document.get("layers")
    if not isinstance(layers, list):
        raise ValueError(f"Parallax theme layers are not a list: {path}")
    migrated_layers = []
    for layer in layers:
        if not isinstance(layer, dict):
            raise ValueError(f"Parallax theme layer is not an object: {path}")
        if any(
            field in layer for field in ("elements", "repeat_period_x", "repeat_period_y")
        ):
            raise ValueError(f"Parallax theme has an ambiguous half-migrated layer: {path}")

        name = layer.get("name")
        texture_id = layer.get("texture_id")
        scale = layer.get("base_scale")
        repeat_x = layer.get("repeat_x")
        repeat_y = layer.get("repeat_y")
        if not isinstance(name, str) or not name:
            raise ValueError(f"Parallax layer has an invalid name: {path}")
        if not isinstance(texture_id, str) or texture_id not in texture_dimensions:
            raise ValueError(
                f"Parallax layer references missing texture {texture_id!r}: {path}"
            )
        if (
            isinstance(scale, bool)
            or not isinstance(scale, (int, float))
            or not math.isfinite(scale)
            or scale <= 0
        ):
            raise ValueError(f"Parallax layer has an invalid base scale: {path}")
        if not isinstance(repeat_x, bool) or not isinstance(repeat_y, bool):
            raise ValueError(f"Parallax layer has invalid repeat flags: {path}")

        width, height = texture_dimensions[texture_id]
        migrated = {
            field: layer[field]
            for field in ("name", "scroll_factor_x", "scroll_factor_y", "offset_x", "offset_y")
            if field in layer
        }
        required_fields = {
            "name",
            "scroll_factor_x",
            "scroll_factor_y",
            "offset_x",
            "offset_y",
        }
        if migrated.keys() != required_fields:
            raise ValueError(f"Parallax layer is missing required geometry: {path}")
        unexpected = set(layer) - required_fields - {
            "texture_id",
            "base_scale",
            "repeat_x",
            "repeat_y",
        }
        if unexpected:
            raise ValueError(
                f"Parallax layer has unsupported fields {sorted(unexpected)!r}: {path}"
            )
        migrated["repeat_period_x"] = float(width * scale) if repeat_x else 0.0
        migrated["repeat_period_y"] = float(height * scale) if repeat_y else 0.0
        migrated["elements"] = [
            {
                "id": 0,
                "name": name,
                "texture_id": texture_id,
                "position_x": 0.0,
                "position_y": 0.0,
                "scale": float(scale),
            }
        ]
        migrated_layers.append(migrated)

    document["layers"] = migrated_layers
    document["schema_version"] = PARALLAX_THEME_SCHEMA_VERSION
    return True


def migrate_parallax_layer_compositions(
    definitions_root: Path, dry_run: bool
) -> list[Path]:
    """Upgrades one-texture layers into explicit repeatable compositions.

    Texture dimensions are read before any file is written because the old
    boolean repeat flags implicitly used the scaled source image dimensions as
    their period. Materialising that period preserves existing rendering while
    allowing multiple independently positioned elements in the new schema.
    """
    themes_directory = definitions_root / "parallax_themes"
    if not themes_directory.is_dir():
        raise ValueError(f"Definition directory does not exist: {themes_directory}")
    documents = []
    required_texture_ids = set()
    for path in sorted(themes_directory.glob("*.json")):
        document = load_document(path)
        documents.append((path, document))
        if document.get("schema_version") is None:
            for layer in document.get("layers", []):
                if isinstance(layer, dict) and isinstance(layer.get("texture_id"), str):
                    required_texture_ids.add(layer["texture_id"])
    texture_dimensions = _parallax_texture_dimensions(
        definitions_root, required_texture_ids
    )

    updates = []
    for path, document in documents:
        if _migrate_parallax_theme_composition(document, texture_dimensions, path):
            updates.append((path, document))

    if not dry_run:
        for path, document in updates:
            write_document(path, document, 2)
    return [path for path, _ in updates]


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

# The slope vocabulary then changed meaning, not just spelling. A name used to
# say which side the wedge tapered away on; it now says which side reaches full
# tile height, which is the opposite side. So kSlope45BottomLeft and
# kSlope45FloorTallRight are the same shape.
#
# The new names deliberately share no spelling with the old ones. Had the rename
# reused them -- kSlope45BottomLeft coming to mean what kSlope45BottomRight
# meant -- a definition that escaped this migration would have loaded as the
# mirrored shape in silence, since the numeric `shape` field is untouched and
# TileShapeFromIdentifier would still have resolved it. Sharing no spelling
# makes a missed file fail the lookup instead.
#
# Applied after SLOPE_IDENTIFIER_RENAMES, so a definition from either earlier
# era arrives at the current names in one pass.
SLOPE_VOCABULARY_RENAMES = {
    "kSlope45BottomLeft": "kSlope45FloorTallRight",
    "kSlope45BottomRight": "kSlope45FloorTallLeft",
    "kSlope45TopLeft": "kSlope45CeilingTallRight",
    "kSlope45TopRight": "kSlope45CeilingTallLeft",
    "kGentleSlopeBottomLeftLower": "kGentleSlopeFloorTallRightLower",
    "kGentleSlopeBottomLeftUpper": "kGentleSlopeFloorTallRightUpper",
    "kGentleSlopeBottomRightLower": "kGentleSlopeFloorTallLeftLower",
    "kGentleSlopeBottomRightUpper": "kGentleSlopeFloorTallLeftUpper",
    "kGentleSlopeTopLeftLower": "kGentleSlopeCeilingTallRightLower",
    "kGentleSlopeTopLeftUpper": "kGentleSlopeCeilingTallRightUpper",
    "kGentleSlopeTopRightLower": "kGentleSlopeCeilingTallLeftLower",
    "kGentleSlopeTopRightUpper": "kGentleSlopeCeilingTallLeftUpper",
    "kSteepSlopeBottomLeftBottom": "kSteepSlopeFloorTallRightBottom",
    "kSteepSlopeBottomLeftTop": "kSteepSlopeFloorTallRightTop",
    "kSteepSlopeBottomRightBottom": "kSteepSlopeFloorTallLeftBottom",
    "kSteepSlopeBottomRightTop": "kSteepSlopeFloorTallLeftTop",
    "kSteepSlopeTopLeftBottom": "kSteepSlopeCeilingTallRightBottom",
    "kSteepSlopeTopLeftTop": "kSteepSlopeCeilingTallRightTop",
    "kSteepSlopeTopRightBottom": "kSteepSlopeCeilingTallLeftBottom",
    "kSteepSlopeTopRightTop": "kSteepSlopeCeilingTallLeftTop",
}


def _rename_shape_identifiers(name: str) -> str:
    """Brings one tile name onto the current slope identifiers.

    Era order matters: the underscore map first, so a name from before either
    rename reaches the current spelling in one pass. Within an era no key is a
    prefix of another, so order there is free; longest-first anyway, because the
    day someone adds a key that is a prefix the failure would be a silently
    mangled tail rather than an error.
    """
    for renames in (SLOPE_IDENTIFIER_RENAMES, SLOPE_VOCABULARY_RENAMES):
        for old in sorted(renames, key=len, reverse=True):
            name = name.replace(old, renames[old])
    return name


def migrate_tileset(document: dict) -> bool:
    """Materialises collections that used to be omitted when empty, and brings
    tile names onto the current slope identifier spellings.

    An absent list and an empty one always meant the same thing, and offering
    the reader two spellings of one state is what forced it to guess. The writer
    now emits both unconditionally.
    """
    changed = False
    for tile in document.get("tiles", []):
        name = _rename_shape_identifiers(tile["name"])
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


PROP_RECIPE_SCHEMA_VERSION = 2
PROP_PIPELINE_VERSION = 2


def migrate_prop_recipe(document: dict) -> bool:
    """Adds explicit grounded attachment semantics without changing output."""
    version = document.get("schema_version")
    if version == PROP_RECIPE_SCHEMA_VERSION:
        return False
    if version != 1:
        raise ValueError(
            f"Cannot migrate prop recipe schema version {version!r}; only 1 is supported"
        )
    if document.get("pipeline_version") != 1:
        raise ValueError("Cannot migrate prop recipe with a non-v1 pipeline")

    pipeline = document["pipeline"]
    composition = pipeline["composition"]
    cleanup = pipeline["cleanup"]
    if "attachment" in composition:
        raise ValueError("Cannot migrate v1 prop recipe that already contains attachment data")
    if "grounded_tolerance" not in cleanup or "contact_tolerance" in cleanup:
        raise ValueError("Cannot migrate ambiguous v1 prop cleanup tolerance")

    composition["attachment"] = {"mode": "grounded", "free_anchor": None}
    cleanup["contact_tolerance"] = cleanup.pop("grounded_tolerance")
    document["pipeline_version"] = PROP_PIPELINE_VERSION
    document["schema_version"] = PROP_RECIPE_SCHEMA_VERSION
    return True


SOURCE_ARTWORK_SCHEMA_VERSION = 2
LEGACY_SOURCE_ARTWORK_DIRECTORY = Path("source_art/props")
SOURCE_ARTWORK_DIRECTORY = Path("source_art")


def migrate_source_artwork(definitions_root: Path, dry_run: bool) -> list[Path]:
    """Moves retained artwork to its neutral ID-backed directory.

    Definitions and images are preflighted as one set before any directory is
    created or file is moved. A definition describing one location while only
    the other image exists is a half-migration and is refused; the script does
    not guess which file should be authoritative.
    """
    definitions_directory = definitions_root / "source_artworks"
    if not definitions_directory.is_dir():
        raise ValueError(
            f"Definition directory does not exist: {definitions_directory}"
        )
    assets_root = definitions_root.parent
    plans: list[tuple[Path, dict, Path, Path, bytes]] = []

    for definition_path in sorted(definitions_directory.glob("*.json")):
        document = load_document(definition_path)
        artwork_id = document.get("id")
        if (
            not isinstance(artwork_id, str)
            or not artwork_id
            or not all(
                "a" <= character <= "z"
                or "A" <= character <= "Z"
                or "0" <= character <= "9"
                or character == "-"
                for character in artwork_id
            )
        ):
            raise ValueError(f"Source artwork has an invalid ID: {definition_path}")
        if definition_path.stem != artwork_id:
            raise ValueError(
                f"Source artwork filename does not match its ID: {definition_path}"
            )

        legacy_relative = LEGACY_SOURCE_ARTWORK_DIRECTORY / f"{artwork_id}.png"
        current_relative = SOURCE_ARTWORK_DIRECTORY / f"{artwork_id}.png"
        legacy_image = assets_root / legacy_relative
        current_image = assets_root / current_relative
        version = document.get("schema_version")

        if version == SOURCE_ARTWORK_SCHEMA_VERSION:
            if document.get("source_path") != current_relative.as_posix():
                raise ValueError(
                    f"Current source artwork has a non-canonical path: {definition_path}"
                )
            if not current_image.is_file() or legacy_image.exists():
                raise ValueError(
                    f"Current source artwork has missing or conflicting image files: {definition_path}"
                )
            continue

        if version != 1:
            raise ValueError(
                f"Cannot migrate source artwork schema version {version!r}: {definition_path}"
            )
        if document.get("source_path") != legacy_relative.as_posix():
            raise ValueError(
                f"Legacy source artwork has a non-canonical path: {definition_path}"
            )
        if not legacy_image.is_file() or current_image.exists():
            raise ValueError(
                f"Legacy source artwork has missing or conflicting image files: {definition_path}"
            )

        migrated = dict(document)
        migrated["schema_version"] = SOURCE_ARTWORK_SCHEMA_VERSION
        migrated["source_path"] = current_relative.as_posix()
        plans.append(
            (definition_path, migrated, legacy_image, current_image, definition_path.read_bytes())
        )

    changed = [definition_path for definition_path, *_ in plans]
    if dry_run or not changed:
        return changed

    (assets_root / SOURCE_ARTWORK_DIRECTORY).mkdir(parents=True, exist_ok=True)
    moved: list[tuple[Path, Path]] = []
    written: list[tuple[Path, bytes]] = []
    try:
        for _, _, legacy_image, current_image, _ in plans:
            legacy_image.replace(current_image)
            moved.append((legacy_image, current_image))
        for definition_path, migrated, _, _, original in plans:
            write_document(definition_path, migrated, 2)
            written.append((definition_path, original))
    except OSError:
        for definition_path, original in reversed(written):
            definition_path.write_bytes(original)
        for legacy_image, current_image in reversed(moved):
            if current_image.exists() and not legacy_image.exists():
                legacy_image.parent.mkdir(parents=True, exist_ok=True)
                current_image.replace(legacy_image)
        raise
    return changed


# Each definition directory, the migration that brings its files current, and
# the indent its manager writes with. Matching the indent matters: a migrated
# file and one the editor re-saves must be byte-identical, or every later save
# produces a whole-file diff that hides the real change.
MIGRATIONS = {
    "blueprints": (migrate_blueprint, 4),
    "levels": (migrate_level, 4),
    "prop_recipes": (migrate_prop_recipe, 2),
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


def migrate_level_blueprint_state_keys(
    definitions_root: Path, dry_run: bool
) -> list[Path]:
    """Replaces fragile placed-entity state indices with Blueprint-local keys."""
    blueprints_directory = definitions_root / "blueprints"
    levels_directory = definitions_root / "levels"
    if not blueprints_directory.is_dir() or not levels_directory.is_dir():
        raise ValueError("Blueprint state-key migration requires blueprint and level directories")

    blueprints: dict[str, list[dict]] = {}
    for blueprint_path in sorted(blueprints_directory.glob("*.json")):
        blueprint = load_document(blueprint_path)
        migrate_blueprint(blueprint)
        blueprint_id = blueprint.get("id")
        states = blueprint.get("states")
        if not isinstance(blueprint_id, str) or not blueprint_id or not isinstance(states, list):
            raise ValueError(f"Blueprint identity or states are invalid: {blueprint_path}")
        if blueprint_id in blueprints:
            raise ValueError(f"Duplicate Blueprint ID during level migration: {blueprint_id}")

        state_keys: set[str] = set()
        for state in states:
            key = state.get("key") if isinstance(state, dict) else None
            if (
                not isinstance(key, str)
                or re.fullmatch(r"[a-z0-9](?:[a-z0-9-]*[a-z0-9])?", key) is None
                or key in state_keys
            ):
                raise ValueError(
                    f"Blueprint has an invalid or duplicate state key: {blueprint_path}"
                )
            state_keys.add(key)
        blueprints[blueprint_id] = states

    updates: list[tuple[Path, dict]] = []
    for level_path in sorted(levels_directory.glob("*.json")):
        level = load_document(level_path)
        changed = False
        for layer in level.get("layers", []):
            for entity in layer.get("entities", []):
                has_index = "blueprint_state_index" in entity
                has_key = "blueprint_state_key" in entity
                if has_index == has_key:
                    raise ValueError(
                        "Level entity must contain exactly one Blueprint state identity: "
                        f"{level_path}"
                    )

                blueprint_id = entity.get("blueprint_id")
                if not isinstance(blueprint_id, str):
                    raise ValueError(f"Level entity has an invalid Blueprint ID: {level_path}")

                if not blueprint_id:
                    if has_index:
                        state_index = entity["blueprint_state_index"]
                        if (
                            not isinstance(state_index, int)
                            or isinstance(state_index, bool)
                            or state_index != 0
                        ):
                            raise ValueError(
                                "Unbound level entity has a non-default Blueprint state index: "
                                f"{level_path}"
                            )
                        entity["blueprint_state_key"] = ""
                        del entity["blueprint_state_index"]
                        changed = True
                    elif entity["blueprint_state_key"] != "":
                        raise ValueError(
                            f"Unbound level entity has a Blueprint state key: {level_path}"
                        )
                    continue

                states = blueprints.get(blueprint_id)
                if states is None:
                    raise ValueError(
                        f"Level entity references an unknown Blueprint {blueprint_id}: {level_path}"
                    )

                if has_index:
                    state_index = entity["blueprint_state_index"]
                    if (
                        not isinstance(state_index, int)
                        or isinstance(state_index, bool)
                        or state_index < 0
                        or state_index >= len(states)
                    ):
                        raise ValueError(
                            f"Level entity has an invalid Blueprint state index: {level_path}"
                        )
                    state = states[state_index]
                    entity["blueprint_state_key"] = state["key"]
                    del entity["blueprint_state_index"]
                    changed = True
                else:
                    state_key = entity["blueprint_state_key"]
                    state = next((state for state in states if state["key"] == state_key), None)
                    if state is None:
                        raise ValueError(
                            f"Level entity has an unknown Blueprint state key: {level_path}"
                        )

                if entity.get("sprite_id") != state.get("sprite_id") or entity.get(
                    "collider_id"
                ) != state.get("collider_id"):
                    raise ValueError(
                        f"Level entity assets do not match its Blueprint state: {level_path}"
                    )

        if changed:
            updates.append((level_path, level))

    if not dry_run:
        for level_path, level in updates:
            write_document(level_path, level, 4)
    return [path for path, _ in updates]


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
    if (args.definitions / "levels").is_dir():
        changed = migrate_parallax_themes(args.definitions, args.dry_run)
        total += len(changed)
        for path in changed:
            print(f"{'would migrate' if args.dry_run else 'migrated'}: {path}")
    if (args.definitions / "parallax_themes").is_dir():
        changed = migrate_parallax_layer_compositions(args.definitions, args.dry_run)
        total += len(changed)
        for path in changed:
            print(f"{'would migrate' if args.dry_run else 'migrated'}: {path}")
    if (args.definitions / "source_artworks").is_dir():
        changed = migrate_source_artwork(args.definitions, args.dry_run)
        total += len(changed)
        for path in changed:
            print(f"{'would migrate' if args.dry_run else 'migrated'}: {path}")
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

    if (args.definitions / "blueprints").is_dir() and (args.definitions / "levels").is_dir():
        changed = migrate_level_blueprint_state_keys(args.definitions, args.dry_run)
        total += len(changed)
        for path in changed:
            print(f"{'would migrate' if args.dry_run else 'migrated'}: {path}")

    if total == 0:
        print("All definitions are current.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
