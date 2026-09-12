import unittest
from pathlib import Path

from PIL import Image, ImageDraw

from scripts.review_leg_pose_trial import foreground_mask, measure


class LegPoseTrialReviewTest(unittest.TestCase):
    def image(self, box, size=(1024, 1024)):
        image = Image.new("RGB", size, "white")
        ImageDraw.Draw(image).rectangle(box, fill=(80, 50, 30))
        return image

    def test_identical_silhouette_has_unit_scores(self):
        target = self.image((200, 300, 399, 499))
        result = measure(target, target.copy(), [88, 136, 184, 232])
        self.assertEqual(result["silhouette_iou"], 1)
        self.assertEqual(result["area_ratio"], 1)
        self.assertEqual(result["centroid_drift_working_pixels"], 0)

    def test_fixed_registration_exposes_translation(self):
        target = self.image((200, 300, 399, 499))
        shifted = self.image((264, 300, 463, 499))
        result = measure(target, shifted, [88, 136, 184, 232])
        self.assertAlmostEqual(result["centroid_drift_working_pixels"], 6)
        self.assertLess(result["silhouette_iou"], 1)

    def test_off_white_matte_is_not_foreground(self):
        image = Image.new("RGB", (8, 8), (246, 247, 245))
        self.assertFalse(foreground_mask(image).any())

    def test_isolated_border_noise_does_not_expand_bounds(self):
        image = self.image((200, 300, 399, 499))
        image.putpixel((0, 1023), (0, 0, 0))
        result = measure(self.image((200, 300, 399, 499)), image, [88, 136, 184, 232])
        self.assertEqual(result["silhouette_iou"], 1)


if __name__ == "__main__":
    unittest.main()
