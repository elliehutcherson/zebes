"""Anatomy and registration checks at the standalone C++ process boundary."""

import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image

from scripts.prepare_sprite_sequence import ROOT
from scripts.render_sprite_painted import extract, row, sole_observations


class SpritePaintedTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.geometry = Path(cls.temp.name) / "geometry"
        cls.registration = Path(cls.temp.name) / "registration"
        for source, target in (("sprite_sequence_geometry.cc", cls.geometry),
                               ("sprite_painted_registration.cc", cls.registration)):
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
                            str(ROOT / "scripts" / source), "-o", str(target)], check=True)
        cls.inputs = ROOT / "experiments/sprite_sequence/evidence/painted-master-v1"
        cls.rows = (cls.inputs / "geometry-input.txt").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_all_twenty_four_boots_have_rear_quarter_ankles_and_original_pose_pins(self):
        current = json.loads(subprocess.run([str(self.geometry), "--painted"], input=self.rows,
                                           text=True, capture_output=True, check=True).stdout)
        prior = json.loads((self.inputs / "geometry.json").read_text())
        for frame, original in zip(current["frames"], prior["frames"], strict=True):
            self.assertEqual(frame["translation"], original["translation"])
            for leg, control in zip(frame["legs"], original["legs"], strict=True):
                a = leg["anchors"]
                for key in ("hip", "knee", "ankle", "toe"):
                    self.assertEqual(a[key], control["anchors"][key])
                sole = [a["toe"][i]-a["heel"][i] for i in (0, 1)]
                fraction = sum((a["ankle"][i]-a["heel"][i])*sole[i] for i in (0, 1))/sum(v*v for v in sole)
                self.assertAlmostEqual(fraction, 0.22, places=10)
                for point in leg["sole_samples"]:
                    cross = (point[0]-a["heel"][0])*sole[1]-(point[1]-a["heel"][1])*sole[0]
                    self.assertAlmostEqual(cross, 0, places=8)
                if leg["contact_mode"] != "none":
                    self.assertAlmostEqual(a["contact"][1]+frame["translation"][1], 214, places=9)
        self.assertGreater(current["frames"][6]["legs"][0]["anchors"]["heel"][0], 165)

    def test_legacy_atlas_is_still_exactly_reproducible(self):
        old = self.inputs.parent / "boot-atlas-v2"
        result = subprocess.run([str(self.geometry), "--atlas"], input=(old / "geometry-input.txt").read_text(),
                                text=True, capture_output=True, check=True)
        self.assertEqual(json.loads(result.stdout), json.loads((old / "geometry.json").read_text()))

    def test_flat_contact_cannot_collapse_a_sole_into_nan_output(self):
        # The source heel and toe share x; flattening y collapses their sole.
        invalid = "frame far 0 -40 0 -20 0 0 10 10 0 0 10 -10 10 10 12 13 profile flat 0\n"
        result = subprocess.run([str(self.geometry), "--painted"], input=invalid,
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("distinct sole", result.stderr)

    def test_similarity_preserves_painted_shape(self):
        data = row("head", [[100, 100], [200, 100]], [[30, 40], [30, 90]])+"\n"
        result = subprocess.run([str(self.registration)], input=data, text=True, capture_output=True, check=True)
        mapping = json.loads(result.stdout)[0]
        self.assertLess(mapping["landmark_residual_raw_px"], 1e-8)
        for box, q in mapping["mesh"][::4096]:
            x, y = box[:2]
            self.assertAlmostEqual(q[0], 100+(y-40)*2)
            self.assertAlmostEqual(q[1], 100-(x-30)*2)

    def test_semantic_mesh_holds_all_pins_through_nonlinear_correction(self):
        target = [[50, 30], [80, 60], [60, 90], [45, 120], [35, 140], [90, 140]]
        source = [[100, 50], [150, 110], [130, 185], [105, 230], [80, 280], [170, 270]]
        result = subprocess.run([str(self.registration)], input=row("leg", source, target)+"\n",
                                text=True, capture_output=True, check=True)
        self.assertLess(json.loads(result.stdout)[0]["landmark_residual_raw_px"], 1e-6)

    def test_width_flow_preserves_pins_expands_cross_sections_and_does_not_fold(self):
        pins = [[128, 64], [128, 128], [128, 192], [128, 240], [120, 264], [168, 264]]
        result = subprocess.run([str(self.registration), "--proportions"],
                                input=row("near_leg", pins, pins)+"\n",
                                text=True, capture_output=True, check=True)
        mapping = json.loads(result.stdout)[0]
        self.assertLess(mapping["landmark_residual_raw_px"], 1e-8)
        samples = {tuple(box[:2]): quad[:2] for box, quad in mapping["mesh"]}
        for pin in pins:
            self.assertLess(math.dist(samples[tuple(pin)], pin), 1e-8)
        # Inverse sampling contracts a transverse displacement, so native
        # painting occupies a wider target cross section without another bake.
        self.assertLess(samples[(140, 160)][0], 138)
        self.assertGreater(samples[(116, 160)][0], 118)
        for _, q in mapping["mesh"]:
            determinant = (q[6]-q[0])*(q[3]-q[1])-(q[7]-q[1])*(q[2]-q[0])
            self.assertGreater(determinant, 0)

    def test_width_flow_requires_complete_semantic_leg_chain(self):
        result = subprocess.run([str(self.registration), "--proportions"],
                                input=row("far_leg", [[0, 0], [10, 10]], [[20, 20], [40, 40]])+"\n",
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_proportions_leave_head_similarity_unchanged(self):
        data = row("head", [[100, 100], [200, 100]], [[30, 40], [30, 90]])+"\n"
        before = subprocess.run([str(self.registration)], input=data, text=True, capture_output=True, check=True)
        after = subprocess.run([str(self.registration), "--proportions"], input=data, text=True, capture_output=True, check=True)
        self.assertEqual(before.stdout, after.stdout)

    def test_invalid_inputs_fail_without_partial_json(self):
        good = row("valid", [[0, 0], [10, 10]], [[20, 20], [40, 40]])+"\n"
        cases = ["", good+good, good+"bad 2 0 0 0 0 1 1 0 0\n",
                 good+"bad 2 0 0 0 0 nan 1 2 2\n", "bad 3 0 0 0 0 1 1 2 2 3 3 4 4\n"]
        for data in cases:
            with self.subTest(data=data):
                result = subprocess.run([str(self.registration)], input=data, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)

    def test_background_extraction_does_not_modify_opaque_paint(self):
        image = Image.new("RGB", (2, 1), "white")
        image.putpixel((1, 0), (90, 50, 30))
        result = extract(image)
        self.assertEqual(result.getpixel((0, 0))[3], 0)
        self.assertEqual(result.getpixel((1, 0)), (90, 50, 30, 255))
        self.assertEqual(image.getpixel((0, 0)), (255, 255, 255))

    def test_outsole_measurement_fails_when_an_observation_misses_artwork(self):
        with self.assertRaisesRegex(ValueError, "no adjacent painted contour"):
            sole_observations(Image.new("RGBA", (64, 64)), [[0, 0], [0, 10], [0, 20], [10, 20], [5, 40], [50, 40]])


if __name__ == "__main__":
    unittest.main()
