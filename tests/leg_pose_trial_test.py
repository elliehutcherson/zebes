import copy
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from scripts.prepare_leg_pose_trial import graph, map_point, prepare


class LegPoseTrialTest(unittest.TestCase):
    def test_trial_preserves_reviewed_target_and_fixed_crop(self):
        source = Path(__file__).resolve().parents[1] / "experiments/character_binding/evidence/leg-ownership-trial-v1"
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "trial"
            prepare(source, output)
            self.assertEqual((source / "near/edit-input.png").read_bytes(), (output / "edit-input.png").read_bytes())
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["provider_calls"], 0)
            self.assertEqual(manifest["fixed_crop"], [88, 136, 184, 232])
            # No bounds fitting: the whole canvas maps back to the original crop.
            self.assertEqual(map_point([88, 136], manifest["fixed_crop"]), [0, 0])
            self.assertEqual(map_point([184, 232], manifest["fixed_crop"]), [1024, 1024])
            self.assertAlmostEqual(manifest["sole_angle_screen_degrees"], 115)
            for name in ("edit-input.png", "landmarks.png", "canny.png"):
                with Image.open(output / name) as image:
                    self.assertEqual(image.size, (1024, 1024))

    def test_local_pair_changes_only_denoise_and_output_name(self):
        low, high = graph(0.65), copy.deepcopy(graph(0.85))
        self.assertEqual(high["14"]["inputs"]["denoise"], 0.85)
        high["14"]["inputs"]["denoise"] = 0.65
        high["16"]["inputs"]["filename_prefix"] = low["16"]["inputs"]["filename_prefix"]
        self.assertEqual(low, high)

    def test_canny_conditions_both_sampler_inputs_and_keeps_actual_prefill(self):
        nodes = graph(0.65)
        self.assertEqual(nodes["11"]["inputs"]["image"], "zebes-leg-pose-v1/canny.png")
        self.assertEqual(nodes["14"]["inputs"]["positive"], ["13", 0])
        self.assertEqual(nodes["14"]["inputs"]["negative"], ["13", 1])
        self.assertEqual(nodes["8"]["class_type"], "VAEEncode")
        self.assertEqual(nodes["8"]["inputs"]["pixels"], ["7", 0])
        image_inputs = {node["inputs"]["image"] for node in nodes.values() if node["class_type"] == "LoadImage"}
        self.assertEqual(image_inputs, {"zebes-leg-pose-v1/identity.png", "zebes-leg-pose-v1/edit-input.png", "zebes-leg-pose-v1/canny.png"})


if __name__ == "__main__":
    unittest.main()
