"""Focused checks for transferring the supplied pose sheet to a bound puppet."""

import json
import math
import unittest
from pathlib import Path

from scripts.retarget_run_reference import angle, transfer


ROOT = Path(__file__).resolve().parents[1]


class RunReferenceTest(unittest.TestCase):
    def setUp(self):
        base = ROOT / "experiments/character_binding"
        self.rest = json.loads((base / "puppet_documents/mouse_run_reference_v2.json").read_text())["rest_pose"]
        self.frames = json.loads((base / "inputs/run-pose-trace-v1.json").read_text())["frames"]

    def test_all_twelve_poses_keep_traced_directions_and_one_shared_leg_scale(self):
        self.assertEqual(len(self.frames), 12)
        lengths = []
        for frame in self.frames:
            posed = transfer(self.rest, frame["pose"], self.frames[0]["pose"])
            self.assertEqual(set(posed), set(self.rest))
            for side in ["l", "r"]:
                for start, end in [("shoulder_" + side, "elbow_" + side),
                                   ("elbow_" + side, "wrist_" + side),
                                   ("hip_c", "knee_" + side),
                                   ("knee_" + side, "ankle_" + side),
                                   ("ankle_" + side, "toe_" + side)]:
                    difference = angle(posed, start, end) - angle(frame["pose"], start, end)
                    self.assertAlmostEqual(math.atan2(math.sin(difference), math.cos(difference)), 0)
                lengths.append(math.dist(posed["hip_c"], posed["knee_" + side]) /
                               math.dist(frame["pose"]["hip_c"], frame["pose"]["knee_" + side]))
        self.assertAlmostEqual(min(lengths), max(lengths))

    def test_pose_ten_retains_the_folded_leg_behind_the_supporting_leg(self):
        trace = self.frames[9]["pose"]
        posed = transfer(self.rest, trace, self.frames[0]["pose"])
        self.assertGreater(posed["knee_l"][0], posed["hip_c"][0])
        self.assertLess(posed["ankle_l"][0], posed["knee_l"][0])
        self.assertLess(posed["ankle_l"][1], posed["knee_l"][1])
        self.assertGreater(posed["ankle_r"][1], posed["knee_r"][1])

    def test_collapsed_tracing_fails_instead_of_inventing_a_direction(self):
        trace = dict(self.frames[0]["pose"])
        trace["elbow_l"] = trace["shoulder_l"]
        with self.assertRaisesRegex(ValueError, "collapsed bone"):
            transfer(self.rest, trace, self.frames[0]["pose"])


if __name__ == "__main__":
    unittest.main()
