"""End-to-end contract tests for the standalone C++ experiment, outside CMake."""

import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.prepare_sprite_sequence import DOCUMENT, TRACE, LANDMARKS, ROOT, geometry_rows, leg_image
from scripts.review_sprite_sequence import mapped_sheet, metrics, world_part
from scripts.sprite_material_atlas import textured_leg


class SpriteSequenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "geometry"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        str(ROOT / "scripts/sprite_sequence_geometry.cc"), "-o", str(cls.binary)], check=True)
        cls.document, cls.trace, cls.landmarks = [json.loads(path.read_text()) for path in (DOCUMENT, TRACE, LANDMARKS)]
        cls.rows = geometry_rows(cls.document, cls.trace, cls.landmarks)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_geometry(self, rows):
        return subprocess.run([str(self.binary)], input=rows, capture_output=True, text=True)

    def test_every_frame_keeps_target_joints_and_shin_attachment(self):
        result = self.run_geometry(self.rows)
        self.assertEqual(result.returncode, 0, result.stderr)
        legs = json.loads(result.stdout)
        self.assertEqual(len(legs), 24)
        for leg in legs:
            frame = next(frame for frame in self.document["frames"] if frame["name"] == leg["name"])
            suffix = "l" if leg["side"] == "near" else "r"
            for key in ("knee", "ankle", "toe"):
                self.assertLess(math.dist(leg[key], frame["pose"][key + "_" + suffix]), 1e-10)
            shin = [leg["knee"][i]-leg["ankle"][i] for i in (0, 1)]
            shaft = [leg["cuff"][i]-leg["ankle"][i] for i in (0, 1)]
            self.assertAlmostEqual(shin[0]*shaft[1]-shin[1]*shaft[0], 0, places=8)
            self.assertGreater(sum(a*b for a, b in zip(shin, shaft)), 0)
            self.assertLess(math.hypot(*shaft), math.hypot(*shin))
            # Connected component at the ankle must contain both sole pins and knee.
            from PIL import ImageDraw
            alpha = leg_image(leg).getchannel("A")
            ImageDraw.floodfill(alpha, tuple(map(round, leg["ankle"])), 128, thresh=0)
            self.assertNotIn(255, alpha.get_flattened_data(), leg["name"] + leg["side"])

    def test_rotated_triangle_preserves_known_heel(self):
        row = "test near 20 0 20 10 20 30 20 40 0 0 -4 3 10 0 8 6\n"
        leg = json.loads(self.run_geometry(row).stdout)[0]
        self.assertEqual(leg["heel"], [17, 26])
        self.assertEqual(leg["ankle"], [20, 30])
        self.assertEqual(leg["toe"], [20, 40])

    def test_recovery_heel_is_above_toe_in_both_half_cycles(self):
        legs = json.loads(self.run_geometry(self.rows).stdout)
        for name, side in (("reference_04", "far"), ("reference_10", "near")):
            leg = next(leg for leg in legs if leg["name"] == name and leg["side"] == side)
            self.assertLess(leg["heel"][1], leg["toe"][1])
            self.assertLess(leg["ankle"][0], leg["knee"][0])
            self.assertEqual(leg["view"], "profile")

    def test_invalid_late_row_never_emits_partial_sequence(self):
        for invalid in ("bad", "test near 0 0 0 0 0 0 0 0 0 0 0 0 0 0 12 11", self.rows.splitlines()[0]):
            with self.subTest(invalid=invalid):
                result = self.run_geometry(self.rows + invalid + "\n")
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)

    def test_degenerate_foot_frame_rejected(self):
        for row in ("test near 0 0 10 10 20 20 30 20 0 0 4 0 10 0 12 11\n",
                    "test near 0 0 10 10 nan 20 30 20 0 0 4 3 10 0 12 11\n", ""):
            self.assertNotEqual(self.run_geometry(row).returncode, 0)

    def test_review_does_not_hide_translation_or_canvas_distortion(self):
        from PIL import Image, ImageDraw
        control = Image.new("RGB", (352, 352), "white")
        candidate = control.copy()
        ImageDraw.Draw(control).rectangle((30, 80, 49, 119), fill="brown")
        ImageDraw.Draw(candidate).rectangle((30, 60, 49, 99), fill="brown")
        score = metrics(control, candidate)
        self.assertEqual(score["centroid_delta_working_px"], [0, -10])
        self.assertEqual(score["area_ratio"], 1)
        self.assertAlmostEqual(score["iou"], 1/3)
        with self.assertRaisesRegex(ValueError, "aspect ratio"):
            mapped_sheet(Image.new("RGB", (100, 100)), (1056, 704))
        part = world_part(control)
        self.assertEqual(part.size, (256, 256))
        self.assertIsNotNone(part.getchannel("A").getbbox())

    def test_one_material_atlas_keeps_all_twenty_four_control_silhouettes(self):
        from PIL import Image
        atlas = {"cloth": Image.new("RGB", (20, 25), (80, 65, 40)),
                 "leather": Image.new("RGB", (24, 22), (140, 80, 45))}
        legs = json.loads(self.run_geometry(self.rows).stdout)
        for leg in legs:
            rendered = textured_leg(leg, atlas)
            self.assertEqual(rendered.getchannel("A").tobytes(), leg_image(leg).getchannel("A").tobytes())
            self.assertEqual(rendered.tobytes(), textured_leg(leg, atlas).tobytes())


if __name__ == "__main__":
    unittest.main()
