import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from PIL import Image, ImageDraw

from scripts.leg_ownership_trial import compose, receive


class LegOwnershipTrialTest(unittest.TestCase):
    def test_isolated_part_uses_fixed_crop_inverse_not_bounds_fitting(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "near").mkdir()
            (root / "manifest.json").write_text(json.dumps({"provider_calls": 0, "entries": [{"name": "near", "status": "prepared", "crop": [88, 136, 184, 232]}]}))
            guide = Image.new("RGBA", (96, 96))
            ImageDraw.Draw(guide).rectangle((10, 10, 20, 20), fill="red")
            guide.save(root / "near/source-part.png")
            raw = Image.new("RGB", (192, 192), "white")
            ImageDraw.Draw(raw).rectangle((20, 20, 40, 40), fill="red")
            raw.save(root / "raw.png")
            receive(SimpleNamespace(output=root, name="near", raw=root / "raw.png"))
            with Image.open(root / "near/registered-rgba.png") as result:
                self.assertEqual(result.size, (256, 256))
                self.assertEqual(result.getpixel((98, 146)), (255, 0, 0, 255))
                self.assertEqual(result.getpixel((87, 146))[3], 0)
            self.assertEqual(json.loads((root / "manifest.json").read_text())["provider_calls"], 1)

    def test_combined_restores_original_rgba_outside_the_mask(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "combined").mkdir()
            (root / "manifest.json").write_text(json.dumps({"provider_calls": 0, "entries": [{"name": "combined", "status": "prepared", "crop": None}]}))
            source = Image.new("RGBA", (256, 256), (12, 23, 34, 0))
            source.putpixel((50, 50), (90, 120, 60, 128))
            source.save(root / "source-rgba.png")
            mask = Image.new("L", source.size)
            ImageDraw.Draw(mask).rectangle((100, 100, 120, 120), fill=255)
            mask.save(root / "combined/mask.png")
            Image.new("RGB", source.size, "red").save(root / "raw.png")
            receive(SimpleNamespace(output=root, name="combined", raw=root / "raw.png"))
            with Image.open(root / "combined/registered-rgba.png") as result:
                self.assertEqual(result.getpixel((50, 50)), source.getpixel((50, 50)))
                self.assertEqual(result.getpixel((0, 0)), (12, 23, 34, 0))
                self.assertEqual(result.getpixel((110, 110)), (255, 0, 0, 255))

    def test_near_leg_overlaps_far_leg_and_coat_overlaps_both(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for part in ("near", "far", "combined"):
                (root / part).mkdir()
            layers = {part: Image.new("RGBA", (256, 256)) for part in ("near", "far", "combined", "body", "head", "tail", "far_arm", "near_arm")}
            ImageDraw.Draw(layers["far"]).rectangle((140, 145, 155, 215), fill="blue")
            ImageDraw.Draw(layers["near"]).rectangle((125, 165, 165, 185), fill="red")
            ImageDraw.Draw(layers["body"]).rectangle((130, 140, 160, 160), fill="green")
            for part, image in layers.items():
                image.save(root / part / "registered-rgba.png" if part in ("near", "far", "combined") else root / (part + ".png"))
            manifest = {"entries": [{"name": name, "status": "complete"} for name in ("combined", "near", "far")],
                        "composite_order": ["far_arm", "far", "near", "tail", "body", "near_arm", "head"]}
            (root / "manifest.json").write_text(json.dumps(manifest))
            compose(SimpleNamespace(output=root))
            with Image.open(root / "separate-composite.png") as result:
                self.assertEqual(result.getpixel((148, 180)), (255, 0, 0, 255))
                self.assertEqual(result.getpixel((148, 200)), (0, 0, 255, 255))
                self.assertEqual(result.getpixel((148, 150)), (0, 128, 0, 255))


if __name__ == "__main__":
    unittest.main()
