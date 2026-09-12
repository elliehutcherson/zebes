"""Behavioral checks for the reusable atlas, including geometry failure paths."""

import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops, ImageDraw

from scripts.prepare_sprite_boot_atlas import (
    CONFIG, DOCUMENT, DONOR, LANDMARKS, ROOT, TRACE, atlas_rows,
    bake_atlas, coat_image, render_leg, surface_mask,
)
from scripts.prepare_sprite_sequence import geometry_rows


class SpriteBootAtlasTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "geometry"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        str(ROOT / "scripts/sprite_sequence_geometry.cc"), "-o", str(cls.binary)], check=True)
        cls.document, cls.trace, cls.landmarks, cls.config = [json.loads(p.read_text()) for p in (DOCUMENT, TRACE, LANDMARKS, CONFIG)]
        cls.rows = atlas_rows(cls.document, cls.trace, cls.landmarks, cls.config)
        cls.geometry = json.loads(cls.run_geometry(cls.rows).stdout)
        cls.atlas = bake_atlas(Image.open(DONOR))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def run_geometry(cls, rows, mode="--atlas"):
        return subprocess.run([str(cls.binary), mode], input=rows, text=True, capture_output=True)

    def test_all_anchors_and_recovery_triangles_remain_registered(self):
        legacy = json.loads(subprocess.run([str(self.binary)], input=geometry_rows(self.document, self.trace, self.landmarks),
                                          text=True, capture_output=True, check=True).stdout)
        for original, frame in zip(self.document["frames"], self.geometry["frames"], strict=True):
            for leg in frame["legs"]:
                suffix = "l" if leg["side"] == "near" else "r"
                a = leg["anchors"]
                for key in ("knee", "ankle", "toe"):
                    self.assertLess(math.dist(a[key], original["pose"][key+"_"+suffix]), 1e-10)
                shin = [a["knee"][i]-a["ankle"][i] for i in (0, 1)]
                shaft = [a["cuff"][i]-a["ankle"][i] for i in (0, 1)]
                self.assertAlmostEqual(shin[0]*shaft[1]-shin[1]*shaft[0], 0, places=8)
                if (frame["name"], leg["side"]) in (("reference_10", "near"), ("reference_04", "far")):
                    control = next(x for x in legacy if x["name"] == frame["name"] and x["side"] == leg["side"])
                    self.assertEqual(a["heel"], control["heel"])
                    self.assertLess(a["heel"][1], a["toe"][1])
                    self.assertEqual(leg["view"], "profile")

    def test_ground_contacts_and_flight_clearance_are_geometric(self):
        modes = []
        for frame in self.geometry["frames"]:
            dy = frame["translation"][1]
            support = [leg for leg in frame["legs"] if leg["contact_mode"] != "none"]
            self.assertLessEqual(len(support), 1)
            for leg in support:
                self.assertAlmostEqual(leg["anchors"]["contact"][1]+dy, 214, places=9)
                modes.append(leg["contact_mode"])
            bottom = max(p[1]+dy for leg in frame["legs"] for surface in leg["surfaces"] for p in surface["outline"])
            self.assertLessEqual(bottom, 214.15)
            if not support:
                self.assertGreater(dy, 0)
                self.assertGreaterEqual(214-bottom, 6-1e-10)
        self.assertEqual(len(modes), 10)
        self.assertEqual(set(modes), {"flat", "heel", "toe"})
        self.assertEqual(self.geometry["frames"][0]["legs"][1]["view"], "sole_visible")

    def test_geometry_and_texture_masks_form_connected_complete_legs(self):
        for frame in self.geometry["frames"]:
            for leg in frame["legs"]:
                rendered = render_leg(leg, self.atlas)
                mask = rendered.getchannel("A").point(lambda value: 255 if value >= 128 else 0)
                draw_mask = Image.new("L", (768, 768))
                for surface in leg["surfaces"]:
                    draw_mask = ImageChops.lighter(draw_mask, surface_mask(surface))
                expected = draw_mask.resize((256, 256), Image.Resampling.LANCZOS)
                self.assertEqual(rendered.getchannel("A").tobytes(), expected.tobytes())
                ImageDraw.floodfill(mask, tuple(map(round, leg["anchors"]["ankle"])), 128)
                self.assertNotIn(255, mask.get_flattened_data(), frame["name"] + leg["side"])

    def test_reuse_changes_free_shins_and_preserves_support_and_lengths(self):
        alternate = json.loads(self.run_geometry(self.rows, "--atlas-flex").stdout)
        changed = 0
        for first, second in zip(self.geometry["frames"], alternate["frames"], strict=True):
            if any(leg["contact_mode"] != "none" for leg in first["legs"]):
                self.assertEqual(first["translation"], second["translation"])
            for before, after in zip(first["legs"], second["legs"], strict=True):
                a, b = before["anchors"], after["anchors"]
                self.assertEqual(before["view"], after["view"])
                if before["contact_mode"] != "none":
                    self.assertEqual(before, after)
                    continue
                for start, end in (("hip", "knee"), ("knee", "ankle"), ("ankle", "toe"), ("heel", "toe")):
                    self.assertAlmostEqual(math.dist(a[start], a[end]), math.dist(b[start], b[end]), places=9)
                self.assertGreater(math.dist(a["ankle"], b["ankle"]), 1)
                changed += 1
        self.assertEqual(changed, 14)

    def test_cyclic_coat_lag_and_fixed_torso(self):
        frames = self.geometry["frames"]
        shifted_rows = self.rows.splitlines()[6:] + self.rows.splitlines()[:6]
        rotated = json.loads(self.run_geometry("\n".join(shifted_rows)+"\n").stdout)
        self.assertEqual([f["coat"] for f in rotated["frames"]], [f["coat"] for f in frames[3:]+frames[:3]])
        body = Image.open(ROOT / "experiments/sprite_sequence/inputs/baseline-v1/part-poses/body/reference_10.png").convert("RGBA")
        control = frames[9]["coat"]
        warped = coat_image(body, control)
        box = (0, 0, 256, control["attachment_y"])
        self.assertEqual(warped.crop(box).tobytes(), body.crop(box).tobytes())
        self.assertNotEqual(warped.tobytes(), body.tobytes())

    def test_invalid_geometry_never_emits_partial_atlas(self):
        rows = self.rows.splitlines()
        invalid = ["\n".join(rows[:-1])+"\n", self.rows.replace("sole_visible", "unknown", 1),
                   self.rows + rows[0]+"\n", self.rows.replace("sole_visible heel 3", "sole_visible toe 3", 1),
                   self.rows.replace("profile none 0", "profile none nan", 1),
                   self.rows.replace(" heel ", " none ").replace(" flat ", " none ").replace(" toe ", " none ")]
        for data in invalid:
            with self.subTest(data=data[:70]):
                result = self.run_geometry(data)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)

    def test_original_profile_evidence_is_still_reproducible(self):
        base = ROOT / "experiments/sprite_sequence/evidence/profile-v2"
        result = subprocess.run([str(self.binary)], input=(base / "geometry-input.txt").read_text(),
                                text=True, capture_output=True, check=True)
        self.assertEqual(json.loads(result.stdout), json.loads((base / "geometry.json").read_text()))


if __name__ == "__main__":
    unittest.main()
