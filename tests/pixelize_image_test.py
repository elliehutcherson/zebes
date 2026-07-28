import importlib.util
import sys
import unittest
from pathlib import Path

from PIL import Image


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "pixelize_image.py"
SPEC = importlib.util.spec_from_file_location("pixelize_image", SCRIPT_PATH)
pixelize_image = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = pixelize_image
SPEC.loader.exec_module(pixelize_image)


class PixelizeImageTest(unittest.TestCase):
    def test_center_crop_uses_largest_exact_aspect_ratio(self):
        source = Image.new("RGB", (1672, 941))

        cropped = pixelize_image.center_crop_to_aspect(source, 320, 180)

        self.assertEqual(cropped.size, (1664, 936))

    def test_output_has_exact_size_and_nearest_neighbor_blocks(self):
        source = Image.new("RGB", (2, 1))
        source.putpixel((0, 0), (255, 0, 0))
        source.putpixel((1, 0), (0, 0, 255))

        output = pixelize_image.pixelize(source, 2, 1, 3)

        self.assertEqual(output.size, (6, 3))
        self.assertEqual(output.getpixel((2, 1)), (255, 0, 0))
        self.assertEqual(output.getpixel((3, 1)), (0, 0, 255))

    def test_invalid_geometry_fails_fast(self):
        with self.assertRaisesRegex(ValueError, "positive"):
            pixelize_image.pixelize(Image.new("RGB", (10, 10)), 0, 10, 1)


if __name__ == "__main__":
    unittest.main()
