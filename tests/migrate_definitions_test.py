import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "migrate_definitions.py"
SPEC = importlib.util.spec_from_file_location("migrate_definitions", SCRIPT_PATH)
migrate_definitions = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = migrate_definitions
SPEC.loader.exec_module(migrate_definitions)


class MigrateDefinitionsTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.assets_root = Path(self._temp.name)
        self.root = self.assets_root / "definitions"
        self.root.mkdir()
        (self.root / "blueprints").mkdir()
        (self.root / "levels").mkdir()
        (self.root / "prop_recipes").mkdir()
        (self.root / "source_artworks").mkdir()
        (self.root / "parallax_themes").mkdir()
        (self.root / "sprites").mkdir()
        (self.root / "terrain_recipes").mkdir()
        (self.root / "textures").mkdir()
        (self.root / "tilesets").mkdir()
        self.addCleanup(self._temp.cleanup)

    def write_level(self, name, document):
        path = self.root / "levels" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def write_blueprint(self, name, document):
        path = self.root / "blueprints" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def write_source_artwork(self, artwork_id, schema_version=1, current=False):
        relative_directory = "source_art" if current else "source_art/props"
        image_path = self.assets_root / relative_directory / f"{artwork_id}.png"
        image_path.parent.mkdir(parents=True, exist_ok=True)
        image_path.write_bytes(b"retained png bytes")
        definition_path = self.root / "source_artworks" / f"{artwork_id}.json"
        definition_path.write_text(
            json.dumps(
                {
                    "schema_version": schema_version,
                    "id": artwork_id,
                    "name": "Cave source",
                    "source_path": f"{relative_directory}/{artwork_id}.png",
                    "provenance": {
                        "kind": "imported",
                        "original_filename": "cave.png",
                        "imported_at_utc": "2026-08-23T12:00:00Z",
                    },
                    "width": 1,
                    "height": 1,
                    "content_digest": "0" * 64,
                }
            ),
            encoding="utf-8",
        )
        return definition_path, image_path

    def test_source_artwork_moves_to_neutral_directory(self):
        definition, legacy_image = self.write_source_artwork("source-1")

        changed = migrate_definitions.migrate_source_artwork(self.root, dry_run=False)

        self.assertEqual(changed, [definition])
        current_image = self.assets_root / "source_art/source-1.png"
        self.assertFalse(legacy_image.exists())
        self.assertEqual(current_image.read_bytes(), b"retained png bytes")
        document = json.loads(definition.read_text(encoding="utf-8"))
        self.assertEqual(document["schema_version"], 2)
        self.assertEqual(document["source_path"], "source_art/source-1.png")
        self.assertEqual(
            migrate_definitions.migrate_source_artwork(self.root, dry_run=False), []
        )

    def test_source_artwork_dry_run_changes_nothing(self):
        definition, legacy_image = self.write_source_artwork("source-1")

        changed = migrate_definitions.migrate_source_artwork(self.root, dry_run=True)

        self.assertEqual(changed, [definition])
        self.assertTrue(legacy_image.exists())
        self.assertEqual(
            json.loads(definition.read_text(encoding="utf-8"))["schema_version"], 1
        )

    def test_source_artwork_migration_preflights_every_definition(self):
        _, first_image = self.write_source_artwork("source-1")
        self.write_source_artwork("source-2")
        conflicting = self.assets_root / "source_art/source-2.png"
        conflicting.parent.mkdir(parents=True, exist_ok=True)
        conflicting.write_bytes(b"conflict")

        with self.assertRaisesRegex(ValueError, "missing or conflicting"):
            migrate_definitions.migrate_source_artwork(self.root, dry_run=False)

        self.assertTrue(first_image.exists())
        self.assertFalse(
            (self.assets_root / "source_art/source-1.png").exists()
        )

    def test_source_artwork_migration_refuses_half_migrated_state(self):
        definition, legacy_image = self.write_source_artwork("source-1")
        current_image = self.assets_root / "source_art/source-1.png"
        current_image.parent.mkdir(parents=True, exist_ok=True)
        legacy_image.replace(current_image)

        with self.assertRaisesRegex(ValueError, "missing or conflicting"):
            migrate_definitions.migrate_source_artwork(self.root, dry_run=False)

        self.assertEqual(
            json.loads(definition.read_text(encoding="utf-8"))["schema_version"], 1
        )

    def write_parallax_texture(self, texture_id, width, height):
        image_directory = self.assets_root / "textures" / "test_images"
        image_directory.mkdir(parents=True, exist_ok=True)
        image_path = image_directory / f"{texture_id}.png"
        image_path.write_bytes(
            migrate_definitions.PNG_SIGNATURE
            + struct.pack(">I", 13)
            + b"IHDR"
            + struct.pack(">II", width, height)
        )
        definition_path = self.root / "textures" / f"{texture_id}.json"
        definition_path.write_text(
            json.dumps(
                {
                    "id": texture_id,
                    "name": texture_id,
                    "path": f"test_images/{texture_id}.png",
                }
            ),
            encoding="utf-8",
        )

    def write_parallax_theme(self, name, document):
        path = self.root / "parallax_themes" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def test_blueprint_states_gain_explicit_grounded_placement(self):
        path = self.write_blueprint(
            "crystal.json",
            {
                "id": "crystal",
                "name": "Crystal",
                "states": [
                    {"name": "Idle", "collider_id": "", "sprite_id": "sprite"}
                ],
            },
        )

        changed = migrate_definitions.migrate_directory(
            self.root, "blueprints", dry_run=False
        )

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(document["states"][0]["placement_mode"], "grounded")

    def test_embedded_parallax_themes_are_extracted_deterministically(self):
        path = self.write_level(
            "cave.json",
            {
                "id": "level-1",
                "name": "Cave",
                "themes": [
                    {
                        "id": 7,
                        "name": "Blue",
                        "layers": [{"name": "Far", "texture_id": "texture-1"}],
                    }
                ],
                "zones": [{"id": 2, "name": "Entry", "theme_id": 7}],
                "layers": [],
            },
        )

        changed = migrate_definitions.migrate_parallax_themes(self.root, dry_run=False)

        expected_id = migrate_definitions.parallax_theme_id("level-1", 7)
        theme_path = self.root / "parallax_themes" / f"{expected_id}.json"
        self.assertEqual(changed, [path, theme_path])
        level = json.loads(path.read_text(encoding="utf-8"))
        self.assertNotIn("themes", level)
        self.assertEqual(level["zones"][0]["theme_id"], expected_id)
        theme = json.loads(theme_path.read_text(encoding="utf-8"))
        self.assertEqual(theme["name"], "Cave — Blue")
        self.assertEqual(theme["layers"][0]["texture_id"], "texture-1")
        self.assertEqual(
            migrate_definitions.migrate_parallax_themes(self.root, dry_run=False), []
        )

    def test_parallax_repeat_flags_become_explicit_composition_periods(self):
        self.write_parallax_texture("cave-far", 320, 180)
        path = self.write_parallax_theme(
            "cave.json",
            {
                "id": "theme-1",
                "name": "Cave",
                "layers": [
                    {
                        "name": "Far formations",
                        "texture_id": "cave-far",
                        "scroll_factor_x": 0.1,
                        "scroll_factor_y": 0.05,
                        "offset_x": -12.0,
                        "offset_y": 8.0,
                        "base_scale": 2.0,
                        "repeat_x": True,
                        "repeat_y": False,
                    }
                ],
            },
        )

        changed = migrate_definitions.migrate_parallax_layer_compositions(
            self.root, dry_run=False
        )

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(document["schema_version"], 2)
        layer = document["layers"][0]
        self.assertEqual(layer["repeat_period_x"], 640.0)
        self.assertEqual(layer["repeat_period_y"], 0.0)
        self.assertNotIn("repeat_x", layer)
        self.assertNotIn("texture_id", layer)
        self.assertEqual(
            layer["elements"],
            [
                {
                    "id": 0,
                    "name": "Far formations",
                    "texture_id": "cave-far",
                    "position_x": 0.0,
                    "position_y": 0.0,
                    "scale": 2.0,
                }
            ],
        )
        self.assertEqual(
            migrate_definitions.migrate_parallax_layer_compositions(
                self.root, dry_run=False
            ),
            [],
        )

    def test_parallax_composition_migration_preflights_every_theme(self):
        self.write_parallax_texture("cave-far", 320, 180)
        valid = self.write_parallax_theme(
            "a-valid.json",
            {
                "id": "theme-1",
                "name": "Cave",
                "layers": [
                    {
                        "name": "Far",
                        "texture_id": "cave-far",
                        "scroll_factor_x": 0.0,
                        "scroll_factor_y": 0.0,
                        "offset_x": 0.0,
                        "offset_y": 0.0,
                        "base_scale": 1.0,
                        "repeat_x": True,
                        "repeat_y": True,
                    }
                ],
            },
        )
        before = valid.read_bytes()
        self.write_parallax_theme(
            "z-invalid.json",
            {
                "id": "theme-2",
                "name": "Broken",
                "layers": [
                    {
                        "name": "Near",
                        "texture_id": "missing",
                        "scroll_factor_x": 0.5,
                        "scroll_factor_y": 0.5,
                        "offset_x": 0.0,
                        "offset_y": 0.0,
                        "base_scale": 1.0,
                        "repeat_x": False,
                        "repeat_y": False,
                    }
                ],
            },
        )

        with self.assertRaisesRegex(ValueError, "missing texture"):
            migrate_definitions.migrate_parallax_layer_compositions(
                self.root, dry_run=False
            )
        self.assertEqual(valid.read_bytes(), before)

    def test_current_parallax_theme_with_legacy_fields_is_refused(self):
        self.write_parallax_theme(
            "half.json",
            {
                "schema_version": 2,
                "id": "theme-1",
                "name": "Half",
                "layers": [
                    {
                        "name": "Far",
                        "repeat_period_x": 100.0,
                        "repeat_period_y": 0.0,
                        "elements": [],
                        "repeat_x": True,
                    }
                ],
            },
        )

        with self.assertRaisesRegex(ValueError, "retains legacy"):
            migrate_definitions.migrate_parallax_layer_compositions(
                self.root, dry_run=False
            )

    def test_conflicting_extracted_theme_is_refused_before_level_changes(self):
        path = self.write_level(
            "cave.json",
            {
                "id": "level-1",
                "name": "Cave",
                "themes": [{"id": 7, "name": "Blue", "layers": []}],
                "zones": [{"id": 2, "name": "Entry", "theme_id": 7}],
            },
        )
        before = path.read_bytes()
        theme_id = migrate_definitions.parallax_theme_id("level-1", 7)
        directory = self.root / "parallax_themes"
        directory.mkdir(exist_ok=True)
        (directory / f"{theme_id}.json").write_text(
            json.dumps({"id": theme_id, "name": "Independent", "layers": []}),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(ValueError, "conflicts"):
            migrate_definitions.migrate_parallax_themes(self.root, dry_run=False)
        self.assertEqual(path.read_bytes(), before)

    def test_half_migrated_level_is_refused(self):
        self.write_level(
            "cave.json",
            {
                "id": "level-1",
                "name": "Cave",
                "zones": [{"id": 2, "name": "Entry", "theme_id": 7}],
            },
        )
        with self.assertRaisesRegex(ValueError, "Half-migrated"):
            migrate_definitions.migrate_parallax_themes(self.root, dry_run=False)

    def test_authored_blueprint_placement_is_not_reinterpreted(self):
        path = self.write_blueprint(
            "lamp.json",
            {
                "id": "lamp",
                "name": "Lamp",
                "states": [
                    {
                        "name": "Idle",
                        "collider_id": "",
                        "sprite_id": "sprite",
                        "placement_mode": "ceiling",
                    }
                ],
            },
        )
        before = path.read_bytes()

        changed = migrate_definitions.migrate_directory(
            self.root, "blueprints", dry_run=False
        )

        self.assertEqual(changed, [])
        self.assertEqual(path.read_bytes(), before)

    def write_tileset(self, name, document):
        path = self.root / "tilesets" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def write_recipe(self, name, document):
        path = self.root / "terrain_recipes" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def write_prop_recipe(self, name, document):
        path = self.root / "prop_recipes" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    @staticmethod
    def v1_prop_recipe():
        return {
            "schema_version": 1,
            "pipeline_version": 1,
            "id": "prop-1",
            "pipeline": {
                "composition": {
                    "canvas_tiles_wide": 3,
                    "canvas_tiles_high": 2,
                    "padding_fraction": 0.06,
                },
                "cleanup": {
                    "alpha_threshold": 128,
                    "minimum_component_area": 2,
                    "grounded_tolerance": 3,
                },
            },
        }

    @staticmethod
    def v2_recipe():
        return {
            "id": "r",
            "name": "Cave",
            "schema_version": 2,
            "config": {"surface": {"top_depth": 9.0}},
        }

    def write_sprite(self, name, document):
        path = self.root / "sprites" / name
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def read_sprite(self, path):
        return json.loads(path.read_text(encoding="utf-8"))

    def test_missing_offsets_become_explicit_zeros(self):
        path = self.write_sprite(
            "walk.json",
            {"id": "a", "name": "Walk", "texture_id": "t", "frames": [{"index": 0}]},
        )

        changed = migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)

        self.assertEqual(changed, [path])
        frame = self.read_sprite(path)["frames"][0]
        self.assertEqual(frame["offset_x"], 0)
        self.assertEqual(frame["offset_y"], 0)

    # An authored offset is the whole reason the field exists; a migration that
    # overwrote one would silently move artwork.
    def test_authored_offsets_are_left_alone(self):
        path = self.write_sprite(
            "idle.json",
            {
                "id": "b",
                "name": "Idle",
                "texture_id": "t",
                "frames": [{"index": 0, "offset_x": -4, "offset_y": 7}],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)

        self.assertEqual(changed, [])
        frame = self.read_sprite(path)["frames"][0]
        self.assertEqual(frame["offset_x"], -4)
        self.assertEqual(frame["offset_y"], 7)

    def test_a_half_migrated_frame_gains_only_the_missing_field(self):
        path = self.write_sprite(
            "jump.json",
            {"id": "c", "name": "Jump", "texture_id": "t", "frames": [{"offset_x": 3}]},
        )

        migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)

        frame = self.read_sprite(path)["frames"][0]
        self.assertEqual(frame["offset_x"], 3)
        self.assertEqual(frame["offset_y"], 0)

    def test_running_twice_changes_nothing_the_second_time(self):
        self.write_sprite(
            "walk.json",
            {"id": "a", "name": "Walk", "texture_id": "t", "frames": [{"index": 0}]},
        )

        migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)
        again = migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)

        self.assertEqual(again, [])

    def test_dry_run_reports_without_writing(self):
        path = self.write_sprite(
            "walk.json",
            {"id": "a", "name": "Walk", "texture_id": "t", "frames": [{"index": 0}]},
        )

        changed = migrate_definitions.migrate_directory(self.root, "sprites", dry_run=True)

        self.assertEqual(changed, [path])
        self.assertNotIn("offset_x", self.read_sprite(path)["frames"][0])

    def test_a_missing_directory_fails_fast(self):
        with self.assertRaisesRegex(ValueError, "does not exist"):
            migrate_definitions.migrate_directory(self.root / "nope", "sprites", dry_run=False)

    # The managers write with nlohmann's dump(4), which sorts keys and appends
    # no terminator. A migrated file that differs in layout would make the next
    # editor save look like a whole-file rewrite.
    def test_output_matches_what_the_manager_writes(self):
        path = self.write_sprite(
            "walk.json",
            {"texture_id": "t", "name": "Walk", "id": "a", "frames": [{"index": 0}]},
        )

        migrate_definitions.migrate_directory(self.root, "sprites", dry_run=False)

        text = path.read_text(encoding="utf-8")
        self.assertFalse(text.endswith("\n"))
        self.assertIn('\n    "frames": [', text)
        self.assertLess(text.index('"frames"'), text.index('"id"'))


    def test_a_v2_recipe_gains_edge_detail_switched_off(self):
        path = self.write_recipe("cave.json", self.v2_recipe())

        changed = migrate_definitions.migrate_directory(
            self.root, "terrain_recipes", dry_run=False
        )

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(document["schema_version"], 3)
        edge = document["config"]["surface"]["edge_detail"]
        # Family None is the whole point: a v2 recipe drew no edge decoration,
        # and inheriting a future default would change its pixels.
        self.assertEqual(edge["family"], 0)
        self.assertEqual(edge["length"], 4)

    def test_the_rest_of_the_recipe_is_untouched(self):
        path = self.write_recipe("cave.json", self.v2_recipe())

        migrate_definitions.migrate_directory(self.root, "terrain_recipes", dry_run=False)

        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(document["config"]["surface"]["top_depth"], 9.0)
        self.assertEqual(document["name"], "Cave")

    def test_a_current_recipe_is_left_alone(self):
        document = self.v2_recipe()
        document["schema_version"] = 3
        document["config"]["surface"]["edge_detail"] = {"family": 2, "length": 9}
        path = self.write_recipe("cave.json", document)

        changed = migrate_definitions.migrate_directory(
            self.root, "terrain_recipes", dry_run=False
        )

        self.assertEqual(changed, [])
        edge = json.loads(path.read_text(encoding="utf-8"))["config"]["surface"]["edge_detail"]
        self.assertEqual(edge["family"], 2)

    # v1 stored a different surface record and none has ever existed here.
    # Guessing at one would silently invent a look nobody authored.
    def test_an_unmigratable_version_fails_fast(self):
        document = self.v2_recipe()
        document["schema_version"] = 1
        self.write_recipe("old.json", document)

        with self.assertRaisesRegex(ValueError, "only 2 is supported"):
            migrate_definitions.migrate_directory(self.root, "terrain_recipes", dry_run=False)

    def test_v1_prop_recipe_becomes_explicitly_grounded_without_losing_settings(self):
        path = self.write_prop_recipe("tree.json", self.v1_prop_recipe())

        changed = migrate_definitions.migrate_directory(
            self.root, "prop_recipes", dry_run=False
        )

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(document["schema_version"], 2)
        self.assertEqual(document["pipeline_version"], 2)
        self.assertEqual(
            document["pipeline"]["composition"]["attachment"],
            {"mode": "grounded", "free_anchor": None},
        )
        cleanup = document["pipeline"]["cleanup"]
        self.assertEqual(cleanup["contact_tolerance"], 3)
        self.assertNotIn("grounded_tolerance", cleanup)
        self.assertEqual(document["pipeline"]["composition"]["padding_fraction"], 0.06)

    def test_current_prop_recipe_is_left_byte_untouched(self):
        document = self.v1_prop_recipe()
        document["schema_version"] = 2
        document["pipeline_version"] = 2
        document["pipeline"]["composition"]["attachment"] = {
            "mode": "free",
            "free_anchor": {"x": 12, "y": 7},
        }
        document["pipeline"]["cleanup"]["contact_tolerance"] = document["pipeline"][
            "cleanup"
        ].pop("grounded_tolerance")
        path = self.write_prop_recipe("lamp.json", document)
        before = path.read_bytes()

        changed = migrate_definitions.migrate_directory(
            self.root, "prop_recipes", dry_run=False
        )

        self.assertEqual(changed, [])
        self.assertEqual(path.read_bytes(), before)

    def test_ambiguous_v1_prop_attachment_is_refused(self):
        document = self.v1_prop_recipe()
        document["pipeline"]["composition"]["attachment"] = {
            "mode": "ceiling",
            "free_anchor": None,
        }
        self.write_prop_recipe("ambiguous.json", document)

        with self.assertRaisesRegex(ValueError, "already contains attachment"):
            migrate_definitions.migrate_directory(self.root, "prop_recipes", dry_run=False)


    # An absent list and an empty one always meant the same thing. Writing both
    # unconditionally is what lets the reader require them.
    def test_a_tileset_without_terrains_gains_an_empty_list(self):
        path = self.write_tileset(
            "grass.json", {"id": "t", "name": "Grass", "texture_id": "x", "tiles": []}
        )

        changed = migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        self.assertEqual(changed, [path])
        self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["terrains"], [])

    def test_members_become_shape_tiles_keeping_their_ids(self):
        # "member" outlived the concept of membership: the list used to mark
        # tiles the brush must never rewrite, and now records a scheme's
        # shape-to-artwork table. Renaming without carrying the contents would
        # silently drop a terrain's slope units.
        path = self.write_tileset(
            "grass.json",
            {
                "id": "t",
                "name": "Grass",
                "texture_id": "x",
                "tiles": [],
                "terrains": [{"id": 1, "name": "Dirt", "member_tile_ids": [7]}, {"id": 2}],
            },
        )

        migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        terrains = json.loads(path.read_text(encoding="utf-8"))["terrains"]
        self.assertEqual(terrains[0]["shape_tile_ids"], [7])
        self.assertNotIn("member_tile_ids", terrains[0])
        self.assertEqual(terrains[1]["shape_tile_ids"], [])

    def test_every_terrain_gains_a_derived_tile_list(self):
        # Every terrain on disk predates deriving artwork, so the list is empty
        # -- but present, because a reader must never have to tell an absent
        # field from an empty one.
        path = self.write_tileset(
            "grass.json",
            {
                "id": "t",
                "name": "Grass",
                "texture_id": "x",
                "tiles": [],
                "terrains": [{"id": 1, "member_tile_ids": []}],
            },
        )

        migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        terrains = json.loads(path.read_text(encoding="utf-8"))["terrains"]
        self.assertEqual(terrains[0]["derived_tiles"], [])

    # A derived tile is named "<terrain> <shape identifier>", so the identifier
    # spelling is data on disk, not just a C++ token. Both renames have to be
    # reachable from one run: a tileset written before either one is still the
    # oldest thing this migration has to move.
    def test_a_tile_name_from_before_both_renames_arrives_current(self):
        path = self.write_tileset(
            "grass.json",
            {
                "id": "t",
                "name": "Grass",
                "texture_id": "x",
                "tiles": [{"id": 1, "name": "grass kGentleSlopeBottomLeft_Lower", "shape": 10}],
                "terrains": [],
            },
        )

        migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        tiles = json.loads(path.read_text(encoding="utf-8"))["tiles"]
        self.assertEqual(tiles[0]["name"], "grass kGentleSlopeFloorTallRightLower")
        # The shape is a number and means what it always meant.
        self.assertEqual(tiles[0]["shape"], 10)

    def test_the_vocabulary_rename_maps_to_the_mirrored_side(self):
        # kSlope45BottomLeft named the side the wedge tapered to nothing on;
        # kSlope45FloorTallRight names the side at full height. Same shape,
        # opposite word. Getting this backwards would mirror every ramp on disk,
        # so it is pinned rather than left to the reader of the rename table.
        path = self.write_tileset(
            "grass.json",
            {
                "id": "t",
                "name": "Grass",
                "texture_id": "x",
                "tiles": [
                    {"id": 1, "name": "grass kSlope45BottomLeft", "shape": 6},
                    {"id": 2, "name": "grass kSteepSlopeTopRightTop", "shape": 25},
                ],
                "terrains": [],
            },
        )

        migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        tiles = json.loads(path.read_text(encoding="utf-8"))["tiles"]
        self.assertEqual(tiles[0]["name"], "grass kSlope45FloorTallRight")
        self.assertEqual(tiles[1]["name"], "grass kSteepSlopeCeilingTallLeftTop")

    def test_a_current_tileset_is_left_alone(self):
        self.write_tileset(
            "grass.json",
            {
                "id": "t",
                "name": "Grass",
                "texture_id": "x",
                "tiles": [],
                "terrains": [{"id": 1, "shape_tile_ids": [], "derived_tiles": []}],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "tilesets", dry_run=False)

        self.assertEqual(changed, [])

    def test_entities_gain_an_explicit_draw_order(self):
        path = self.write_level(
            "cave.json",
            {
                "id": "l",
                "name": "Cave",
                "tile_chunks": [],
                "entities": [{"id": 1}, {"id": 2}],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "levels", dry_run=False)

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        entities = document["layers"][0]["entities"]
        # Zero for every entity, because entities used to draw in ascending ID
        # order and ties still resolve that way. Any other value would reorder
        # levels that were authored before the field existed.
        self.assertEqual([entity["sort_order"] for entity in entities], [0, 0])

    def test_an_authored_draw_order_is_left_alone(self):
        path = self.write_level(
            "cave.json",
            {
                "id": "l",
                "name": "Cave",
                "layers": [
                    {
                        "id": 0,
                        "name": "Base",
                        "tile_chunks": [],
                        "entities": [{"id": 1, "sort_order": 7}],
                    }
                ],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "levels", dry_run=False)

        self.assertEqual(changed, [])
        entities = json.loads(path.read_text(encoding="utf-8"))["layers"][0][
            "entities"
        ]
        self.assertEqual(entities[0]["sort_order"], 7)

    def test_root_collections_are_wrapped_without_changing_contents(self):
        path = self.write_level(
            "empty.json",
            {
                "id": "l",
                "name": "Empty",
                "tile_chunks": [{"chunk_id": 2, "tiles": [4]}],
                "entities": [{"id": 9, "sort_order": 2}],
                "themes": [],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "levels", dry_run=False)

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertNotIn("tile_chunks", document)
        self.assertNotIn("entities", document)
        self.assertEqual(
            document["layers"],
            [
                {
                    "id": 0,
                    "name": "Base",
                    "tile_chunks": [{"chunk_id": 2, "tiles": [4]}],
                    "entities": [{"id": 9, "sort_order": 2}],
                }
            ],
        )

    def test_current_layer_format_is_idempotent(self):
        path = self.write_level(
            "current.json",
            {
                "id": "l",
                "name": "Current",
                "layers": [
                    {
                        "id": 3,
                        "name": "World",
                        "tile_chunks": [],
                        "entities": [],
                    }
                ],
            },
        )

        first = migrate_definitions.migrate_directory(
            self.root, "levels", dry_run=False
        )
        second = migrate_definitions.migrate_directory(
            self.root, "levels", dry_run=False
        )

        self.assertEqual(first, [])
        self.assertEqual(second, [])
        self.assertEqual(
            json.loads(path.read_text(encoding="utf-8"))["layers"][0]["id"], 3
        )

    def test_ambiguous_or_partial_layer_format_is_refused(self):
        documents = [
            {"layers": [], "tile_chunks": [], "entities": []},
            {"tile_chunks": []},
            {"entities": []},
            {"name": "missing all collections"},
        ]
        for index, document in enumerate(documents):
            with self.subTest(index=index):
                with self.assertRaises(ValueError):
                    migrate_definitions.migrate_level(document)

    def test_the_level_wide_parallax_list_is_dropped(self):
        # Themes and zones replaced it. The flat list stayed in the format long
        # after the last reader of it went away, so a level carried layers no
        # editor could reach and no renderer drew.
        path = self.write_level(
            "cave.json",
            {
                "id": "l",
                "name": "Cave",
                "tile_chunks": [],
                "entities": [],
                "themes": [],
                "parallax_layers": [{"name": "Layer 0", "texture_id": "t"}],
            },
        )

        changed = migrate_definitions.migrate_directory(self.root, "levels", dry_run=False)

        self.assertEqual(changed, [path])
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertNotIn("parallax_layers", document)
        self.assertEqual(document["themes"], [])


if __name__ == "__main__":
    unittest.main()
