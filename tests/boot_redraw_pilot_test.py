import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from PIL import Image

from scripts.boot_redraw_pilot import review


class BootRedrawPilotTest(unittest.TestCase):
    def test_feathered_edge_is_editable_while_black_pixels_remain_exact(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            frame = root / "reference_01"
            frame.mkdir()
            (root / "manifest.json").write_text(json.dumps({"provider_calls": 0, "frames": [{"name": frame.name, "status": "prepared"}]}))
            source = Image.new("RGBA", (256, 256), (255, 0, 0, 255))
            source.save(frame / "original.png")
            source.save(frame / "source-rgba.png")
            mask = Image.new("L", source.size)
            mask.putpixel((10, 10), 128)
            mask.putpixel((11, 10), 255)
            mask.save(frame / "repair-mask.png")
            raw = root / "provider.png"
            Image.new("RGBA", source.size, (0, 0, 255, 255)).save(raw)
            review(SimpleNamespace(output=root, frame=frame.name, raw=raw))
            with Image.open(frame / "composite.png") as result:
                self.assertEqual(result.getpixel((0, 0)), (255, 0, 0, 255))
                self.assertEqual(result.getpixel((10, 10)), (127, 0, 128, 255))
                self.assertEqual(result.getpixel((11, 10)), (0, 0, 255, 255))
            manifest = json.loads((root / "manifest.json").read_text())
            self.assertEqual(manifest["provider_calls"], 1)
            self.assertEqual(manifest["frames"][0]["outside_mask_changed_pixels"], 0)
            with self.assertRaisesRegex(ValueError, "already recorded"):
                review(SimpleNamespace(output=root, frame=frame.name, raw=raw))


if __name__ == "__main__":
    unittest.main()
