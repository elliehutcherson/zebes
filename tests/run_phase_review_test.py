import json
import tempfile
import unittest
from pathlib import Path

from scripts.review_run_phases import PHASES, prepare


class RunPhaseReviewTest(unittest.TestCase):
    def test_retained_trace_has_mirrored_stance_and_two_flights(self):
        root = Path(__file__).resolve().parents[1]
        sheet = root / "experiments/character_binding/inputs/run-pose-reference-12.png"
        trace = root / "experiments/character_binding/inputs/run-pose-trace-v1.json"
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "review"
            prepare(sheet, trace, output)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["provider_calls"], 0)
            self.assertEqual([frame["support"] for frame in manifest["frames"]], ["near"] * 5 + ["flight"] + ["far"] * 5 + ["flight"])
            self.assertEqual([frame["phase"] for frame in manifest["frames"]], list(PHASES))
            frame10 = manifest["frames"][9]
            knee, ankle, toe = frame10["paths"]["near"][1:]
            self.assertLess(ankle[1], knee[1])
            self.assertLess(ankle[1], toe[1])
            self.assertFalse(frame10["explicit_heel_landmark"])


if __name__ == "__main__":
    unittest.main()
