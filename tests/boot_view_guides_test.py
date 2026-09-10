import math
import json
import unittest
from pathlib import Path

from scripts.prepare_boot_view_guides import render_boot


class BootViewGuidesTest(unittest.TestCase):
    def setUp(self):
        self.config = {"camera_yaw_degrees": 35, "camera_elevation_degrees": 8, "canvas": [128, 128]}

    def test_tilt_toward_camera_reveals_sole_but_flat_support_hides_it(self):
        _, _, forward = render_boot([45, 75], [80, 55], self.config)
        _, _, flat = render_boot([45, 75], [80, 75], self.config)
        _, _, backward = render_boot([45, 55], [80, 75], self.config)
        self.assertGreater(forward["visible_sole_pixels"], 0)
        self.assertEqual(flat["visible_sole_pixels"], 0)
        self.assertEqual(backward["visible_sole_pixels"], 0)

    def test_projection_keeps_both_sole_pins_in_every_quadrant(self):
        heel = [64, 64]
        for angle in range(-180, 180, 15):
            radians = math.radians(angle)
            toe = [64 + 25 * math.cos(radians), 64 + 25 * math.sin(radians)]
            material, _, annotations = render_boot(heel, toe, self.config)
            self.assertLess(annotations["sole_pin_error"], 1e-6)
            self.assertIsNotNone(material.getbbox())

    def test_first_pose_camera_exposes_front_sole_but_not_back_sole(self):
        path = Path(__file__).resolve().parents[1] / "experiments/character_binding/inputs/boot-view-guide-v1.json"
        config = json.loads(path.read_text())
        counts = {}
        for side in ("near", "far"):
            angle = math.radians(config["sole_directions_degrees"][side][0])
            toe = [128 + 24 * math.cos(angle), 128 + 24 * math.sin(angle)]
            _, _, annotations = render_boot([128, 128], toe, config)
            counts[side] = annotations["visible_sole_pixels"]
        self.assertGreater(counts["near"], 0)
        self.assertEqual(counts["far"], 0)

    def test_stouter_boot_adds_volume_without_moving_the_reviewed_pins(self):
        path = Path(__file__).resolve().parents[1] / "experiments/character_binding/inputs/boot-view-guide-v2.json"
        stout = json.loads(path.read_text())
        original, _, before = render_boot([45, 75], [80, 55], self.config)
        larger, _, after = render_boot([45, 75], [80, 55], {**stout, "canvas": [128, 128]})
        self.assertGreater(sum(larger.getchannel("A").getdata()), sum(original.getchannel("A").getdata()))
        for key in ("heel", "toe", "cuff"):
            self.assertEqual(before[key], after[key])

    def test_shaft_follows_shin_without_moving_the_sole_in_every_pose(self):
        root = Path(__file__).resolve().parents[1] / "experiments/character_binding"
        previous = json.loads((root / "evidence/boot-view-guides-v2/manifest.json").read_text())
        document = json.loads((root / "puppet_documents/mouse_run_reference_v2.json").read_text())
        config = json.loads((root / "inputs/boot-view-guide-v3.json").read_text())
        for frame, guide in zip(document["frames"], previous["frames"], strict=True):
            for side, suffix in (("near", "l"), ("far", "r")):
                pins = guide["annotations"][side]
                knee = frame["pose"]["knee_" + suffix]
                _, _, aligned = render_boot(pins["heel"], pins["toe"], config, knee)
                self.assertLess(aligned["calf_shaft_angle_degrees"], 1e-4)
                self.assertEqual(aligned["heel"], pins["heel"])
                self.assertEqual(aligned["toe"], pins["toe"])
                self.assertLess(math.dist(aligned["cuff"], aligned["ankle"]), math.dist(knee, aligned["ankle"]))

    def test_shin_alignment_requires_an_explicit_knee(self):
        with self.assertRaisesRegex(ValueError, "target knee"):
            render_boot([45, 75], [80, 55], {**self.config, "shaft_alignment": "shin"})


if __name__ == "__main__":
    unittest.main()
