import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from PIL import Image

from scripts.pose_cleanup_pilot import extract_neutral_matte, extract_white_matte, prepare


class PoseCleanupPilotTest(unittest.TestCase):
    def test_observed_checkerboard_is_removed_without_recoloring_character_pixels(self):
        pixels = [(180, 181, 179, 255), (109, 109, 109, 255),
                  (40, 40, 40, 255), (40, 64, 32, 255),
                  (160, 89, 40, 127), (230, 180, 150, 255)]
        image = Image.new("RGBA", (len(pixels), 1))
        image.putdata(pixels)
        result = list(extract_neutral_matte(image, 8).getdata())
        self.assertEqual([pixel[3] for pixel in result], [0, 0, 255, 255, 127, 255])
        self.assertEqual([pixel[:3] for pixel in result], [pixel[:3] for pixel in pixels])

    def test_submitted_inputs_cannot_be_reprepared(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            manifest = output / "manifest.json"
            original = json.dumps({"provider_calls": 1})
            manifest.write_text(original)
            with self.assertRaisesRegex(ValueError, "after provider submission"):
                prepare(SimpleNamespace(output=output))
            self.assertEqual(manifest.read_text(), original)

    def test_white_matte_keeps_enclosed_highlights_and_removes_existing_background_holes(self):
        image = Image.new("RGBA", (7, 7), "white")
        source = Image.new("RGBA", (7, 7))
        for y in range(1, 6):
            for x in range(1, 6):
                image.putpixel((x, y), (40, 70, 30, 255))
                source.putpixel((x, y), (40, 70, 30, 255))
        image.putpixel((2, 3), (255, 255, 255, 255))
        image.putpixel((4, 3), (255, 255, 255, 255))
        source.putpixel((4, 3), (0, 0, 0, 0))
        result = extract_white_matte(image, source)
        self.assertEqual(result.getpixel((0, 0))[3], 0)
        self.assertEqual(result.getpixel((2, 3))[3], 255)
        self.assertEqual(result.getpixel((4, 3))[3], 0)
        self.assertEqual(result.getpixel((3, 3)), (40, 70, 30, 255))


if __name__ == "__main__":
    unittest.main()
