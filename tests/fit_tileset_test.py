import importlib.util
import sys
import unittest
from pathlib import Path

import numpy as np


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "fit_tileset.py"
SPEC = importlib.util.spec_from_file_location("fit_tileset", SCRIPT_PATH)
fit_tileset = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = fit_tileset
SPEC.loader.exec_module(fit_tileset)


class FitTilesetTest(unittest.TestCase):
    def test_foreground_mask_prefers_meaningful_alpha(self):
        rgba = np.full((4, 4, 4), 255, dtype=np.uint8)
        rgba[:2, :, 3] = 0

        mask = fit_tileset.foreground_mask(rgba, 16, 24.0)

        np.testing.assert_array_equal(mask, rgba[:, :, 3] > 16)

    def test_mapping_rejects_out_of_range_generated_component(self):
        with self.assertRaisesRegex(ValueError, "every generated component"):
            fit_tileset.validate_mapping({0: 0, 1: 2}, 2)

    def test_fit_uses_requested_integer_scale_and_template_alpha(self):
        template = np.zeros((2, 2, 4), dtype=np.uint8)
        template[0, 0] = (40, 200, 40, 255)
        generated = np.zeros((2, 2, 4), dtype=np.uint8)
        generated[0, 0] = (30, 180, 30, 255)
        template_mask = template[:, :, 3] > 0
        generated_mask = generated[:, :, 3] > 0
        template_components, template_labels = fit_tileset.find_components(template_mask, 1)
        generated_components, generated_labels = fit_tileset.find_components(generated_mask, 1)

        output = fit_tileset.fit_tileset(
            template,
            generated,
            template_components,
            generated_components,
            template_labels,
            generated_labels,
            {0: 0},
            2,
        )

        self.assertEqual(output.shape, (4, 4, 4))
        expected_alpha = np.repeat(np.repeat(template_mask, 2, axis=0), 2, axis=1) * 255
        np.testing.assert_array_equal(output[:, :, 3], expected_alpha)


if __name__ == "__main__":
    unittest.main()
