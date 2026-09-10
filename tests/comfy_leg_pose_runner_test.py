import json
import shutil
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from PIL import Image

from scripts.run_comfy_leg_pose_trial import reviewed_inputs, run


class ComfyLegPoseRunnerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        source = Path(__file__).resolve().parents[1] / "experiments/character_binding/evidence/leg-pose-preservation-v1"
        self.trial = Path(self.temp.name) / "trial"
        shutil.copytree(source, self.trial, ignore=shutil.ignore_patterns("results"))
        self.args = SimpleNamespace(trial=self.trial, name="canny-0.65", server="http://127.0.0.1:8189", timeout=1)

    def receipt(self, status, **extra):
        _, _, _, digest, _ = reviewed_inputs(self.trial, self.args.name)
        output = self.trial / "results" / self.args.name
        output.mkdir(parents=True)
        (output / "receipt.json").write_text(json.dumps({"status": status, "reviewed_graph_sha256": digest, **extra}))
        return output

    def test_changed_edge_input_is_rejected_before_network_calls(self):
        with (self.trial / "canny.png").open("ab") as target:
            target.write(b"changed")
        with patch("scripts.run_comfy_leg_pose_trial.request") as network:
            with self.assertRaisesRegex(ValueError, "input changed after review"):
                run(self.args)
        network.assert_not_called()

    def test_ambiguous_submission_is_not_retried(self):
        self.receipt("submitting")
        with patch("scripts.run_comfy_leg_pose_trial.request") as network:
            with self.assertRaisesRegex(ValueError, "reconcile"):
                run(self.args)
        network.assert_not_called()

    def test_resume_downloads_existing_output_without_post_or_bounds_fitting(self):
        output = self.receipt("running", prompt_id="existing-job", submitted_at_epoch=0)
        raw = (self.trial / "edit-input.png").read_bytes()
        history = {"existing-job": {"status": {"status_str": "success"}, "outputs": {"16": {"images": [
            {"filename": "finished.png", "subfolder": "trial", "type": "output"}
        ]}}}}
        with patch("scripts.run_comfy_leg_pose_trial.request", side_effect=[history, raw]) as network:
            run(self.args)
        self.assertEqual(network.call_count, 2)
        self.assertEqual(network.call_args_list[0].args[1], "/history/existing-job")
        self.assertTrue(network.call_args_list[1].args[1].startswith("/view?"))
        self.assertEqual((output / "raw-output.png").read_bytes(), raw)
        with Image.open(output / "fixed-crop-rgb.png") as mapped, Image.open(self.trial / "edit-input.png") as source:
            expected = source.convert("RGB").resize((96, 96), Image.Resampling.NEAREST)
            self.assertEqual(mapped.tobytes(), expected.tobytes())
        manifest = json.loads((self.trial / "manifest.json").read_text())
        self.assertEqual(manifest["provider_calls"], 1)


if __name__ == "__main__":
    unittest.main()
