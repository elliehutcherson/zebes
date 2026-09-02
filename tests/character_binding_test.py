"""Tests for the active character-binding prototype.

Stable isolation and medial-axis behavior is covered by the C++
`profile_silhouette_test`; these tests cover only still-experimental semantic
binding and thin Python orchestration.
"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

EXPERIMENTS_ROOT = Path(__file__).parent.parent / "experiments"
if str(EXPERIMENTS_ROOT) not in sys.path:
    sys.path.insert(0, str(EXPERIMENTS_ROOT))

from character_binding import cli, profile_bind  # noqa: E402
from character_binding.png import read_rgba, write_rgba  # noqa: E402


class ProfileBindingTest(unittest.TestCase):
    @staticmethod
    def profile_mask() -> tuple[bytearray, int, int]:
        width = height = 128
        mask = bytearray(width * height)

        def ellipse(cx, cy, radius_x, radius_y):
            for y in range(max(0, cy - radius_y), min(height, cy + radius_y + 1)):
                for x in range(max(0, cx - radius_x), min(width, cx + radius_x + 1)):
                    if (
                        ((x - cx) / radius_x) ** 2
                        + ((y - cy) / radius_y) ** 2
                        <= 1.0
                    ):
                        mask[y * width + x] = 1

        def rectangle(left, top, right, bottom):
            for y in range(top, bottom):
                start = y * width + left
                mask[start : start + right - left] = b"\x01" * (right - left)

        ellipse(64, 27, 18, 19)
        ellipse(45, 15, 10, 11)
        ellipse(83, 15, 10, 11)
        rectangle(59, 43, 69, 52)
        rectangle(45, 50, 83, 91)
        rectangle(35, 57, 48, 82)
        rectangle(80, 57, 101, 79)
        rectangle(49, 88, 59, 117)
        rectangle(69, 88, 79, 117)
        rectangle(42, 113, 59, 120)
        rectangle(69, 113, 88, 120)
        return mask, width, height

    @staticmethod
    def profile_skeleton(width: int, height: int) -> bytearray:
        skeleton = bytearray(width * height)

        def line(start, end):
            x0, y0 = start
            x1, y1 = end
            delta_x = abs(x1 - x0)
            delta_y = -abs(y1 - y0)
            step_x = 1 if x0 < x1 else -1
            step_y = 1 if y0 < y1 else -1
            error = delta_x + delta_y
            while True:
                skeleton[y0 * width + x0] = 1
                if (x0, y0) == (x1, y1):
                    return
                doubled = 2 * error
                if doubled >= delta_y:
                    error += delta_y
                    x0 += step_x
                if doubled <= delta_x:
                    error += delta_x
                    y0 += step_y

        line((64, 27), (64, 90))
        line((64, 60), (40, 78))
        line((64, 60), (92, 70))
        line((64, 90), (50, 116))
        line((64, 90), (78, 116))
        return skeleton

    def binding(self) -> profile_bind.ProfileBinding:
        mask, width, height = self.profile_mask()
        return profile_bind.make_binding_from_topology(
            mask, self.profile_skeleton(width, height), width, height, 1
        )

    def test_medial_axis_binding_finds_ordered_profile_joints(self):
        binding = self.binding()
        joints = binding.joints

        self.assertLess(joints["head"][1], joints["neck"][1])
        self.assertLess(joints["neck"][1], joints["shoulder"][1])
        self.assertLess(joints["shoulder"][1], joints["hip"][1])
        self.assertLess(joints["hip"][1], joints["leg_split"][1])
        self.assertLess(joints["leg_split"][1], joints["foot_a"][1])
        self.assertIn("hand_a", joints)
        self.assertIn("hand_b", joints)

    def test_neutral_binding_retains_the_isolated_silhouette(self):
        binding = self.binding()
        neutral, _ = profile_bind.render_pose(binding, "neutral")
        intersection = sum(
            bool(reference) and bool(candidate)
            for reference, candidate in zip(binding.mask, neutral, strict=True)
        )
        union = sum(
            bool(reference) or bool(candidate)
            for reference, candidate in zip(binding.mask, neutral, strict=True)
        )

        self.assertGreater(intersection / union, 0.90)

    def test_airborne_pose_lifts_both_feet(self):
        binding = self.binding()
        _, airborne = profile_bind.render_pose(binding, "airborne")

        self.assertLess(airborne["foot_a"][1], binding.joints["foot_a"][1])
        self.assertLess(airborne["foot_b"][1], binding.joints["foot_b"][1])


    def test_color_warp_fills_every_approved_pose_pixel(self):
        mask, width, height = self.profile_mask()
        binding = self.binding()
        pixels = bytearray(width * height * 4)
        for index, covered in enumerate(mask):
            if covered:
                pixels[index * 4 : index * 4 + 4] = bytes(
                    (index % width, index // width, 90, 255)
                )
        reduced = profile_bind.downsample_subject_rgba(
            pixels, mask, width, height, binding
        )
        posed_mask, _ = profile_bind.render_pose(binding, "contact")
        posed_color = profile_bind.render_color_pose(binding, reduced, "contact")

        for index, covered in enumerate(posed_mask):
            self.assertEqual(bool(posed_color[index * 4 + 3]), bool(covered))

    def test_flat_cli_consumes_cpp_isolated_alpha_and_topology(self):
        mask, width, height = self.profile_mask()
        skeleton = self.profile_skeleton(width, height)
        pixels = bytearray(width * height * 4)
        topology_pixels = bytearray(b"\x00\x00\x00\xff" * width * height)
        for index, covered in enumerate(mask):
            if not covered:
                continue
            pixels[index * 4 : index * 4 + 4] = b"\x60\x90\x50\xff"
            topology_pixels[index * 4 : index * 4 + 4] = b"\xd0\xd0\xd0\xff"
        for index, covered in enumerate(skeleton):
            if covered:
                topology_pixels[index * 4 : index * 4 + 4] = b"\xff\x46\x46\xff"

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "isolated.png"
            topology = root / "skeleton.png"
            out = root / "binding"
            write_rgba(source, width, height, pixels)
            write_rgba(topology, width, height, topology_pixels)
            with contextlib.redirect_stdout(io.StringIO()):
                result = cli.main(
                    [
                        "bind-profile",
                        str(source),
                        str(topology),
                        "--out",
                        str(out),
                        "--skip-control-render",
                    ]
                )

            self.assertEqual(result, 0)
            self.assertTrue((out / "binding.json").is_file())
            decoded_width, decoded_height, _ = read_rgba(out / "source-color.png")
            self.assertEqual((decoded_width, decoded_height), (128, 128))


if __name__ == "__main__":
    unittest.main()
