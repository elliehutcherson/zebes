import copy
import unittest

from scripts.prepare_boot_surface_trial import graph
from scripts.prepare_boot_view_guides import render_boot


class BootSurfaceTrialTest(unittest.TestCase):
    def test_depth_is_actual_proxy_z_buffer_and_old_api_is_unchanged(self):
        config = {
            "canvas": [64, 64], "camera_yaw_degrees": 35, "camera_elevation_degrees": 8,
            "proportions": {"sole_thickness": 0.13, "foot_depth": 0.78, "toe_box_height": 0.48,
                            "shaft_width": 0.76, "shaft_depth": 0.72},
        }
        ordinary = render_boot([20, 42], [45, 42], config)
        self.assertEqual(len(ordinary), 3)
        material, _, depth, annotations = render_boot([20, 42], [45, 42], config, include_depth=True)
        self.assertEqual(depth.size, material.size)
        self.assertEqual(depth.getbbox(), material.getchannel("A").getbbox())
        values = list(depth.get_flattened_data())
        self.assertGreater(max(values), min(value for value in values if value))
        self.assertIn("brighter boot pixels are nearer", annotations["depth_convention"])

    def test_depth_pair_changes_only_strength_and_output_name(self):
        low, high = graph(0.45), copy.deepcopy(graph(0.80))
        self.assertEqual(high["16"]["inputs"]["strength"], 0.80)
        high["16"]["inputs"]["strength"] = 0.45
        high["19"]["inputs"]["filename_prefix"] = low["19"]["inputs"]["filename_prefix"]
        self.assertEqual(low, high)
        self.assertEqual(low["16"]["inputs"]["image"], ["14", 0])
        self.assertEqual(low["17"]["inputs"]["positive"], ["16", 0])

    def test_canny_control_has_no_depth_nodes(self):
        nodes = graph(None)
        self.assertNotIn("14", nodes)
        self.assertEqual(nodes["17"]["inputs"]["positive"], ["13", 0])


if __name__ == "__main__":
    unittest.main()
