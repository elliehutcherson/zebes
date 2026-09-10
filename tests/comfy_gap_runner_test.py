import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

from PIL import Image, ImageDraw

from scripts.run_comfy_gap_trial import asset_path, run, sha, upload, verify_composite


def png(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return buffer.getvalue()


class ComfyGapRunnerTest(unittest.TestCase):
    def test_upload_verification_maps_name_to_filename(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.png"
            path.write_bytes(b"exact-upload-bytes")
            response = {"name": "input (1).png", "subfolder": "trial/frame", "type": "input"}
            with patch("scripts.run_comfy_gap_trial.request", side_effect=[response, path.read_bytes()]) as http:
                remote = upload("http://127.0.0.1:8189", path, Path("frame/input.png"))
            self.assertEqual(remote, "trial/frame/input (1).png")
            self.assertEqual(parse_qs(urlparse(http.call_args_list[1].args[1]).query),
                             {"filename": ["input (1).png"], "subfolder": ["trial/frame"], "type": ["input"]})

    def test_uncertain_submission_is_not_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            graph = root / "graph.json"
            graph.write_text("{}")
            output = root / "output"
            output.mkdir()
            (output / "receipt.json").write_text(json.dumps({"status": "submitting", "reviewed_graph_sha256": sha(graph.read_bytes())}))
            with patch("scripts.run_comfy_gap_trial.validate_reviewed_inputs", return_value={}), patch("scripts.run_comfy_gap_trial.request") as http:
                with self.assertRaisesRegex(ValueError, "reconcile"):
                    run(SimpleNamespace(server="http://127.0.0.1:8189", graph=graph, assets=root, output=output))
                http.assert_not_called()

    def test_asset_traversal_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "invalid asset path"):
            asset_path(Path("/tmp/assets"), "zebes-run-gap-v1/../other.png")

    def test_composite_keeps_original_outside_mask(self):
        blank = Image.new("RGB", (1024, 1024), (90, 40, 20))
        raw = Image.new("RGB", blank.size, (40, 80, 120))
        mask = Image.new("L", blank.size)
        ImageDraw.Draw(mask).rectangle((30, 30, 50, 50), fill=255)
        expected = Image.composite(raw, blank, mask)
        result, error = verify_composite(png(blank), png(raw), png(mask), png(expected))
        self.assertEqual(error, 0)
        self.assertEqual(result.getpixel((40, 40)), (40, 80, 120))
        self.assertEqual(result.getpixel((100, 100)), (90, 40, 20))


if __name__ == "__main__":
    unittest.main()
