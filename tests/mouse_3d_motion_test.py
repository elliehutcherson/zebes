"""Checks for the isolated Blender asset's animation control geometry."""

import math
import unittest

from experiments.mouse_3d.motion import SHIN, STANCE, THIGH, foot_path, pose_at, two_bone_joint


class Mouse3dMotionTest(unittest.TestCase):
    def test_fixed_length_connected_chains_throughout_cycle(self):
        for sample in range(1000):
            bones = pose_at(sample / 1000)["bones"]
            for side in ("near", "far"):
                thigh, shin, foot = [bones[f"{part}.{side}"] for part in ("thigh", "shin", "foot")]
                self.assertEqual(thigh[1], shin[0])
                self.assertEqual(shin[1], foot[0])
                self.assertAlmostEqual(math.dist(*thigh), THIGH)
                self.assertAlmostEqual(math.dist(*shin), SHIN)
                self.assertAlmostEqual(math.dist(*foot), 0.5)
                for part, length in (("upper_arm", 0.52), ("forearm", 0.49)):
                    self.assertAlmostEqual(math.dist(*bones[f"{part}.{side}"]), length)

    def test_support_sole_and_airborne_clearance(self):
        for sample in range(1000):
            x, z, angle, planted, clearance = foot_path(sample / 1000)
            lowest = min(z + xp * math.sin(angle) + zp * math.cos(angle)
                         for xp in (-0.23, 0.63) for zp in (-0.25, 0.15))
            self.assertAlmostEqual(lowest, clearance)
            self.assertGreaterEqual(lowest, -1e-12)
            self.assertEqual(planted, sample / 1000 < STANCE)
            if planted:
                self.assertEqual(z, 0.25)
                self.assertEqual(angle, 0)

    def test_planted_foot_has_constant_ground_speed(self):
        positions = [foot_path(i / 100)[0] for i in range(40)]
        for left, right in zip(positions, positions[1:]):
            self.assertAlmostEqual(right - left, -0.034)

    def test_cycle_is_closed_and_continuous(self):
        self.assertEqual(pose_at(0), pose_at(1))
        for boundary in (0, 0.4, 0.5, 0.9, 1):
            before = pose_at(boundary - 1e-7)["bones"]
            after = pose_at(boundary + 1e-7)["bones"]
            for name in before:
                for a, b in zip(before[name], after[name]):
                    self.assertLess(math.dist(a, b), 1e-4, name)

    def test_rejects_invalid_or_unreachable_targets(self):
        for endpoint in ((0, 0), (0, 3), (float("nan"), 1)):
            with self.assertRaises(ValueError):
                two_bone_joint((0, 0), endpoint, 0.8, 0.7)
        with self.assertRaises(ValueError):
            pose_at(float("inf"))
        with self.assertRaises(ValueError):
            two_bone_joint((0, 0), (0, 1), 0.8, 0.7, bend=0)


if __name__ == "__main__":
    unittest.main()
