import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from PIL import Image, ImageChops, ImageDraw

from scripts.prepare_gap_workflows import graph
from scripts.prepare_run_gap_trial import PROTECTED, make_inputs, prepare, rigid_point, weighted_blur


class RunGapTrialTest(unittest.TestCase):
    def test_cuff_follows_boot_rotation_not_ankle_position(self):
        rest = {"ankle_l": [10, 10], "toe_l": [20, 10]}
        pose = {"ankle_l": [30, 30], "toe_l": [30, 40]}
        self.assertEqual(rigid_point([10, 5], rest, pose, "l"), [35, 30])
        with self.assertRaisesRegex(ValueError, "unscaled"):
            rigid_point([10, 5], rest, {"ankle_l": [30, 30], "toe_l": [30, 50]}, "l")

    def test_prefills_change_gap_but_preserve_head_and_boot_sole(self):
        source = Image.new("RGBA", (64, 64))
        draw = ImageDraw.Draw(source)
        draw.rectangle((20, 2, 40, 16), fill=(70, 100, 30, 255))
        draw.rectangle((20, 48, 43, 58), fill=(120, 60, 30, 255))
        parts = {name: Image.new("RGBA", source.size) for name in (*PROTECTED, "near_boot", "far_boot")}
        ImageDraw.Draw(parts["head"]).rectangle((20, 2, 40, 16), fill="red")
        ImageDraw.Draw(parts["near_boot"]).rectangle((20, 48, 43, 58), fill="red")
        pose = {"hip_c": [30, 18], "knee_l": [32, 35], "knee_r": [32, 35],
                "ankle_l": [30, 52], "ankle_r": [30, 52], "toe_l": [40, 52], "toe_r": [40, 52]}
        config = {"canvas": [64, 64], "seam_margin": 2, "blur_radius": 4, "sides": {}}
        for side, suffix in (("near", "l"), ("far", "r")):
            config["sides"][side] = {"joint_suffix": suffix, "cuff": [[26, 48], [35, 48]],
                                     "thigh_width": 8, "calf_width": 7, "color": [50, 40, 30], "outline": [20, 20, 20]}
        variants, mask, missing, protected, _ = make_inputs(source, parts, pose, pose, config)
        self.assertEqual(mask.getpixel((32, 43)), 255)
        self.assertEqual(mask.getpixel((32, 56)), 0)
        self.assertEqual(mask.getpixel((25, 10)), 0)
        self.assertTrue(missing.getbbox())
        self.assertIsNone(ImageChops.darker(mask, protected).getbbox())
        self.assertNotEqual(variants["shaped"].getpixel((32, 43)), variants["blank"].getpixel((32, 43)))
        for variant in variants.values():
            self.assertEqual(variant.getpixel((32, 56)), (120, 60, 30))
            self.assertEqual(variant.getpixel((25, 10)), (70, 100, 30))

    def test_blur_does_not_mix_transparent_white_into_color(self):
        image = Image.new("RGBA", (11, 11), (255, 255, 255, 0))
        ImageDraw.Draw(image).rectangle((3, 3, 7, 7), fill=(80, 40, 20, 255))
        color = weighted_blur(image, 2, (80, 40, 20)).getpixel((5, 2))
        self.assertLess(max(abs(a - b) for a, b in zip(color, (80, 40, 20))), 10)

    def test_reviewed_directory_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name)
            (output / "reviewed.txt").write_text("reviewed")
            with self.assertRaisesRegex(ValueError, "new revision"):
                prepare(SimpleNamespace(output=output))

    def test_graph_preserves_prefill_and_uses_color_mask_channel(self):
        workflow = graph("reference_10", "shaped", 0.35, 17)
        self.assertEqual(workflow["10"]["class_type"], "VAEEncode")
        self.assertEqual(workflow["10"]["inputs"]["pixels"], ["7", 0])
        self.assertEqual(workflow["9"]["inputs"], {"image": ["8", 0], "channel": "red"})
        self.assertEqual(workflow["11"]["inputs"]["mask"], ["9", 0])
        self.assertEqual(workflow["14"]["inputs"]["latent_image"], ["11", 0])
        self.assertEqual(workflow["18"]["inputs"]["destination"], ["17", 0])
        self.assertTrue(workflow["17"]["inputs"]["image"].endswith("blank-1024.png"))
        self.assertTrue(workflow["7"]["inputs"]["image"].endswith("shaped-1024.png"))


if __name__ == "__main__":
    unittest.main()
